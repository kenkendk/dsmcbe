#include <dsmcbe_csp.h>
#include <Shared.h>
#include <guids.h>
#include <debug.h>
#include <stdlib.h>
#include <dsmcbe.h>

#ifndef MALLOC
#define MALLOC(x) malloc(x)
#endif

#ifndef FREE
#define FREE(x) free(x)
#endif

//This function handles solving all rows where the upper and lower rows actually exit
// and does not update the edge values
DATATYPE straightForwardSolver(struct VROW* vrow, DATATYPE** rowbuffer, unsigned int firstRowIndex, unsigned int rowcount)
{
	size_t i;
	DATATYPE delta = 0;

	for(i = 0; i < rowcount; i++)
	{
		DATATYPE* upperRow = rowbuffer[i];
		DATATYPE* middleRow = rowbuffer[i + 1];
		DATATYPE* lowerRow = rowbuffer[i + 2];

		//The the first element to process
		size_t x =  (firstRowIndex + vrow->halfIterationCount + i) % 2;

		//If it is on an edge, skip it
		x += (x ^ 1) * 2; //Branch free version of: if (x == 0) x = 2;

		//TODO: With the proper data layout this loop can be vectorized

		//Update all elements inside the row
		for(; x < vrow->rowWidth - 1; x+= 2) {
			DATATYPE oldvalue = middleRow[x];
			DATATYPE newvalue =
				(middleRow[x] +
				 middleRow[x + 1] +
				 middleRow[x - 1] +
				 lowerRow[x] +
				 upperRow[x]) / 5;
			delta += ABS(oldvalue - newvalue);
			middleRow[x] = newvalue;
		}
	}

	vrow->halfIterationCount++;

	return delta;
}


//The actual SOR solver
int runSolver()
{
	struct MACHINE_SETUP* setup;
	struct VROW* vrow0;
	struct VROW* vrow1;
	struct VROW* vrow2;

	size_t i;

	DATATYPE* epsilon_storage;
	DATATYPE delta = 0;
	DATATYPE* r0;

	//Grab the setup
	CSP_SAFE_CALL("intial grab setup", dsmcbe_csp_channel_read(WORK_CHANNEL, NULL, (void**)&setup));

	//Allocate the epsilon storage
	CSP_SAFE_CALL("allocate epsilon", dsmcbe_csp_item_create((void**)&epsilon_storage, sizeof(DATATYPE)));

	//First we need to grab our initial set of data
	CSP_SAFE_CALL("intial grab vrow0", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + setup->minRow, NULL, (void**)&vrow0));
	CSP_SAFE_CALL("intial grab vrow1", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + setup->minRow + 1, NULL, (void**)&vrow1));
	CSP_SAFE_CALL("intial grab vrow2", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + setup->minRow + 2, NULL, (void**)&vrow2));

	//Initialize setup
	int direction = 1;
	unsigned int curRow = setup->minRow + 1;

	DATATYPE** rowbuffer = (DATATYPE**)MALLOC(sizeof(DATATYPE*) * (vrow1->rowCount + 2));

	//Now lets "roll" :)
	while(TRUE)
	{
		//printf(WHERESTR "Worker #%d [%d - %d] is solving using rowset [ %d, %d, %d ]\n", WHEREARG, setup->workerNumber, setup->minRow, setup->maxRow, vrow0->rowNo, vrow1->rowNo, vrow2->rowNo);

		//Special case, the first virtual row must be updated here
		if (vrow0->rowNo == 0 && vrow0->rowCount > 1) {

			//printf(WHERESTR "Solving top vrow [%d - %d]\n", WHEREARG, vrow0->rowNo * setup->numberOfRowsInVRow, vrow0->rowNo * setup->numberOfRowsInVRow + vrow0->rowCount);
			r0 = (DATATYPE*)(((char*)vrow0) + sizeof(struct VROW));
			for(i = 0; i < vrow0->rowCount; i++) {
				rowbuffer[i] = r0;
				r0 += vrow0->rowWidth;
			}

			//Last row is first row from next vrow
			rowbuffer[i] = (DATATYPE*)(((char*)vrow1) + sizeof(struct VROW));

			delta += straightForwardSolver(vrow0, rowbuffer, 1, vrow0->rowCount - 1);
		}


		//printf(WHERESTR "Solving middle vrow [%d - %d]\n", WHEREARG, vrow1->rowNo * setup->numberOfRowsInVRow, vrow1->rowNo * setup->numberOfRowsInVRow + vrow1->rowCount);

		//Solve the current set of vrows
		//First row is the last entry in the previous vrow
		rowbuffer[0] = (DATATYPE*)(((char*)vrow0) + sizeof(struct VROW)) + (vrow0->rowWidth * (vrow0->rowCount - 1));

		//Insert all rows from this block
		r0 = (DATATYPE*)(((char*)vrow1) + sizeof(struct VROW));
		for(i = 0; i < vrow1->rowCount; i++) {
			rowbuffer[i + 1] = r0;
			r0 += vrow1->rowWidth;
		}

		//Last row is first row from next vrow
		rowbuffer[i + 1] = (DATATYPE*)(((char*)vrow2) + sizeof(struct VROW));

		delta += straightForwardSolver(vrow1, rowbuffer, setup->numberOfRowsInVRow * vrow1->rowNo, vrow1->rowCount);


		//Special case, the last virtual row must be updated here
		if (vrow2->rowNo == setup->numberOfVRows - 1 && vrow0->rowCount > 1) {

			//printf(WHERESTR "Solving bottom vrow [%d - %d]\n", WHEREARG, vrow2->rowNo * setup->numberOfRowsInVRow, vrow2->rowNo * setup->numberOfRowsInVRow + vrow2->rowCount);

			//First row is the last entry in the previous vrow
			rowbuffer[0] = (DATATYPE*)(((char*)vrow1) + sizeof(struct VROW)) + (vrow1->rowWidth * (vrow1->rowCount - 1));

			r0 = (DATATYPE*)(((char*)vrow2) + sizeof(struct VROW));
			for(i = 0; i < vrow0->rowCount; i++) {
				rowbuffer[i + 1] = r0;
				r0 += vrow0->rowWidth;
			}


			delta += straightForwardSolver(vrow0, rowbuffer, setup->numberOfRowsInVRow * vrow2->rowNo, vrow2->rowCount - 1);
		}


		int mustSync;

		if (direction == 1) {
			mustSync = curRow == setup->maxRow - 1;
		} else {
			mustSync = curRow == setup->minRow + 1;
		}

		if (mustSync) {


			//If the collector wants to build a snapshot, we must have released all items
			CSP_SAFE_CALL("release top row",    dsmcbe_csp_channel_write(VROW_CHANNEL_BASE + curRow - 1, vrow0));
			CSP_SAFE_CALL("release middle row", dsmcbe_csp_channel_write(VROW_CHANNEL_BASE + curRow,     vrow1));
			CSP_SAFE_CALL("release bottom row", dsmcbe_csp_channel_write(VROW_CHANNEL_BASE + curRow + 1, vrow2));


			//printf(WHERESTR "Worker #%d Sync started with currow = %d, minrow = %d\n", WHEREARG, setup->workerNumber, curRow, setup->minRow);

			*epsilon_storage = delta;
			CSP_SAFE_CALL("write sync", dsmcbe_csp_channel_write(SYNC_CHANNEL_IN, (void*)epsilon_storage));

			//printf(WHERESTR "Worker #%d Reading sync with currow = %d, minrow = %d\n", WHEREARG, setup->workerNumber, curRow, setup->minRow);

			if (dsmcbe_csp_channel_read(SYNC_CHANNEL_OUT, NULL, (void**)&epsilon_storage) != CSP_CALL_SUCCESS)
			{
				//printf(WHERESTR "Worker #%d Poisoned with currow = %d, minrow = %d\n", WHEREARG, setup->workerNumber, curRow, setup->minRow);

				//We got poisoned
				FREE(rowbuffer);
				return 0;
			}

			//printf(WHERESTR "Worker #%d Sync completed with currow = %d, minrow = %d\n", WHEREARG, curRow, setup->minRow, setup->workerNumber);

			//Re-acquire the rows
			CSP_SAFE_CALL("re-read top row",    dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + curRow - 1, NULL, (void**)&vrow0));
			CSP_SAFE_CALL("re-read middle row", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + curRow,     NULL, (void**)&vrow1));
			CSP_SAFE_CALL("re-read bottom row", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + curRow + 1, NULL, (void**)&vrow2));

			//No poison, so we revert the direction and continue
			direction *= -1;
			delta = 0;
		}
		else
		{
			//We just keep on rolling
			if (direction == 1) {
				//printf(WHERESTR "Worker #%d Writing row %d to channel %d, minrow = %d\n", WHEREARG, setup->workerNumber, vrow0->rowNo, VROW_CHANNEL_BASE + curRow - 1, setup->minRow);

				CSP_SAFE_CALL("writing back row", dsmcbe_csp_channel_write(VROW_CHANNEL_BASE + curRow - 1, vrow0));

				//Roll the index by one vrow
				vrow0 = vrow1;
				vrow1 = vrow2;

				CSP_SAFE_CALL("reading row", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + curRow + 2, NULL, (void**)&vrow2));

				//printf(WHERESTR "Worker #%d read row %d from channel %d, minrow = %d\n", WHEREARG, setup->workerNumber, vrow2->rowNo, VROW_CHANNEL_BASE + curRow + 2, setup->minRow);

				curRow++;
			} else {
				//printf(WHERESTR "Worker #%d Writing row %d to channel %d, minrow = %d\n", WHEREARG, setup->workerNumber, vrow2->rowNo, VROW_CHANNEL_BASE + curRow + 1, setup->minRow);

				CSP_SAFE_CALL("writing back row", dsmcbe_csp_channel_write(VROW_CHANNEL_BASE + curRow + 1, vrow2));

				//Roll the index by one vrow
				vrow2 = vrow1;
				vrow1 = vrow0;
				CSP_SAFE_CALL("reading row", dsmcbe_csp_channel_read(VROW_CHANNEL_BASE + curRow - 2, NULL, (void**)&vrow0));

				//printf(WHERESTR "Worker #%d read row %d from channel %d, minrow = %d\n", WHEREARG, setup->workerNumber, vrow0->rowNo, VROW_CHANNEL_BASE + curRow - 2, setup->minRow);

				curRow--;
			}

		}

	}
}
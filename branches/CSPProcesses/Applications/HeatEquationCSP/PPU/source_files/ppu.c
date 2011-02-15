#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include <malloc_align.h>
#include <free_align.h>
#include <math.h>
#include <unistd.h>
#include <dsmcbe_csp.h>
#include <dsmcbe_ppu.h>
#include <debug.h>

#include "../header_files/StopWatch.h"
#include "../header_files/guids.h"
#include "../header_files/PPMReaderWriter.h"
#include "../header_files/Shared.h"

#define MIN_VALUE (-273)
#define MAX_VALUE (40)

#define SINGLE_DELTA

int dropImage(int index, unsigned int vrows, unsigned int rows_pr_vrow, unsigned int image_width, unsigned int image_height) {

	struct IMAGE_FORMAT format;
	format.width = image_width;
	format.height = image_height;
	format.image = (struct RGB*)malloc(sizeof(struct RGB) * format.width * format.height);

	size_t i;
	size_t x;
	size_t y;

	struct VROW* vrow;
	DATATYPE* data;
	struct RGB* img;

	for(i = 0; i < vrows; i++) {
		CSP_SAFE_CALL("read vrow for display", dsmcbe_csp_channel_read(VROW_PPU_CHANNEL_BASE + i, NULL, (void**)&vrow));

		data = (DATATYPE*)((vrow) + 1);
		img = format.image + (image_width * vrow->rowNo * rows_pr_vrow);

		if (vrow->halfIterationCount % 2 != 0)
			fprintf(stderr, WHERESTR "Vrow %d was was supposed to be in a consistent state but had halfiterationcount=%d\n", WHEREARG, vrow->rowNo, vrow->halfIterationCount);

		for(y = 0; y < vrow->rowCount; y++) {
			for(x = 0; x < vrow->rowWidth; x++)	{

				unsigned int color;
				DATATYPE value = data[x];

				if (value < 0)
					color = MIN(abs((int)(value)),255);
				else
					color = MIN(255,6*((int)(value)))*256*256; //#Max 40C so we increase

				img[x].r = (color >> 16) & 0xff;
				img[x].g = (color >> 8) & 0xff;
				img[x].b = color & 0xff;
			}

			data += vrow->rowWidth;
			img += format.width;
		}

		CSP_SAFE_CALL("write vrow back", dsmcbe_csp_channel_write(VROW_PPU_CHANNEL_BASE + i, (void*)vrow));
	}


	char filenamebuf[200];
	sprintf(filenamebuf, "outfile-%d.ppm", index);
	writeimage_rgb(filenamebuf, &format);

	free(format.image);

	return CSP_CALL_SUCCESS;
}

int main(int argc, char* argv[])
{
	int spu_count;
	int spu_fibers;
	unsigned int iteration_count = 1000;
	unsigned int image_width = 348;
	unsigned int image_height = 348;
	unsigned int rows_pr_vrow = 10;

	struct WORKER_SETUP* setup;
	struct VROW* vrow;
	DATATYPE* data;
	size_t x;
	size_t y;
	size_t i;
	size_t j;
	unsigned int vrows;
	unsigned int vrowindex = 0;
	DATATYPE delta;
	unsigned int rowsprworker;
	unsigned int oddrows;
	unsigned int workercount;

	if(argc == 3) {
		spu_count = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
	} else if(argc == 7) {
		spu_count = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
		image_width = atoi(argv[3]);
		image_height = atoi(argv[4]);
		rows_pr_vrow = atoi(argv[5]);
		iteration_count = atoi(argv[6]);
	} else {
		printf("Wrong number of arguments %i\n", argc);
		printf("Usage:\n");
		printf(" ./HeatEquationCSP <spu-count> <spu-fibers>\n");
		printf(" ./HeatEquationCSP <spu-count> <spu-fibers> <image width> <image height> <vrow size> <iterations>\n");
		return -1;
	}
	
	//Start timer!
	sw_init();
	sw_start();

	pthread_t* threads;	
	char timer_buffer[256];

	workercount = spu_count * spu_fibers;
	if (workercount <= 0)
	{
		REPORT_ERROR("Must specify at least one SPE for execution");
		exit(-1);
	}

	threads = dsmcbe_simpleInitialize(0, NULL, spu_count, spu_fibers);

	dsmcbe_rc_register_keyboard_debug_handler();



	//Setup the simulation environment

	//Figure out how many virtual rows we need to create
	vrows = (image_height + (rows_pr_vrow - 1)) / rows_pr_vrow;

	if (vrows > (VROW_SPU_CHANNEL_BASE_DOWN - VROW_PPU_CHANNEL_BASE) || vrows > (VROW_SPU_CHANNEL_BASE_UP - VROW_SPU_CHANNEL_BASE_DOWN)) {
		REPORT_ERROR2("Invalid number of vrows: %d", vrows);
		exit(-1);
	}

	//Create all the virtual rows
	for(vrowindex = 0; vrowindex < vrows; vrowindex++)
	{
		CSP_SAFE_CALL("create work item", dsmcbe_csp_item_create((void**)&vrow, sizeof(struct VROW) + (sizeof(DATATYPE) * image_width * rows_pr_vrow)));
		data = (DATATYPE*)(((char*)vrow) + sizeof(struct VROW));

		unsigned int actual_rows = vrowindex == vrows - 1 ? image_height - (rows_pr_vrow * (vrows - 1)) : rows_pr_vrow;

		for(y = 0; y < actual_rows; y++)
		{
			//Set all data to zero
			for(x = 0; x < image_width; x++)
				data[(y * image_width) + x] = 0;

			//Set the outer values to absolute zero
			data[(y * image_width)] = MIN_VALUE;
			data[((y + 1) * image_width) - 1] = MIN_VALUE;
		}

		//Set the first row to +40
		if (vrowindex == 0)
			for(x = 0; x < image_width; x++)
				data[x] = MAX_VALUE;

		//Set the last row to absolute zero
		if (vrowindex == vrows - 1)
			for(x = 0; x < image_width; x++)
				data[x + (image_width * (actual_rows - 1))] = MIN_VALUE;

		vrow->halfIterationCount = 0;
		vrow->rowCount = actual_rows;
		vrow->rowNo = vrowindex;
		vrow->rowWidth = image_width;

		CSP_SAFE_CALL("create vrow channel", dsmcbe_csp_channel_create(VROW_PPU_CHANNEL_BASE + vrowindex, 1, CSP_CHANNEL_TYPE_ONE2ONE));
		CSP_SAFE_CALL("create vrow channel", dsmcbe_csp_channel_create(VROW_SPU_CHANNEL_BASE_DOWN + vrowindex, 1, CSP_CHANNEL_TYPE_ONE2ONE));
		CSP_SAFE_CALL("create vrow channel", dsmcbe_csp_channel_create(VROW_SPU_CHANNEL_BASE_UP + vrowindex, 1, CSP_CHANNEL_TYPE_ONE2ONE));
		CSP_SAFE_CALL("write work item", dsmcbe_csp_channel_write(VROW_PPU_CHANNEL_BASE + vrowindex, (void*)vrow));
	}

	vrowindex = 0;

	rowsprworker = (vrows - 2) / workercount;
	oddrows = (vrows - 2) - (rowsprworker * workercount);

	if (rowsprworker < 3)
	{
		fprintf(stderr, "Less than 3 rows pr. worker, invalid setup\n");
		exit(-1);
	}

	//Create the work channel, non-blocking for the entire setup
	CSP_SAFE_CALL("create work channel", dsmcbe_csp_channel_create(WORK_CHANNEL, workercount, CSP_CHANNEL_TYPE_ONE2ANY));

	//Create the worker setups
	for(i = 0; i < workercount; i++)
	{
		CSP_SAFE_CALL("create worker", dsmcbe_csp_item_create((void**)&setup, sizeof(struct WORKER_SETUP)));

		int rows_for_worker = i < oddrows ? rowsprworker + 1 : rowsprworker;

		setup->workerNumber = i;
		setup->isFirstWorker = i == 0;
		setup->isLastWorker = i == workercount - 1;
		setup->minRow = vrowindex;
		setup->maxRow = vrowindex + rows_for_worker + 1;
		setup->numberOfRows = image_height;
		setup->numberOfRowsInVRow = rows_pr_vrow;
		setup->numberOfVRows = vrows;
		setup->rowWidth = image_width;
#ifdef SINGLE_DELTA
		setup->deltaSyncIterations = iteration_count * 2;
#else
		setup->deltaSyncIterations = 1;
#endif

		//printf(WHERESTR "Created worker number %d, with [%d - %d]\n", WHEREARG, setup->workerNumber, setup->minRow, setup->maxRow);
		CSP_SAFE_CALL("write worker", dsmcbe_csp_channel_write(WORK_CHANNEL, (void*)setup));
		vrowindex += rows_for_worker;
	}


	//Setup the sync channels
	CSP_SAFE_CALL("create sync in channel", dsmcbe_csp_channel_create(SYNC_CHANNEL_IN, workercount, CSP_CHANNEL_TYPE_ANY2ONE));
	CSP_SAFE_CALL("create sync out channel", dsmcbe_csp_channel_create(SYNC_CHANNEL_OUT, workercount, CSP_CHANNEL_TYPE_ONE2ANY));

	printf("Running simulation on image (%d x %d) with %d iterations, %d workers, %d vrows, %d rows pr vrow\n", image_width, image_height, iteration_count, workercount, vrows, rows_pr_vrow);

	//All ready just collect the results
	DATATYPE** storage = (DATATYPE**)malloc(sizeof(DATATYPE*) * workercount);

#ifdef SINGLE_DELTA
	iteration_count = 1;
#else
	//The iterations are only half a round
	iteration_count *= 2;
#endif
	for(i = 0; i < iteration_count; i++) {
		delta = 0;

		for(j = 0; j < workercount; j++) {
			//printf("Reading sync %d of %d\n", j+1, workercount);
			CSP_SAFE_CALL("read sync", dsmcbe_csp_channel_read(SYNC_CHANNEL_IN, NULL, (void**)&storage[j]));
			delta += *storage[j];
			//printf("Read sync %d of %d\n", j+1, workercount);
		}

		//printf("All workers synced for iteration %d, delta is %f\n", i, delta);

		if (i != iteration_count - 1) {
			for(j = 0; j < workercount; j++) {
				CSP_SAFE_CALL("write sync", dsmcbe_csp_channel_write(SYNC_CHANNEL_OUT, (void*)storage[j]));
			}
		}

		if (i % 200 == 0)
			printf("All workers synced for iteration %d, delta is %f\n", i / 2, delta);
	}

	CSP_SAFE_CALL("poison sync", dsmcbe_csp_channel_poison(SYNC_CHANNEL_OUT));

	//Save the final image
	dropImage(i / 2, vrows, rows_pr_vrow, image_width, image_height);

	// Stop timer
	sw_stop();
	sw_timeString(timer_buffer);
	printf("Time used: %s\n", timer_buffer);
	printf("Total seconds used: %f\n", readTotalSeconds());
	
	return 0;
}

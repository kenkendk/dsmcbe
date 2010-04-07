#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>

#include <datapackages.h>

#define REPETITIONS 100000

unsigned int id;
char* file;
unsigned int SPU_THREADS;
extern spe_context_ptr_t* spe_threads;

int main(int argc, char **argv) {
	
	unsigned long size;
	unsigned int items;
	size_t i;

	if(argc == 4) {
		id = atoi(argv[1]);
		file = argv[2]; 	
		SPU_THREADS = atoi(argv[3]);		 
	} else if (argc == 2) {
		id = 0;
		file = NULL; 	
		SPU_THREADS = atoi(argv[1]);		 		
	} else {
		printf("Wrong number of arguments \"./PPU id network-file spu-threads\"\n");
		return -1;
	}
	
	printf(WHERESTR "id: %i, file %s, SPU_THREADS %i\n", WHEREARG, id, file, SPU_THREADS);
	

	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(id, file, SPU_THREADS);

	/*printf(WHERESTR "Starting\n", WHEREARG);
	release(create(700, 4));
	printf(WHERESTR "Starting\n", WHEREARG);

	for(i = 0; i < 10000000; i++)
	{
		if (i % 1000 == 0)
			printf("i: %d\n", i);
		release(acquire(700, &size, ACQUIRE_MODE_WRITE));
	}*/

	int* data;

	printf(WHERESTR "%d: Connected, starting\n", WHEREARG, id);

//	#define STEP1
//	#define STEP2
//	#define STEP3

	if (id == 0)
	{
		sleep(1);
		printf(WHERESTR "%d: Creating\n", WHEREARG, id);
		data = create(ETTAL, sizeof(int), CREATE_MODE_NONBLOCKING);

		(*data) = 928;
		
		printf(WHERESTR "%d: Data location is %i\n", WHEREARG, id, (unsigned int)data);
		
		printf(WHERESTR "%d: Releasing\n", WHEREARG, id);
		release(data);
	}
	else
	{
		printf(WHERESTR "%d: Reading\n", WHEREARG, id);
		data = acquire(ETTAL, &size, ACQUIRE_MODE_READ);

		printf(WHERESTR "%d: Read: %d\n", WHEREARG, id, *data);
		
		release(data);

		printf(WHERESTR "%d: Released\n", WHEREARG, id);
	}
	
	//sleep(10);
	

	if (SPU_THREADS != 0)
	{
		release(acquire(LOCK_ITEM_SPU, &size, ACQUIRE_MODE_READ));
		//sleep(1);
		
		printf(WHERESTR "Updating data\n", WHEREARG);
		
		data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
		
		*data = 210;
		
		//printf(WHERESTR "Data location is %i\n", WHEREARG, (unsigned int)data);
		printf(WHERESTR "Value is %i. The expected value is 210.\n", WHEREARG, *data);
		
		//printf(WHERESTR "Releasing, this should invalidate SPU\n", WHEREARG);
		release(data);
		
		release(create(LOCK_ITEM_PPU, 0, CREATE_MODE_NONBLOCKING));
		printf(WHERESTR "Released\n", WHEREARG);
	
		
		items = 25 * 1024;
		unsigned int* largeblock = create(LARGE_ITEM, items * sizeof(unsigned int), CREATE_MODE_NONBLOCKING);
	
		printf(WHERESTR "Created large block at %d\n", WHEREARG, (unsigned int)largeblock);
		for(i = 0; i < items; i++)
			largeblock[i] = i;
		
		printf(WHERESTR "Releasing large block with %d items\n", WHEREARG, items);
		release(largeblock);
	
	
		printf(WHERESTR "Creating large sequence, %d blocks of size %d\n", WHEREARG, SEQUENCE_COUNT, items * sizeof(unsigned int));
		for(i = 0; i < SEQUENCE_COUNT; i++)
		{
			void* dataitem = create(LARGE_SEQUENCE + i, items * sizeof(unsigned int), CREATE_MODE_NONBLOCKING);
			if (dataitem == NULL)
				printf(WHERESTR "Failed to create item with ID: %d\n", WHEREARG, LARGE_SEQUENCE + i);
			else
				release(dataitem);
		}
	
		/*printf(WHERESTR "Creating small sequence, %d blocks of size %d\n", WHEREARG, SMALL_SEQUENCE_COUNT, sizeof(unsigned int));
		for(i = 0; i < SMALL_SEQUENCE_COUNT; i++)
			release(create(SMALL_SEQUENCE + i, sizeof(unsigned int)));
		*/
		
/*		unsigned int result = 0;
		int value = 0;
		
		
		while(1){
			value = spe_mfcio_tag_status_read(spe_threads[0], 0, SPE_TAG_IMMEDIATE, &result);
			printf("DMA Transfer status value: %i result: %u errno: %d\n", value, result , errno);
}*/
		
		printf(WHERESTR "Acquiring SPU item\n", WHEREARG);
		data = acquire(SPUITEM, &size, ACQUIRE_MODE_WRITE);
		printf(WHERESTR "Acquired SPU item, value is %d. Expected value 4.\n", WHEREARG, (*data));
		release(data);
		printf(WHERESTR "Released SPU item\n", WHEREARG);
		
		/*
		
		printf(WHERESTR "Re-acquire\n", WHEREARG);
		largeblock = acquire(LARGE_ITEM, &size, WRITE);
		printf(WHERESTR "Acquired large block at %d (%d)\n", WHEREARG, (unsigned int)largeblock, (unsigned int)size);
		items = size / sizeof(unsigned int);
		
		for(i = 0; i < items; i++)
		{
			if (largeblock[i] != (i + (2 * SPU_FIBERS)))
				printf(WHERESTR "Invalid value at %d\n", WHEREARG, i);
		}
			
		release(largeblock);
		printf(WHERESTR "Tests completed, shutting down\n", WHEREARG);
		
		*/
		
		//printf(WHERESTR "Released, waiting for SPU to complete\n", WHEREARG);
				
		for(i = 0; i < SPU_THREADS; i++)
			pthread_join(spu_threads[i], NULL);
	
		//printf(WHERESTR "All SPU's are terminated, cleaning up\n", WHEREARG);
		//terminate();
	} else {
		
		//Step 1, repeated acquire, owner in write mode, others in read mode

		int previous;
		previous = REPETITIONS;

		printf(WHERESTR "Starting test\n", WHEREARG);
		
#ifdef STEP1

		if (id == PAGE_TABLE_OWNER)
		{
			printf(WHERESTR "Reset number\n", WHEREARG);
			data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
			*data = 0;
			release(data);
			printf(WHERESTR  "number was reset\n", WHEREARG);
		}
		else
		{
			while(previous != 0)
			{
				data = acquire(ETTAL, &size, ACQUIRE_MODE_READ);
				previous = *data;
				release(data);
			}				
			printf(WHERESTR "Reset detected\n", WHEREARG);
		}
		
		printf(WHERESTR "Contention test 1\n", WHEREARG);

		sleep(10);
		
		if (id == PAGE_TABLE_OWNER)
		{
			for(i = 0; i < REPETITIONS; i++)
			{
				printf("i: %d\n", i);
				data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				*data = i;
				release(data);
			}
		}
		else
		{
			while(previous < (REPETITIONS - 1))
			{
				data = acquire(ETTAL, &size, ACQUIRE_MODE_READ);
				if (*data >= previous)
				{
					if (*data != previous)
						printf("i: %d\n", *data);
					previous = *data;
				}
				else	
					printf(WHERESTR "number decreased?\n", WHEREARG);
				release(data);

				//printf("prev: %d\n", previous);
			}
		}

		printf(WHERESTR "Test 1 complete, starting test 2\n", WHEREARG);
				
		sleep(5);
#endif

#ifdef STEP2
		
		//Step 2, repeated acquire, owner in read mode, others in write mode
		previous = REPETITIONS;

		if (id != PAGE_TABLE_OWNER)
		{
			printf(WHERESTR "Starting acquire\n", WHEREARG);
			data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
			*data = 0;
			printf(WHERESTR "data is %i\n", WHEREARG, *data);			
			release(data);			
			printf(WHERESTR "Reset number\n", WHEREARG);
		}
		else
		{
			while(previous != 0)
			{
				//printf(WHERESTR "Starting acquire\n", WHEREARG);
				data = acquire(ETTAL, &size, ACQUIRE_MODE_READ);			
				previous = *data;
				//printf(WHERESTR "data is %i\n", WHEREARG, previous);
				release(data);
				//printf(WHERESTR "data is %i\n", WHEREARG, previous);

			}				
			printf(WHERESTR "Reset detected\n", WHEREARG);
		}
		
		printf(WHERESTR "Starting contetion test 2\n", WHEREARG);
		sleep(10);
		
		if (id != PAGE_TABLE_OWNER)
		{
			for(i = 0; i < REPETITIONS; i++)
			{
				printf("i: %d\n", i);
				data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				*data = i;
				release(data);
			}
		}
		else
		{
			while(previous < (REPETITIONS - 1))
			{
				data = acquire(ETTAL, &size, ACQUIRE_MODE_READ);
				if (*data >= previous)
				{
					if (*data != previous)
						printf("i: %d\n", *data);
					previous = *data;
				}
				else	
					printf(WHERESTR "number decreased?\n", WHEREARG);
				release(data);
			}
		}
		
		printf(WHERESTR "Test complete\n", WHEREARG);
		sleep(10);
#endif				

#ifdef STEP3
		
		//Step 3, repeated acquire, both in write mode
		previous = REPETITIONS;

		if (id != PAGE_TABLE_OWNER)
		{
			printf(WHERESTR "Starting acquire\n", WHEREARG);
			data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
			*data = 0;
			printf(WHERESTR "data is %i\n", WHEREARG, *data);			
			release(data);			
			printf(WHERESTR "Reset number\n", WHEREARG);
		}
		else
		{
			while(previous != 0)
			{
				//printf(WHERESTR "Starting acquire\n", WHEREARG);
				data = acquire(ETTAL, &size, ACQUIRE_MODE_READ);			
				previous = *data;
				//printf(WHERESTR "data is %i\n", WHEREARG, previous);
				release(data);
				//printf(WHERESTR "data is %i\n", WHEREARG, previous);

			}				
			printf(WHERESTR "Reset detected\n", WHEREARG);
		}
		
		printf(WHERESTR "Starting contetion test 3\n", WHEREARG);
		sleep(10);
	
		
		if (id != PAGE_TABLE_OWNER)
		{
			for(i = 0; i < REPETITIONS; i++)
			{
				printf("i: %d\n", i);
				data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				while(*data % 2 == 0){
					release(data);
					data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				}

				//printf(WHERESTR "ID %i: Data was %i\n", WHEREARG, id, *data);
				*data = *data + 1;
				previous = *data;				
				release(data);
			}
		}
		else
		{
			for(i = 0; i < REPETITIONS; i++)
			{
				printf("i: %d\n", i);
				data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				while(*data % 2 != 0){
					release(data);
					data = acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				}

				//printf(WHERESTR "ID %i: Data was %i\n", WHEREARG, id, *data);
				*data = *data + 1;
				previous = *data;				
				release(data);
			}
		}
		
		printf(WHERESTR "Test complete\n", WHEREARG);
		sleep(10);
#endif				
		
		//Step 4, repeated create, owner in read mode, others in write mode

		//Step 5, repeated create, owner in write mode, others in read mode

#ifdef STEP6
		//Step 6, Delete tests

#endif

	}
	//printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	return 0;
}

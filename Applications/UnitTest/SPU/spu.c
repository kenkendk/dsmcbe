#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>

//We need the counter on the heap so the threads share it
//static int counter = 0;

void TEST();

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId) {

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);
	
	printf(WHERESTR "Hello World\n", WHEREARG);
	
	unsigned long size;
	unsigned int i;
	//unsigned int items;
	int threadNo;
	int* allocation;
	
	
	//This section tests that two threads may acquire the same object in various ways:
	// 1. Both have read acces (shared pointer)
	// 2. Read is acquired and write is requested (blocks until read is released)
	// 3. Write is acquire, read i requested (blocks until write is released)
	
	/*
	int* f = &counter;
	threadNo = CreateThreads(2);
	if (threadNo >= 0)
	{
		allocation = dsmcbe_acquire(ETTAL, &size, READ);
		printf(WHERESTR "Thread #%d, Value read from acquire is: %i (ls: %d). \n", WHEREARG, threadNo, *allocation, (int)allocation);

		//Force thread 1
		while((*f) == 0 && threadNo != 1)
			YieldThread();
		
		if (threadNo == 1)
			(*f)++;

		while((*f) == 1 && threadNo != 0)
			YieldThread();
		
		if (threadNo == 0)
			(*f)++;

		printf(WHERESTR "Thread #%d, release (%d). \n", WHEREARG, threadNo, counter);
		dsmcbe_release(allocation);
		printf(WHERESTR "Thread #%d, released (%d). \n", WHEREARG, threadNo, counter);
		YieldThread();
		
		//Force thread 0
		while((*f) == 2 && threadNo != 0)
			YieldThread();
		
		if (threadNo == 0)
		{
			printf(WHERESTR "Thread #%d, acquire read (%d). \n", WHEREARG, threadNo, counter);
			allocation = dsmcbe_acquire(ETTAL, &size, READ);
			printf(WHERESTR "Thread #%d, has read lock (%d). \n", WHEREARG, threadNo, counter);
			(*f)++;
		}
		
		//Force thread 1
		while((*f) == 3 && threadNo != 1)
			YieldThread();

		if (threadNo == 1)
		{		
			printf(WHERESTR "Thread #%d, is acquire'ing write lock. \n", WHEREARG, threadNo);
			(*f)++;
			allocation = dsmcbe_acquire(ETTAL, &size, WRITE);
			printf(WHERESTR "Thread #%d, has write lock. \n", WHEREARG, threadNo);
		}
		
		//Thread 0 has a read lock, and thread 1 is awaiting is write lock
		
		//Force thread 0
		while((*f) == 4 && threadNo != 0)
			YieldThread();
		
		if (threadNo == 0)
		{
			printf(WHERESTR "Thread #%d, is releasing read lock. \n", WHEREARG, threadNo);
			dsmcbe_release(allocation);
			printf(WHERESTR "Thread #%d, has released read lock. \n", WHEREARG, threadNo);

			(*f)++;
			printf(WHERESTR "Thread #%d, is getting read lock. \n", WHEREARG, threadNo);
			dsmcbe_acquire(ETTAL, &size, READ);
			printf(WHERESTR "Thread #%d, has read lock. \n", WHEREARG, threadNo);
		}
			
		printf(WHERESTR "Thread #%d, release. \n", WHEREARG, threadNo);
		dsmcbe_release(allocation);
		
		printf(WHERESTR "Thread #%d, terminate. \n", WHEREARG, threadNo);
		TerminateThread();
	}

	sleep(5);*/

	/*void* test;
	void* test2;

	printf(WHERESTR "Start malloc test\n", WHEREARG);
	for( i = 0; i < 10000000; i++)
	{
		allocation = (int*)malloc(24);
		test2 = (int*)malloc(24);
		test = _malloc_align(1000 + (i % 32) * 4, 7);
		if (allocation == NULL)
			printf("Failed alloc %d\n", i);
		if (test == NULL)
			printf("Failed test %d\n", i);
		if (i % 5000 != 0)
			free(allocation);
		free(test2);
		_free_align(test);
	}
	printf(WHERESTR "Done malloc test\n", WHEREARG);
	sleep(5);*/

	if (SPU_FIBERS > 1)
		threadNo = CreateThreads(SPU_FIBERS);
	else
		threadNo = 0;
	
	if (threadNo >= 0)
	{
		//printf(WHERESTR "Thread #%d, acquire.\n", WHEREARG, threadNo);
		allocation = dsmcbe_acquire(ETTAL, &size, ACQUIRE_MODE_WRITE);
				
		printf(WHERESTR "Thread #%d, Value read from acquire is: %i. The value is supposed to be %d. (ls: %d)\n", WHEREARG, threadNo, *allocation, threadNo == 0 ? 928 : 210, (int)allocation);
		
		*allocation = 210;
		
		dsmcbe_release(allocation);
		*allocation = 111;

		printf(WHERESTR "Thread #%d, released, creating SPU lock.\n", WHEREARG, threadNo);

		dsmcbe_release(dsmcbe_create(LOCK_ITEM_SPU, 0));
		//sleep(2);		

		printf(WHERESTR "Thread #%d, waiting for PPU lock.\n", WHEREARG, threadNo);
		dsmcbe_release(dsmcbe_acquire(LOCK_ITEM_PPU, &size, ACQUIRE_MODE_READ));
		
		printf(WHERESTR "Thread #%d, acquire.\n", WHEREARG, threadNo);
		allocation = dsmcbe_acquire(ETTAL, &size, ACQUIRE_MODE_READ);

		/*if (*allocation != 210)
			printf("Error: %d\n", *allocation);
		else
			printf("OK\n");*/
		printf(WHERESTR "Thread #%d, Value read from acquire is: %i. The value is supposed to be %d. (ls: %d)\n", WHEREARG, threadNo, *allocation, threadNo == 0 ? 210 : 210, (int)allocation);
		
		*allocation = 210;
		
		dsmcbe_release(allocation);
		
/*				
		printf(WHERESTR "Thread #%d, modified value, relasing.\n", WHEREARG, threadNo);
		dsmcbe_release(allocation);
		printf(WHERESTR "Thread #%d, release completed\n", WHEREARG, threadNo);
		
		printf(WHERESTR "Thread #%d, reading large value\n", WHEREARG, threadNo);
		unsigned int* largeblock = dsmcbe_acquire(LARGE_ITEM, &size, WRITE);
		
		items = size / sizeof(unsigned int);
		printf(WHERESTR "Thread #%d, read large value (%d, %d) (ls: %d)\n", WHEREARG, threadNo, (int)size, items, (int)largeblock);

	
		//The first to get here has counter == 0
		for(i = 0; i < items; i++)
		{
			if (largeblock[i] != (i + (counter * 2)))
				printf(WHERESTR "Thread #%d, Invalid value at %d\n", WHEREARG, threadNo, i);
			if (IsThreaded())
				largeblock[i] = i + ((counter + 1) * 2);
			else
				largeblock[i] = i + ((SPU_FIBERS) * 2);
		}
		counter++;
		
		printf(WHERESTR "Thread #%d, releasing large value\n", WHEREARG, threadNo);
		dsmcbe_release(largeblock);
				
		printf(WHERESTR "Thread #%d, released large value\n", WHEREARG, threadNo);

		printf(WHERESTR "Thread #%d, (read) re-acquire value\n", WHEREARG, threadNo);
		allocation = dsmcbe_acquire(ETTAL, &size, READ);
		printf(WHERESTR "Thread #%d, re-acquired value (ls: %d)\n", WHEREARG, threadNo, (int)allocation);
		dsmcbe_release(allocation);
		printf(WHERESTR "Thread #%d, released value\n", WHEREARG, threadNo);

		printf(WHERESTR "Thread #%d, (write) re-acquire value\n", WHEREARG, threadNo);
		allocation = dsmcbe_acquire(ETTAL, &size, WRITE);
		printf(WHERESTR "Thread #%d, re-acquired value (ls: %d)\n", WHEREARG, threadNo, (int)allocation);
		dsmcbe_release(allocation);
		printf(WHERESTR "Thread #%d, released value\n", WHEREARG, threadNo);
*/
		int j = 0;
		printf(WHERESTR "Reading large sequence...\n", WHEREARG);
		for(i = 0; i < SEQUENCE_COUNT; i++)
		{
			dsmcbe_release(dsmcbe_acquire(LARGE_SEQUENCE + i, &size, ACQUIRE_MODE_READ));
			if (i % 1000 == 0)
				printf(WHERESTR "Read large sequence %d\n", WHEREARG, i);
			for(j = 0; j < 10; j++)			
				printf("DMA Transfer status value: %i\n", mfc_read_tag_status_immediate());
				
			mfc_stat_tag_status();
				
		}

		/*printf(WHERESTR "Reading small sequence...\n", WHEREARG);
		for(i = 0; i < SMALL_SEQUENCE_COUNT; i++)
		{
			release(acquire(SMALL_SEQUENCE + i, &size, READ));
			if (i % 1000 == 0)
				printf(WHERESTR "Read small sequence %d\n", WHEREARG, i);	
		}*/


		//Memory leak testing, the SPU memory is very limited so a million runs usually reveal the problem
		for(i = 0; i < 1000000; i++)
		{
			if (i % 10000 == 0)
			{
				printf(WHERESTR "Thread #%d, performing memory test %d of 1000000\n", WHEREARG, threadNo, i);
			}
			dsmcbe_release(dsmcbe_acquire(LARGE_ITEM, &size, ACQUIRE_MODE_WRITE));
		}

		if (SPU_FIBERS > 1)
			TerminateThread();
	}
	
	//printf(WHERESTR "Creating new item\n", WHEREARG);
	allocation = dsmcbe_create(SPUITEM, sizeof(unsigned int));
	//printf(WHERESTR "Created new item\n", WHEREARG);
	*allocation = 4;
	//printf(WHERESTR "Releasing new item\n", WHEREARG);
	dsmcbe_release(allocation);
	//printf(WHERESTR "Released new item\n", WHEREARG);
	
	return 0;
}


#include "spu.h"
#include <common/datastructures.h>
#include <unistd.h>
#include <malloc.h>

//We need the counter on the heap so the threads share it
static int counter = 0;

int main(int argc, char **argv) {
	
	initialize();

	//printf(WHERESTR "Hello World\n", WHEREARG);
	unsigned long size;
	unsigned int i;
	unsigned int items;
	int threadNo;
	int* allocation;

	if (SPU_FIBERS > 1)
		threadNo = CreateThreads(SPU_FIBERS);
	else
		threadNo = 0;
	
	if (threadNo != -1)
	{
		//printf(WHERESTR "Thread #%d, acquire.\n", WHEREARG, threadNo);
		allocation = acquire(ETTAL, &size, WRITE);
				
		//printf(WHERESTR "Thread #%d, Value read from acquire is: %i. The value is supposed to be %d. (ls: %d)\n", WHEREARG, threadNo, *allocation, threadNo == 0 ? 928 : 210, (int)allocation);
		
		*allocation = 210;
		
		release(allocation);
		*allocation = 111;

		release(create(LOCK_ITEM_SPU, 0));
		//sleep(2);		

		release(acquire(LOCK_ITEM_PPU, &size, READ));		

		//printf(WHERESTR "Thread #%d, acquire.\n", WHEREARG, threadNo);
		allocation = acquire(ETTAL, &size, READ);

		if (*allocation != 210)
			printf("Error: %d\n", *allocation);
		else
			printf("OK\n");
		//printf(WHERESTR "Thread #%d, Value read from acquire is: %i. The value is supposed to be %d. (ls: %d)\n", WHEREARG, threadNo, *allocation, threadNo == 0 ? 210 : 210, (int)allocation);
		
		*allocation = 210;
		
		release(allocation);
		
/*				
		printf(WHERESTR "Thread #%d, modified value, relasing.\n", WHEREARG, threadNo);
		release(allocation);
		printf(WHERESTR "Thread #%d, release completed\n", WHEREARG, threadNo);
		
		printf(WHERESTR "Thread #%d, reading large value\n", WHEREARG, threadNo);
		unsigned int* largeblock = acquire(LARGE_ITEM, &size, WRITE);
		
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
		release(largeblock);
				
		printf(WHERESTR "Thread #%d, released large value\n", WHEREARG, threadNo);

		printf(WHERESTR "Thread #%d, (read) re-acquire value\n", WHEREARG, threadNo);
		allocation = acquire(ETTAL, &size, READ);
		printf(WHERESTR "Thread #%d, re-acquired value (ls: %d)\n", WHEREARG, threadNo, (int)allocation);
		release(allocation);
		printf(WHERESTR "Thread #%d, released value\n", WHEREARG, threadNo);

		printf(WHERESTR "Thread #%d, (write) re-acquire value\n", WHEREARG, threadNo);
		allocation = acquire(ETTAL, &size, WRITE);
		printf(WHERESTR "Thread #%d, re-acquired value (ls: %d)\n", WHEREARG, threadNo, (int)allocation);
		release(allocation);
		printf(WHERESTR "Thread #%d, released value\n", WHEREARG, threadNo);
*/
		//Memory leak testing, the SPU memory is very limited so a million runs usually reveal the problem
		for(i = 0; i < 1000000; i++)
		{
			if (i % 10000 == 0)
				printf(WHERESTR "Thread #%d, performing memory test %d of 1000000\n", WHEREARG, threadNo, i);
			release(acquire(LARGE_ITEM, &size, WRITE));
		}

		if (SPU_FIBERS > 1)
			TerminateThread();
	}
	
	//printf(WHERESTR "Creating new item\n", WHEREARG);
	allocation = create(SPUITEM, sizeof(unsigned int));
	//printf(WHERESTR "Created new item\n", WHEREARG);
	*allocation = 4;
	//printf(WHERESTR "Releasing new item\n", WHEREARG);
	release(allocation);
	//printf(WHERESTR "Released new item\n", WHEREARG);
	
	terminate();
	//printf(WHERESTR "Done\n", WHEREARG);
	
	
	
	return 0;
}


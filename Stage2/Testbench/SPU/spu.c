#include "spu.h"

int main(int argc, char **argv) {
	
	initialize();
	
	printf(WHERESTR "Hello World\n", WHEREARG);
	unsigned long size;
	unsigned int i;
	unsigned int items;
	int threadNo;
	unsigned int myno;
	int* allocation;
	void* test;

	myno = 7;
	threadNo = -1;
	
	/*test = malloc(sizeof(int));
	if (test == NULL)
		perror("1 SPU malloc failed for thread storage");
	
	
	threadNo = CreateThreads(2);
	if (threadNo != -1)
	{
		printf(WHERESTR "Running in thread %d (%d).\n", WHEREARG, threadNo, myno);
		test = malloc(sizeof(int));
		if (test == NULL)
			perror("2 SPU malloc failed for thread storage");
		YieldThread();
		printf(WHERESTR "Running after yield in thread %d (%d).\n", WHEREARG, threadNo, myno);
		test = malloc(sizeof(int));
		if (test == NULL)
			perror("3 SPU malloc failed for thread storage");
		TerminateThread();
		printf(WHERESTR "!!! After terminate in thread %d (%d).\n", WHEREARG, threadNo, myno);
	}

	printf(WHERESTR "Main thread returned, restarting threads.\n", WHEREARG);
	test = malloc(sizeof(int));
	if (test == NULL)
		perror("4 SPU malloc failed for thread storage");
	
	threadNo = CreateThreads(2);
	if (threadNo != -1)
	{*/	
		printf(WHERESTR "Thread #: %d.\n", WHEREARG, threadNo);
		allocation = acquire(ETTAL, &size);
		//YieldThread();
				
		printf(WHERESTR "Thread #: %d.\n", WHEREARG, threadNo);
		printf(WHERESTR "Value read from acquire is: %i. The value is supposed to be 928.\n", WHEREARG, *allocation);
		
		*allocation = 210;
				
		release(allocation);
		//YieldThread();
		
		printf(WHERESTR "Thread #: %d.\n", WHEREARG, threadNo);
		printf(WHERESTR "Release completed\n", WHEREARG);
		
		
		printf(WHERESTR "Reading large value\n", WHEREARG);
		unsigned int* largeblock = acquire(LARGE_ITEM, &size);
		//YieldThread();
		
		printf(WHERESTR "Thread #: %d.\n", WHEREARG, threadNo);
		items = size / sizeof(unsigned int);
		printf(WHERESTR "Read large value (%d, %d)\n", WHEREARG, (int)size, items);
		
		
		for(i = 0; i < items; i++)
		{
			if (largeblock[i] != i)
				printf(WHERESTR "Ivalid value at %d\n", WHEREARG, i);
			largeblock[i] = i + 2;
		}
		
		printf(WHERESTR "Releasing large value\n", WHEREARG);
		release(largeblock);
		//YieldThread();
				
		printf(WHERESTR "Thread #: %d.\n", WHEREARG, threadNo);
		printf(WHERESTR "Released large value\n", WHEREARG);
		//TerminateThread();
	//}
	
	printf(WHERESTR "Creating new item\n", WHEREARG);
	allocation = create(SPUITEM, sizeof(unsigned int));
	printf(WHERESTR "Created new item\n", WHEREARG);
	*allocation = 4;
	printf(WHERESTR "Releasing new item\n", WHEREARG);
	release(allocation);
	printf(WHERESTR "Released new item\n", WHEREARG);
	
	//Memory leak testing, the SPU memory is very limited so a million runs usually reveal the problem
	for(i = 0; i < 1000000; i++)
	{
		if (i % 1000 == 0)
			printf(WHERESTR "Performing %d of 1000000\n", WHEREARG, i);
		release(acquire(LARGE_ITEM, &size));
	}
	
	terminate();
	printf(WHERESTR "Done\n", WHEREARG);
	
	
	
	return 0;
}

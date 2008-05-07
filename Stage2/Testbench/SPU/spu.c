#include "spu.h"

int main(int argc, char **argv) {
	
	initialize();
	
	printf(WHERESTR "Hello World\n", WHEREARG);
	unsigned long size;
	unsigned int i;
	unsigned int items;
	int threadNo;
	unsigned int myno;

	myno = -1;
	
	threadNo = CreateThreads(2);
	if (threadNo != -1)
	{
		printf(WHERESTR "Running in thread %d (%d).\n", WHEREARG, threadNo, myno);
		YieldThread();
		printf(WHERESTR "Running after yield in thread %d (%d).\n", WHEREARG, threadNo, myno);
		TerminateThread();
		printf(WHERESTR "!!! After terminate in thread %d (%d).\n", WHEREARG, threadNo, myno);
	}

	printf(WHERESTR "Main thread returned, continuing basic run.\n", WHEREARG);
	
	
	int* allocation = acquire(ETTAL, &size);

	printf(WHERESTR "Value read from acquire is: %i. The value is supposed to be 928.\n", WHEREARG, *allocation);
	
	*allocation = 210;
			
	release(allocation);
	printf(WHERESTR "Release completed\n", WHEREARG);
	
	
	printf(WHERESTR "Reading large value\n", WHEREARG);
	unsigned int* largeblock = acquire(LARGE_ITEM, &size);
	items = size / sizeof(unsigned int);
	printf(WHERESTR "Read large value (%d, %d)\n", WHEREARG, size, items);
	
	
	for(i = 0; i < items; i++)
	{
		if (largeblock[i] != i)
			printf(WHERESTR "Ivalid value at %d\n", WHEREARG, i);
		largeblock[i] = i + 2;
	}
	
	printf(WHERESTR "Releasing large value\n", WHEREARG);
	release(largeblock);
	printf(WHERESTR "Released large value\n", WHEREARG);
	
	printf(WHERESTR "Done\n", WHEREARG);
	
	return 0;
}

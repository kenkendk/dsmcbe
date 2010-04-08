#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include "../PPU/guids.h"
#include <time.h>

int pid;

void delta2(GUID in, GUID outA, GUID outB)
{
	unsigned int* inValue;
	unsigned int* outValue;

	unsigned int value;
	unsigned long size;

	while(1)
	{
		printf("delta2: Trying to get\n");
		inValue = get(in, &size);
		printf("delta2: get OK\n");
		if (inValue == NULL)
			printf("delta2 inValue is NULL\n");
		value = *inValue;
		printf("delta2: Trying to release\n");
		release(inValue);
		printf("delta2: release OK\n");

		printf("delta2: Trying to malloc\n");
		outValue = createMalloc(sizeof(int));
		printf("delta2: malloc OK\n");
		if (outValue == NULL)
			printf("delta2 outValue #1 is NULL\n");
		*outValue = value;

		printf("delta2: Trying to put\n");
		put(outA, outValue);
		printf("delta2: put OK\n");

		printf("delta2: Trying to malloc #2\n");
		outValue = createMalloc(sizeof(int));
		printf("delta2: malloc #2 OK\n");
		if (outValue == NULL)
			printf("delta2 outValue #2 is NULL\n");
		*outValue = value;

		printf("delta2: Trying to put #2\n");
		put(outB, outValue);
		printf("delta2: put #2 OK\n");
	}
}

void delta1(GUID in, GUID out)
{
	unsigned int* inValue;
	unsigned int* outValue;

	unsigned int value;
	unsigned long size;

	while(1)
	{
		printf("delta1: Trying to get\n");
		inValue = get(in, &size);
		printf("delta1: get OK\n");

		if (inValue == NULL)
			printf("delta1 inValue is NULL\n");
		value = *inValue;

		printf("delta1: Trying to release\n");
		release(inValue);
		printf("delta1: release OK\n");

		printf("delta1: Trying to malloc\n");
		outValue = createMalloc(sizeof(int));
		printf("delta1: malloc OK\n");

		if (outValue == NULL)
			printf("delta1 outValue is NULL\n");

		*outValue = value;

		printf("delta1: Trying to put\n");
		put(out, outValue);
		printf("delta1: put OK\n");
	}
}

void prefix(GUID in, GUID out, int value)
{
	unsigned int* outValue;

	printf("Prefix: Trying to malloc\n");
	outValue = createMalloc(sizeof(int));
	printf("Prefix: malloc OK, ls: %d\n", (unsigned int)outValue);

	if (outValue == NULL)
		printf("prefix outValue is NULL\n");

	*outValue = value;
	printf("Prefix: Trying to put\n");
	put(out, outValue);
	printf("Prefix: put OK\n");

	printf("Prefix: Trying to delta1\n");
	delta1(in, out);
	printf("Prefix: delta1 OK\n");
}


int main(int argc, char** argv) {
	
	initialize();

	unsigned long size;
	int processcount;

	//Basic setup, get a unique id for each participating process
	int * tmp = acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	pid = (*tmp)++;
	release(tmp);

	acquireBarrier(BARRIER_GUID);

	//Get the number of participating processes
	tmp = acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	processcount = *tmp;
	release(tmp);

	acquireBarrier(BARRIER_GUID);

	GUID readerChannel = CHANNEL_START_GUID + (pid % processcount);
	GUID writerChannel = CHANNEL_START_GUID + ((pid + 1) % processcount);

	printf("Ready to serve\n");

	if (pid == 0)
	{
		sleep(5);
		delta2(readerChannel, writerChannel, DELTA_CHANNEL);
	}
	else if (pid == 1)
		prefix(readerChannel, writerChannel, 1);
	else
		delta1(readerChannel, writerChannel);
	
	terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


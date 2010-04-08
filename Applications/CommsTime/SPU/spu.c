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
		inValue = get(in, &size);
		if (inValue == NULL)
			printf("delta2 inValue is NULL\n");
		value = *inValue;
		release(inValue);

		outValue = createMalloc(sizeof(int));
		if (outValue == NULL)
			printf("delta2 outValue #1 is NULL\n");
		*outValue = value;
		put(outA, outValue);

		outValue = createMalloc(sizeof(int));
		if (outValue == NULL)
			printf("delta2 outValue #2 is NULL\n");
		*outValue = value;
		put(outB, outValue);
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
		inValue = get(in, &size);
		if (inValue == NULL)
			printf("delta1 inValue is NULL\n");
		value = *inValue;
		release(inValue);

		outValue = createMalloc(sizeof(int));

		if (outValue == NULL)
			printf("delta1 outValue is NULL\n");

		*outValue = value;
		put(out, outValue);
	}
}

void prefix(GUID in, GUID out, int value)
{
	unsigned int* outValue;

	outValue = createMalloc(sizeof(int));

	if (outValue == NULL)
		printf("prefix outValue is NULL\n");

	*outValue = value;
	put(out, outValue);

	delta1(in, out);
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

	if (pid == 0)
		delta2(readerChannel, writerChannel, DELTA_CHANNEL);
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


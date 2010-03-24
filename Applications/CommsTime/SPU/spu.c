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
	unsigned long* inValue;
	unsigned long* outValue;

	unsigned long value;
	unsigned long size;

	while(1)
	{
		inValue = acquire(in, &size, ACQUIRE_MODE_DELETE);
		if (inValue == NULL)
			printf("delta2 inValue is NULL\n");
		value = *inValue;
		release(inValue);

		outValue = create(outA, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("delta2 outValue #1 is NULL\n");
		*outValue = value;
		release(outValue);

		outValue = create(outB, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("delta2 outValue #2 is NULL\n");
		*outValue = value;
		release(outValue);
	}
}

void delta1(GUID in, GUID out)
{
	unsigned long* inValue;
	unsigned long* outValue;

	unsigned long value;
	unsigned long size;

	while(1)
	{
		inValue = acquire(in, &size, ACQUIRE_MODE_DELETE);
		if (inValue == NULL)
			printf("delta1 inValue is NULL\n");
		value = *inValue;
		release(inValue);

		outValue = create(out, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("delta1 outValue is NULL\n");
		*outValue = value;
		release(outValue);
	}
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

	//The initial processor starts the sequence
	if (pid == 0)
	{
		release(create(readerChannel, sizeof(unsigned int), CREATE_MODE_BLOCKING));
	}

	if (pid == 0)
		delta2(readerChannel, writerChannel, DELTA_CHANNEL);
	else
		delta1(readerChannel, writerChannel);
	
	terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


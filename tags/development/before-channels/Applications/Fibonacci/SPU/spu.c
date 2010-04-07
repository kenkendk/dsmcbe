#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include "../PPU/guids.h"
#include <time.h>

//We need the counter on the heap so the threads share it
//static int counter = 0;

void TEST();
void delta2(GUID in, GUID outA, GUID outB, char* name);
void plus(GUID inA, GUID inB, GUID out, char* name);
void tail(GUID in, GUID out, char* name);
void prefix(GUID in, GUID out, unsigned long prefix, char* name);

int main(int argc, char** argv) {
	
	printf("SPU is initializing\n");
	initialize();

	unsigned long size;
	int pid;
	int processcount;
	int* tmp;

	printf("SPU is initialized, waiting for process counter\n");

	//Basic setup, get a unique id for each participating process
	tmp = acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	pid = (*tmp)++;
	release(tmp);

	printf("SPU %d is waiting for barrier\n", pid);

	acquireBarrier(BARRIER_GUID);

	//Get the number of participating processes
	tmp = acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	processcount = *tmp;
	release(tmp);

	acquireBarrier(BARRIER_GUID);

	printf("SPU %d is now running\n", pid);

	if (pid == 0)
	{
		delta2(CHANNEL_PREFIX_DELTA2, CHANNEL_OUT, CHANNEL_DELTA2_DELTA2, "* 1 *");
	}
	else if (pid == 1)
	{
		delta2(CHANNEL_DELTA2_DELTA2, CHANNEL_DELTA2_TAIL, CHANNEL_DELTA2_PLUS, "* 2 *");
	}
	else if (pid == 2)
	{
		tail(CHANNEL_DELTA2_TAIL, CHANNEL_TAIL_PLUS, "* 3 *");
	}
	else if (pid == 3)
	{
		plus(CHANNEL_DELTA2_PLUS, CHANNEL_TAIL_PLUS, CHANNEL_PLUS_PREFIX, "* 4 *");
	}
	else if (pid == 4)
	{
		prefix(CHANNEL_PLUS_PREFIX, CHANNEL_PREFIX_PREFIX, 1, "* 5 *");
	}
	else if (pid == 5)
	{
		prefix(CHANNEL_PREFIX_PREFIX, CHANNEL_PREFIX_DELTA2, 0, "* 6 *");
	}


	printf("SPU %d is terminating\n", pid);

	terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}

void delta2(GUID in, GUID outA, GUID outB, char* name)
{
	unsigned long* inValue;
	unsigned long* outValue;

	unsigned long value;
	unsigned long size;

	while(1)
	{
		inValue = acquire(in, &size, ACQUIRE_MODE_DELETE);
		if (inValue == NULL)
			printf("Plus inValue is NULL\n");
		value = *inValue;
		release(inValue);

		outValue = create(outA, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("%s - Plus outValue #1 is NULL\n", name);
		*outValue = value;
		release(outValue);

		outValue = create(outB, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("%s - Plus outValue #2 is NULL\n", name);
		*outValue = value;
		release(outValue);

		//printf("Delta2 %s - Value %d\n", name, value);
	}
}

void plus(GUID inA, GUID inB, GUID out, char* name)
{
	unsigned long* inAValue;
	unsigned long* inBValue;
	unsigned long* outValue;

	unsigned long valueA;
	unsigned long valueB;
	unsigned long size;

	while(1)
	{
		inAValue = acquire(inA, &size, ACQUIRE_MODE_DELETE);
		if (inAValue == NULL)
			printf("%s - Plus inAValue is NULL\n", name);
		valueA = *inAValue;
		release(inAValue);

		inBValue = acquire(inB, &size, ACQUIRE_MODE_DELETE);
		if (inBValue == NULL)
			printf("%s - Plus inBValue is NULL\n", name);
		valueB = *inBValue;
		release(inBValue);

		outValue = create(out, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("%s - Plus outValue is NULL\n", name);
		*outValue = valueA + valueB;
		release(outValue);

		//printf("Plus %s - Value #1: %d, Value #2: %d\n", name, valueA, valueB);
	}
}

void tail(GUID in, GUID out, char* name)
{
	unsigned long* inValue;
	unsigned long* outValue;

	unsigned long value;
	unsigned long size;

	// Haps!
	inValue = acquire(in, &size, ACQUIRE_MODE_DELETE);
	if (inValue == NULL)
		printf("%s - Tail inValue is NULL\n", name);
	release (inValue);

	while(1)
	{
		inValue = acquire(in, &size, ACQUIRE_MODE_DELETE);
		if (inValue == NULL)
			printf("%s - Tail inValue is NULL\n", name);
		value = *inValue;
		release(inValue);

		outValue = create(out, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("%s - Tail outValue is NULL\n", name);
		*outValue = value;
		release(outValue);

		//printf("Tail %s: Value: %d\n", name, value);
	}
}

void prefix(GUID in, GUID out, unsigned long prefix, char* name)
{
	unsigned long* inValue;
	unsigned long* outValue;

	unsigned long value;
	unsigned long size;

	outValue = create(out, sizeof(int), CREATE_MODE_BLOCKING);
	if (outValue == NULL)
		printf("%s - Prefix first outValue is NULL\n", name);
	*outValue = prefix;
	release(outValue);

	//printf("Prefix %s - Value %d\n", name, value);

	while(1)
	{
		inValue = acquire(in, &size, ACQUIRE_MODE_DELETE);
		if (inValue == NULL)
			printf("%s - Prefix inValue is NULL\n", name);
		value = *inValue;
		release(inValue);

		outValue = create(out, sizeof(int), CREATE_MODE_BLOCKING);
		if (outValue == NULL)
			printf("%s - Prefix outValue is NULL\n", name);
		*outValue = value;
		release(outValue);

		//printf("Prefix %s - Value %d\n", name, value);
	}
}

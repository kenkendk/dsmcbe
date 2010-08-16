#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include "../PPU/guids.h"
#include <time.h>
#include "csp_commons.h"
#include <dsmcbe_csp.h>

void* plus_callback(void* a, size_t size_a, void* b, size_t size_b)
{
	size_a = size_b = 0; //Remove compiler warning

	(*(unsigned int*)a) = *((unsigned int*)a) + *((unsigned int*)b);
	return a;
}

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId) {
	
	unsigned long size;
	int pid;
	int processcount;
	int* tmp;

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	printf("SPU is initialized, waiting for process counter\n");

	//TODO: Rewrite this to use csp channels

	//Basic setup, get a unique id for each participating process
	tmp = dsmcbe_acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	pid = (*tmp)++;
	dsmcbe_release(tmp);

	printf("SPU %d is waiting for barrier\n", pid);

	dsmcbe_acquireBarrier(BARRIER_GUID);

	//Get the number of participating processes
	tmp = dsmcbe_acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	processcount = *tmp;
	dsmcbe_release(tmp);

	dsmcbe_acquireBarrier(BARRIER_GUID);

	printf("SPU %d is now running\n", pid);

	if (pid == 0)
	{
		delta2(CHANNEL_PREFIX_DELTA2, CHANNEL_OUT, CHANNEL_DELTA2_DELTA2);
	}
	else if (pid == 1)
	{
		delta2(CHANNEL_DELTA2_DELTA2, CHANNEL_DELTA2_TAIL, CHANNEL_DELTA2_PLUS);
	}
	else if (pid == 2)
	{
		tail(CHANNEL_DELTA2_TAIL, CHANNEL_TAIL_PLUS);
	}
	else if (pid == 3)
	{
		combine(CHANNEL_DELTA2_PLUS, CHANNEL_TAIL_PLUS, CHANNEL_PLUS_PREFIX, &plus_callback);
	}
	else if (pid == 4)
	{
		unsigned int* inputdata;
		CSP_SAFE_CALL("creating prefix value 1", dsmcbe_csp_item_create((void**)&inputdata, sizeof(unsigned int)));
		*inputdata = 1;
		prefix(CHANNEL_PLUS_PREFIX, CHANNEL_PREFIX_PREFIX, inputdata);
	}
	else if (pid == 5)
	{
		unsigned int* inputdata;
		CSP_SAFE_CALL("creating prefix value 0", dsmcbe_csp_item_create((void**)&inputdata, sizeof(unsigned int)));
		*inputdata = 0;
		prefix(CHANNEL_PREFIX_PREFIX, CHANNEL_PREFIX_DELTA2, inputdata);
	}


	printf("SPU %d is terminating\n", pid);

	return 0;
}

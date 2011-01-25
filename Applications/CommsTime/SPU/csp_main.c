//#define DEBUG

#ifndef COMPILE_FOR_PPE
#define SMART_ID_ASSIGNMENT
#endif

#include <dsmcbe_csp.h>
#include <SPUThreads.h>
#include "csp_commons.h"
#include "../PPU/guids.h"
#include <stdlib.h>
#include <limits.h>
#include <debug.h>

int unittest(unsigned int pid)
{
	//Test the skip channel feature
	GUID testchans[] = {BUFFERED_CHANNEL_START + pid, CSP_SKIP_GUARD};
	GUID selected_chan = 777;
	void* test_data;

	CSP_SAFE_CALL("test skip non-existing-channel", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
	if (selected_chan != CSP_SKIP_GUARD)
		printf("**** ERROR: ALT SKIP read is broken :(\n");

	CSP_SAFE_CALL("test create buffered channel", dsmcbe_csp_channel_create(BUFFERED_CHANNEL_START + pid, 1, CSP_CHANNEL_TYPE_ONE2ONE));

	CSP_SAFE_CALL("test skip existing-channel", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
	if (selected_chan != CSP_SKIP_GUARD)
		printf("**** ERROR: ALT SKIP read is broken :(\n");

	CSP_SAFE_CALL("test createitem", dsmcbe_csp_item_create(&test_data, 4));
	CSP_SAFE_CALL("test write buffered", dsmcbe_csp_channel_write(BUFFERED_CHANNEL_START + pid, test_data));

	CSP_SAFE_CALL("test createitem2", dsmcbe_csp_item_create(&test_data, 4));
	CSP_SAFE_CALL("test alt-write buffered", dsmcbe_csp_channel_write_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, test_data, &selected_chan));
	if (selected_chan != CSP_SKIP_GUARD)
		printf("**** ERROR: ALT SKIP write is broken :(\n");
	CSP_SAFE_CALL("test freeitem2", dsmcbe_csp_item_free(test_data));

	CSP_SAFE_CALL("test alt-read buffered", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
	if (selected_chan != BUFFERED_CHANNEL_START + pid)
		printf("**** ERROR: ALT read is broken :(\n");

	CSP_SAFE_CALL("test alt-write buffered", dsmcbe_csp_channel_write_alt(CSP_ALT_MODE_PRIORITY, testchans, 1, test_data, &selected_chan));
	if (selected_chan != BUFFERED_CHANNEL_START + pid)
		printf("**** ERROR: ALT write is broken :(\n");
	CSP_SAFE_CALL("test alt-read buffered", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 1, &selected_chan, NULL, &test_data));
	if (selected_chan != BUFFERED_CHANNEL_START + pid)
		printf("**** ERROR: ALT read is broken :(\n");

	CSP_SAFE_CALL("poison test channel", dsmcbe_csp_channel_poison(BUFFERED_CHANNEL_START + pid));

	if (dsmcbe_csp_channel_write_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, test_data, &selected_chan) != CSP_CALL_POISON)
		printf("**** ERROR: ALT poison read is broken :(\n");

	CSP_SAFE_CALL("test freeitem", dsmcbe_csp_item_free(test_data));

	if (dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data) != CSP_CALL_POISON)
		printf("**** ERROR: ALT poison write is broken :(\n");

	return CSP_CALL_SUCCESS;
}

#ifdef SMART_ID_ASSIGNMENT
unsigned int IsFetchingData = 0;
unsigned int NextProcId = UINT_MAX;
unsigned int processcount;
#endif


//This is the CommsTime CSP main function
int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	unsigned int pid;
#ifndef SMART_ID_ASSIGNMENT
	unsigned int NextProcId = UINT_MAX;
	unsigned int processcount;
#endif


	unsigned int* tmp;

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

#ifdef SMART_ID_ASSIGNMENT
	//This shared variable works as a barrier
	while (IsFetchingData == 1)
		dsmcbe_thread_yield();

	//The first thread will fetch a setup for all threads
	if (NextProcId == UINT_MAX)
	{
		IsFetchingData = 1;

#endif

		CSP_SAFE_CALL("read processid", dsmcbe_csp_channel_read(PROCESS_COUNTER_GUID, NULL, (void**)&tmp));

		NextProcId = tmp[0];
		//printf(WHERESTR "Spe %llu, thread %u read setup and got ProcId %u\n",WHEREARG, speid, threadId, NextProcId);

#ifdef COMPILE_FOR_PPE
		tmp[0] += 1;
#else
		tmp[0] += dsmcbe_thread_get_count();
#endif
		processcount = tmp[1];

		if (tmp[0] != tmp[1]) {
			CSP_SAFE_CALL("write processid", dsmcbe_csp_channel_write(PROCESS_COUNTER_GUID, tmp));
		} else {
			CSP_SAFE_CALL("free processid", dsmcbe_csp_item_free(tmp));
		}

#ifdef SMART_ID_ASSIGNMENT
		IsFetchingData = 0;
	}
#endif

	pid = NextProcId++;

	//printf(WHERESTR "Spe %llu, thread %u read setup and got ProcId %u, with %d processes\n",WHEREARG, speid, threadId, pid, processcount);

	unittest(pid);

	//printf(WHERESTR "Spe %llu, thread %u with ProcId %u completed the unittest\n",WHEREARG, speid, threadId, pid);

	GUID readerChannel = CHANNEL_START_GUID + (pid % processcount);
	GUID writerChannel = CHANNEL_START_GUID + ((pid + 1) % processcount);

	CSP_SAFE_CALL("create start channel", dsmcbe_csp_channel_create(readerChannel, 0, CSP_CHANNEL_TYPE_ONE2ONE_SIMPLE));

	//printf(WHERESTR "Spe %llu, thread %u with ProcId %u is reading from %d and writing to %d\n",WHEREARG, speid, threadId, pid, readerChannel, writerChannel);


	int call_result;

	if (pid == 0)
		call_result = delta2(readerChannel, DELTA_CHANNEL, writerChannel);
	else if (pid == 1)
	{
		void* data;
		CSP_SAFE_CALL("create prefix value", dsmcbe_csp_item_create(&data, DATA_BLOCK_SIZE));
		call_result = prefix(readerChannel, writerChannel, data);
	}
	else
		call_result = delta1(readerChannel, writerChannel);

	if (call_result != CSP_CALL_POISON)
		printf("**** ERROR: poison is broken :(\n");

	CSP_SAFE_CALL("poison start channel", dsmcbe_csp_channel_poison(writerChannel));

	return CSP_CALL_SUCCESS;
}

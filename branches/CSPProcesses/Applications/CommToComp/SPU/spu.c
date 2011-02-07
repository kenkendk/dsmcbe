#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include <time.h>
#include <limits.h>
#include "../PPU/guids.h"

#include <dsmcbe_spu_internal.h>

//Shared variables for all SPE threads
unsigned int IsFetchingData = 0;
unsigned int NextProcId = UINT_MAX;
unsigned int Shared_procCount = 0;
unsigned int Shared_workCount = 0;
unsigned int Shared_dataSize = 0;

#define TICK_EMITTER (0)

//Define this to print out progress to detect stalls
//#define TRACE_PROGRESS

#ifdef TRACE_PROGRESS
	#define TRACE_PROGRESS_DATA(name, channelid) printf(WHERESTR "Spe %llu (%u), thread %u %s %d (%u), channel %u %s\n",WHEREARG, speid, procId, threadId, name, ((unsigned int*)data)[1], ((unsigned int)data), channelid, ((unsigned int*)data)[2] == UINT_MAX ? "-TICK-" : "");
	#define TRACE_PROGRESS_NODATA(name, channelid) printf(WHERESTR "Spe %llu (%u), thread %u %s, channel %u\n",WHEREARG, speid, procId, threadId, name, channelid);
#else
	#define TRACE_PROGRESS_DATA(name, channelid)
	#define TRACE_PROGRESS_NODATA(name, channelid)
#endif


int copyItem(void* dataItem, size_t size, GUID targetChannel)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

	void* copy;
	int res;

	CSP_SAFE_CALL("create copy", dsmcbe_csp_item_create(&copy, size));
	memcpy(copy, dataItem, size);

	res = dsmcbe_csp_channel_write(targetChannel, copy);
	if (res != CSP_CALL_SUCCESS)
		dsmcbe_csp_item_free(copy);

	return res;
}

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId) {
	
	unsigned int i;
	float a;

	unsigned int procId;
	unsigned int procCount;
	unsigned int workCount;
	unsigned int dataSize;

	GUID readChannel;
	GUID writeChannel;

	void* data;

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

#ifdef SMART_ID_ASSIGNMENT
	//This shared variable works as a barrier
	while (IsFetchingData == 1)
		dsmcbe_thread_yield();

	//The first thread will fetch a setup for all threads
	if (NextProcId == UINT_MAX)
	{
		IsFetchingData = 1;

#endif
		//printf(WHERESTR "Spe %llu, thread %u is reading setup\n",WHEREARG, speid, threadId);

		SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

		CSP_SAFE_CALL("get setup", dsmcbe_csp_channel_read(SETUP_CHANNEL, NULL, &data));
		NextProcId = ((int*)data)[0];
		Shared_procCount = ((int*)data)[1];
		Shared_workCount = ((int*)data)[2];
		Shared_dataSize = ((int*)data)[3];

#ifdef SMART_ID_ASSIGNMENT
		CSP_SAFE_CALL("free setup", dsmcbe_csp_item_free(data));
		data = NULL;
#endif
		//printf(WHERESTR "Spe %llu, thread %u read setup and got ProcId %u\n",WHEREARG, speid, threadId, NextProcId);

#ifdef SMART_ID_ASSIGNMENT
		IsFetchingData = 0;
	}
#endif

	SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

	//Each thread will copy the shared data into the non-shared variables
	procId = NextProcId;
	procCount = Shared_procCount;
	workCount = Shared_workCount;
	dataSize = Shared_dataSize;

#ifndef SMART_ID_ASSIGNMENT
		CSP_SAFE_CALL("free setup", dsmcbe_csp_item_free(data));
		data = NULL;
#endif

	//The next thread gets a fresh ID
	NextProcId++;

	readChannel = (procId % procCount) + RING_CHANNEL_BASE;
	writeChannel = ((procId + 1) % procCount) + RING_CHANNEL_BASE;

	//printf(WHERESTR "Spe %llu (%u), thread %u is reading from %u and writing to %u\n",WHEREARG, speid, procId, threadId, readChannel, writeChannel);

	CSP_SAFE_CALL("create work channel", dsmcbe_csp_channel_create(readChannel, 1, CSP_CHANNEL_TYPE_ONE2ONE_SIMPLE));

	SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

	unsigned int skipFirstWrite = 0;
	unsigned long oprcount = 0;



#ifdef SINGLE_PACKAGE
	unsigned int ids[] = SINGLE_PACKAGE;
	unsigned int found = 0;
	int idcounter = 0;

	while(ids[idcounter] != UINT_MAX)
		if (procId == ids[idcounter++])
		{
			found = 1;
			break;
		}

	if (found)
	{
		skipFirstWrite = 0;
		CSP_SAFE_CALL("create comm block", dsmcbe_csp_item_create(&data, dataSize));
		((unsigned int*)data)[1] = procId;
	}
	else
	{
		skipFirstWrite = 1;
	}
#else
	CSP_SAFE_CALL("create comm block", dsmcbe_csp_item_create(&data, dataSize));
	((unsigned int*)data)[1] = procId;
#endif

	//We mark the first package so we know when it returns
	if (procId == TICK_EMITTER)
		((unsigned int*)data)[2] = UINT_MAX;
	else
		((unsigned int*)data)[2] = 0;

	while(1)
	{
		SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

		if (!skipFirstWrite)
		{
			SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

			TRACE_PROGRESS_DATA("is writing", writeChannel);
			if(dsmcbe_csp_channel_write(writeChannel, data) != CSP_CALL_SUCCESS)
			{
				//printf(WHERESTR "Spe %llu (%u), thread %u is reading from %u and writing to %u, is free'in item\n",WHEREARG, speid, procId, threadId, readChannel, writeChannel);
				CSP_SAFE_CALL("free copy", dsmcbe_csp_item_free(data));
				//printf(WHERESTR "Spe %llu (%u), thread %u is reading from %u and writing to %u, is terminating\n",WHEREARG, speid, procId, threadId, readChannel, writeChannel);

				break;
			}
			TRACE_PROGRESS_NODATA("wrote", writeChannel);

			SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);
		}
		else
			skipFirstWrite = 0;

		TRACE_PROGRESS_NODATA("is reading", readChannel);
		if(dsmcbe_csp_channel_read(readChannel, NULL, &data) != CSP_CALL_SUCCESS) {
			CSP_SAFE_CALL("forward poison", dsmcbe_csp_channel_poison(writeChannel));
			break;
		}
		TRACE_PROGRESS_DATA("read", readChannel);

		/*if (((unsigned int*)data)[1] != (procId == 0 ? procCount - 1 : procId - 1))
			printf(WHERESTR "Spe %llu (%u), thread %u is reading from %u and writing to %u, got item last touched by %d\n",WHEREARG, speid, procId, threadId, readChannel, writeChannel, ((unsigned int*)data)[1]);*/

		SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

		if (procId == TICK_EMITTER && ((unsigned int*)data)[2] == UINT_MAX)
		{
			SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

			if (oprcount >= SPE_REPETITIONS)
			{
				TRACE_PROGRESS_DATA("is writing delta copy", DELTA_CHANNEL);
				oprcount = 0;
				if (copyItem(data, dataSize, DELTA_CHANNEL) != CSP_CALL_SUCCESS)
				{
					//We got poison from PPU
					//printf(WHERESTR "Spe %llu (%u), thread %u is reading from %u and writing to %u, is free'in item\n",WHEREARG, speid, procId, threadId, readChannel, writeChannel);
					CSP_SAFE_CALL("free copy", dsmcbe_csp_item_free(data));
					//printf(WHERESTR "Spe %llu (%u), thread %u is reading from %u and writing to %u, is terminating\n",WHEREARG, speid, procId, threadId, readChannel, writeChannel);

					CSP_SAFE_CALL("poison write", dsmcbe_csp_channel_poison(writeChannel));

					while(dsmcbe_csp_channel_read(readChannel, NULL, &data) == CSP_CALL_SUCCESS) {
						CSP_SAFE_CALL("free extra", dsmcbe_csp_item_free(data));
					}

					break;
				}

				TRACE_PROGRESS_NODATA("wrote delta copy", DELTA_CHANNEL);
			}
			else
				oprcount++;
		}

		SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);

		//Simulate some amount of work
		a = 1.1;
		for(i = 0; i < workCount; i++)
			a = (a + i) * 0.1;

		((unsigned int*)data)[0] = (unsigned int)a;
		((unsigned int*)data)[1] = procId;

		SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);
	}

	//printf(WHERESTR "Spe %llu (%u), thread %u is terminating\n",WHEREARG, speid, procId, threadId);

	SET_CURRENT_FUNCTION(FILE_DSMCBE_USER);
	return 0;
}


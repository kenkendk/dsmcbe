/*
 * Implementation file for CSP functions on the SPU
 */

#include <dsmcbe_csp.h>
#include <datapackages.h>
#include <SPUEventHandler_extrapackages.h>
#include <dsmcbe_spu_internal.h>
#include <limits.h>
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include "SPUThreads.h"

#ifdef SPU_STOP_AND_WAIT
//This is from syscall.h
int __send_to_ppe(unsigned int signalcode, unsigned int opcode, void *data);

#define STOP_AND_WAIT __send_to_ppe(0x2100 | (CSP_STOP_FUNCTION_CODE & 0xff), 1, (void*)2);
#else
#define STOP_AND_WAIT
#endif

//HACK: Inject the ringbuffer code here
#include "../../PPU/source files/ringbuffer.c"

int dsmcbe_csp_item_create(void** data, size_t size)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(size);

	*data = spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}


int dsmcbe_csp_item_free(void* data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_ITEM_FREE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX((unsigned int)data);

	spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_write(GUID channelid, void* data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_WRITE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	spu_dsmcbe_pendingRequests[nextId].channelId = channelid;
	struct spu_dsmcbe_directChannelStruct* channel = dsmcbe_csp_findDirectChannelIndex(channelid);
	if (channel != NULL)
	{
		//printf(WHERESTR "Handling a direct write request with id %d, channelId: %d\n", WHEREARG, nextId, channelid);
		//TODO: We need to store the size?
		dsmcbe_csp_handleDirectWriteRequest(channel, nextId, data, 0);
	}
	else
	{
		//printf(WHERESTR "Handling a normal write request with id %d, channelId: %d\n", WHEREARG, nextId, channelid);

		SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
		SPU_WRITE_OUT_MBOX(nextId);
		SPU_WRITE_OUT_MBOX(channelid);
		SPU_WRITE_OUT_MBOX((unsigned int)data);

		STOP_AND_WAIT
	}

	spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_WRITE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_read(GUID channelid, size_t* size, void** data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_READ_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	spu_dsmcbe_pendingRequests[nextId].channelId = channelid;
	struct spu_dsmcbe_directChannelStruct* channel = dsmcbe_csp_findDirectChannelIndex(channelid);
	if (channel != NULL)
	{
		//printf(WHERESTR "Handling a direct read request with id %d, channelId: %d\n", WHEREARG, nextId, channelid);
		dsmcbe_csp_handleDirectReadRequest(channel, nextId);
	}
	else
	{
		//printf(WHERESTR "Handling a normal read request with id %d, channelId: %d\n", WHEREARG, nextId, channelid);
		SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
		SPU_WRITE_OUT_MBOX(nextId);
		SPU_WRITE_OUT_MBOX(channelid);
		STOP_AND_WAIT
	}

	*data = spu_dsmcbe_endAsync(nextId, size);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_READ_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_read_alt(unsigned int mode, GUID* channels, size_t channelcount, GUID* channelid, size_t* size, void** data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(mode);
	SPU_WRITE_OUT_MBOX((unsigned int)channels);
	SPU_WRITE_OUT_MBOX(channelcount);
	SPU_WRITE_OUT_MBOX((unsigned int)channelid);

	STOP_AND_WAIT

	*data = spu_dsmcbe_endAsync(nextId, size);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE)
	{
		if (channelid != NULL)
			*channelid = CSP_SKIP_GUARD;

		return CSP_CALL_SUCCESS;
	}
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_write_alt(unsigned int mode, GUID* channels, size_t channelcount, void* data, GUID* channelid)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(mode);
	SPU_WRITE_OUT_MBOX((unsigned int)channels);
	SPU_WRITE_OUT_MBOX(channelcount);
	SPU_WRITE_OUT_MBOX((unsigned int)data);

	STOP_AND_WAIT

	spu_dsmcbe_endAsync(nextId, (unsigned long*)channelid);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE)
	{
		if (channelid != NULL)
			*channelid = CSP_SKIP_GUARD;

		return CSP_CALL_SUCCESS;
	}
	else
		return CSP_CALL_ERROR;
}

void dsmcbe_csp_channel_poison_internal(GUID channelid)
{
	struct spu_dsmcbe_directChannelStruct* channel = dsmcbe_csp_findDirectChannelIndex(channelid);
	if (channel != NULL)
	{
		if (channel->poisonState == 0)
		{
			//printf(WHERESTR "Poison to channel %d\n", WHEREARG, channelid);

			channel->poisonState = 1;
			if (channel->readerId != UINT_MAX)
			{
				//printf(WHERESTR "Poison to channel %d for reader with requestId: %d\n", WHEREARG, channelid, channel->readerId);
				spu_dsmcbe_pendingRequests[channel->readerId].responseCode = PACKAGE_CSP_CHANNEL_POISONED_RESPONSE;
				dsmcbe_thread_set_ready_by_requestId(channel->readerId);
			}

			//If we are all filled, the last entry gets the poison, as that is not buffered
			if (channel->pendingWrites->count == channel->pendingWrites->size)
			{
				unsigned int writerId = (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, channel->pendingWrites->count - SPE_PENDING_WRITE_SIZE);
				spu_dsmcbe_pendingRequests[writerId].responseCode = PACKAGE_CSP_CHANNEL_POISONED_RESPONSE;
				dsmcbe_thread_set_ready_by_requestId(writerId);

				//printf(WHERESTR "Poison to channel %d for writer with requestId: %d\n", WHEREARG, channelid, writerId);

				//HACK: Remove the last 3 elements from the buffer
				channel->pendingWrites->count -= SPE_PENDING_WRITE_SIZE;
				//TODO: Also fix tail?
			}

			channel->readerId = UINT_MAX;
		}
		/*else
			printf(WHERESTR "Re-poison channel %d\n", WHEREARG, channelid);*/

	}
	else
		REPORT_ERROR2("Got poison for channel %d, but that was not internal?", channelid);
}

int dsmcbe_csp_channel_poison(GUID channelid)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_POISON_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	struct spu_dsmcbe_directChannelStruct* channel = dsmcbe_csp_findDirectChannelIndex(channelid);
	if (channel != NULL)
	{
		if (channel->poisonState == 0)
		{
			dsmcbe_csp_channel_poison_internal(channelid);

			spu_dsmcbe_pendingRequests[nextId].responseCode = PACKAGE_CSP_CHANNEL_POISON_RESPONSE;
			dsmcbe_thread_set_ready_by_requestId(nextId);
		}
		else
		{
			spu_dsmcbe_pendingRequests[nextId].responseCode = PACKAGE_NACK;
			dsmcbe_thread_set_ready_by_requestId(nextId);
		}
	}
	else
	{
		SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
		SPU_WRITE_OUT_MBOX(nextId);
		SPU_WRITE_OUT_MBOX(channelid);

		STOP_AND_WAIT
	}

	spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISON_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_create(GUID channelid, unsigned int buffersize, unsigned int type)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_CREATE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(channelid);
	SPU_WRITE_OUT_MBOX(buffersize);
	SPU_WRITE_OUT_MBOX(type);

	spu_dsmcbe_endAsync(nextId, NULL);

	STOP_AND_WAIT

	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_CREATE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}

//Searches for a direct channel with the given id, returns the direct channel object or NULL
struct spu_dsmcbe_directChannelStruct* dsmcbe_csp_findDirectChannelIndex(GUID channelId)
{
	int index = channelId % MAX_DIRECT_CHANNELS;
	size_t i;

	for(i = 0; i < MAX_DIRECT_CHANNELS; i++)
		if (spu_dsmcbe_directChannels[(i + index) % MAX_DIRECT_CHANNELS].id == 0)
			return NULL;
		else if (spu_dsmcbe_directChannels[(i + index) % MAX_DIRECT_CHANNELS].id == channelId)
			return &spu_dsmcbe_directChannels[(i + index) % MAX_DIRECT_CHANNELS];

	return NULL;
}

int dsmcbe_csp_respondToDirectReader(struct spu_dsmcbe_directChannelStruct* channel, unsigned int readerId, void* data, unsigned int size)
{
	//printf(WHERESTR "Responding to readReq: %u, writerId: %u, data: %u, size: %u, rb->count: %u\n", WHEREARG, readerId, (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, 0), (unsigned int)data, size, channel->pendingWrites->count);

	spu_dsmcbe_pendingRequests[readerId].responseCode = PACKAGE_CSP_CHANNEL_READ_RESPONSE;
	spu_dsmcbe_pendingRequests[readerId].pointer = data;
	spu_dsmcbe_pendingRequests[readerId].size = size;

	dsmcbe_thread_set_ready_by_requestId(readerId);
	channel->readerId = UINT_MAX;

	return CSP_CALL_SUCCESS;
}

int dsmcbe_csp_respondToDirectWriter(unsigned int writerId)
{
	//printf(WHERESTR "Responding to writeReq, writerId: %u\n", WHEREARG, writerId);

	spu_dsmcbe_pendingRequests[writerId].responseCode = PACKAGE_CSP_CHANNEL_WRITE_RESPONSE;
	spu_dsmcbe_pendingRequests[writerId].pointer = NULL;
	spu_dsmcbe_pendingRequests[writerId].size = 0;
	dsmcbe_thread_set_ready_by_requestId(writerId);

	return CSP_CALL_SUCCESS;
}

//Handles a request for read on a local direct channel
void dsmcbe_csp_handleDirectReadRequest(struct spu_dsmcbe_directChannelStruct* channel, unsigned int requestId)
{
	if (channel->readerId != UINT_MAX)
	{
		//Wrong use of ONE2ONE channel
		spu_dsmcbe_pendingRequests[requestId].responseCode = PACKAGE_NACK;
		dsmcbe_thread_set_ready_by_requestId(requestId);
		return;
	}

	if (channel->poisonState != 0 && channel->pendingWrites->count == 0)
	{
		//printf(WHERESTR "Poison on read from channel %d for reader with requestId: %d\n", WHEREARG, channel->id, requestId);

		spu_dsmcbe_pendingRequests[requestId].responseCode = PACKAGE_CSP_CHANNEL_POISONED_RESPONSE;
		dsmcbe_thread_set_ready_by_requestId(requestId);
	}
	else if (channel->pendingWrites->count != 0)
	{
		//printf(WHERESTR "Got readReq with id %d and writer was ready, channel: %d\n", WHEREARG, requestId, channel->id);

		void* data = dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, 1);
		unsigned int size = (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, 2);

		dsmcbe_csp_respondToDirectReader(channel, requestId, data, size);

		//If the channel is filled, we remove an item, so the last one can be processed now
		if (channel->pendingWrites->count == channel->pendingWrites->size)
		{
			unsigned int writerId = (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, channel->pendingWrites->count - SPE_PENDING_WRITE_SIZE);

			//printf(WHERESTR "Responding to writeReq, writerId: %u, data: %u, size: %u, rb->count: %u\n", WHEREARG, writerId, (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, channel->pendingWrites->count - (SPE_PENDING_WRITE_SIZE + 1)), (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, channel->pendingWrites->count - (SPE_PENDING_WRITE_SIZE + 2)), channel->pendingWrites->count);

			dsmcbe_csp_respondToDirectWriter(writerId);
		}

		//Now pop the request
		dsmcbe_ringbuffer_pop(channel->pendingWrites);
		dsmcbe_ringbuffer_pop(channel->pendingWrites);
		dsmcbe_ringbuffer_pop(channel->pendingWrites);

		//printf(WHERESTR "After responding to readReq, channel count is %d, channel: %d\n", WHEREARG, channel->pendingWrites->count, channel->id);
	}
	else
	{
		//printf(WHERESTR "Got readReq with id %d but no writer was ready, channel: %d\n", WHEREARG, requestId, channel->id);

		channel->readerId = requestId;
	}

	//We perform a thread switch here to make the operation more like the non-optimized version
	dsmcbe_thread_yield_ready();
}

//Handles a request for write on a local direct channel
void dsmcbe_csp_handleDirectWriteRequest(struct spu_dsmcbe_directChannelStruct* channel, unsigned int requestId, void* data, unsigned int size)
{
	if (channel->pendingWrites->count == channel->pendingWrites->size)
	{
		//Wrong use of ONE2ONE channel
		spu_dsmcbe_pendingRequests[requestId].responseCode = PACKAGE_NACK;
		dsmcbe_thread_set_ready_by_requestId(requestId);
		return;
	}

	if (channel->poisonState != 0)
	{
		//printf(WHERESTR "Poison on write to channel %d for writer with requestId: %d\n", WHEREARG, channel->id, requestId);

		spu_dsmcbe_pendingRequests[requestId].responseCode = PACKAGE_CSP_CHANNEL_POISONED_RESPONSE;
		dsmcbe_thread_set_ready_by_requestId(requestId);
	}
	else
	{
		//If all is set, skip the buffer operations
		if (channel->readerId != UINT_MAX && channel->pendingWrites->count == 0)
		{
			//printf(WHERESTR "Got writeReq %d and reader was ready with id %d, channel: %d\n", WHEREARG, requestId, channel->readerId, channel->id);
			dsmcbe_csp_respondToDirectReader(channel, channel->readerId, data, size);

			//printf(WHERESTR "Responding to writeReq, writerId: %u, data: %u, size: %u, rb->count: %u\n", WHEREARG, requestId, (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, 1), (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->pendingWrites, 2), channel->pendingWrites->count);
			dsmcbe_csp_respondToDirectWriter(requestId);
		}
		else
		{
			//printf(WHERESTR "Buffering write request, reqId: %u, data: %u, size: %u, rb->count: %u, channel->id: %d\n", WHEREARG, requestId, (unsigned int)data, size, channel->pendingWrites->count, channel->id);
			dsmcbe_ringbuffer_push(channel->pendingWrites, (void*)requestId);
			dsmcbe_ringbuffer_push(channel->pendingWrites, data);
			dsmcbe_ringbuffer_push(channel->pendingWrites, (void*)size);
			//printf(WHERESTR "Buffered write request, reqId: %u, data: %u, size: %u, rb->count: %u\n", WHEREARG, requestId, (unsigned int)data, size, channel->pendingWrites->count);

			//A buffered request
			if (channel->pendingWrites->count != channel->pendingWrites->size)
				dsmcbe_csp_respondToDirectWriter(requestId);
		}
	}

	//We perform a thread switch here to make the operation more like the non-optimized version
	dsmcbe_thread_yield_ready();
}

//Handles a write request that crosses a direct setup response
void dsmcbe_csp_handleCrossWrite(unsigned int requestID)
{
	unsigned int channelId = spu_dsmcbe_pendingRequests[requestID].channelId;
	struct spu_dsmcbe_directChannelStruct* channel = dsmcbe_csp_findDirectChannelIndex(channelId);
	if (channel == NULL)
	{
		REPORT_ERROR2("Cross request for channel %d had no direct setup", channelId);
		exit(-1);
	}

	void* data = spu_dsmcbe_pendingRequests[requestID].pointer;
	unsigned int size = spu_dsmcbe_pendingRequests[requestID].size;

	spu_dsmcbe_pendingRequests[requestID].pointer = NULL;
	spu_dsmcbe_pendingRequests[requestID].size = 0;
	spu_dsmcbe_pendingRequests[requestID].responseCode = 0;

	dsmcbe_csp_handleDirectWriteRequest(channel, requestID, data, size);

}

//Sets up a direct local channel
void dsmcbe_csp_setupDirectChannel(unsigned int requestID, GUID channelId, void* pendingWrites)
{
	if (dsmcbe_csp_findDirectChannelIndex(channelId) != NULL)
	{
		REPORT_ERROR2("Duplicate direct setup of channel %d detected", channelId);
		exit(-1);
	}

	int index = channelId % MAX_DIRECT_CHANNELS;
	size_t i;
	for(i = 0; i < MAX_DIRECT_CHANNELS; i++)
		if (spu_dsmcbe_directChannels[(i + index) % MAX_DIRECT_CHANNELS].id == 0)
			break;

	if (i >= MAX_DIRECT_CHANNELS)
	{
		REPORT_ERROR("Out of direct channel space on SPE");
		exit(-1);
	}

	struct spu_dsmcbe_directChannelStruct* channel = &spu_dsmcbe_directChannels[(i + index) % MAX_DIRECT_CHANNELS];
	channel->id = channelId;
	channel->readerId = UINT_MAX;
	channel->pendingWrites = (struct dsmcbe_ringbuffer*)pendingWrites;

	unsigned int shouldRespondToNext = channel->pendingWrites->count == channel->pendingWrites->size;

	unsigned int wreqId = (unsigned int)dsmcbe_ringbuffer_pop(channel->pendingWrites);

	//We are now set up, and the first item in pendingWrites matches the readRequest
	spu_dsmcbe_pendingRequests[requestID].responseCode = PACKAGE_CSP_CHANNEL_READ_RESPONSE;
	spu_dsmcbe_pendingRequests[requestID].pointer = dsmcbe_ringbuffer_pop(channel->pendingWrites);
	spu_dsmcbe_pendingRequests[requestID].size = (unsigned int)dsmcbe_ringbuffer_pop(channel->pendingWrites);

	spu_dsmcbe_pendingRequests[wreqId].responseCode = PACKAGE_CSP_CHANNEL_WRITE_RESPONSE;
	spu_dsmcbe_pendingRequests[wreqId].pointer = NULL;
	spu_dsmcbe_pendingRequests[wreqId].size = 0;

	dsmcbe_thread_set_ready_by_requestId(requestID);
	dsmcbe_thread_set_ready_by_requestId(wreqId);

	if (shouldRespondToNext)
		dsmcbe_csp_respondToDirectWriter((unsigned int)dsmcbe_ringbuffer_peek(channel->pendingWrites));

	//printf(WHERESTR "After setup, writeId: %u, readId: %u, size: %u, ls: %u, rb: %u, rb->count: %u\n", WHEREARG, wreqId, requestID, spu_dsmcbe_pendingRequests[requestID].size, (unsigned int)spu_dsmcbe_pendingRequests[requestID].pointer, (unsigned int)channel->pendingWrites, channel->pendingWrites->count);
}

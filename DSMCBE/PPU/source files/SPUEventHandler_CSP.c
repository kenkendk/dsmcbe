/*
 * Direct transfer scheme
 *
 * To speed up transfers, CSP uses a direct transfer, possibly bypassing main memory.
 *
 * Scenario 1, reader is PPU, writer is PPU
 *   Very simple, no action taken, the pointer is immediately accessible to the read, and need not be freed by the writer
 *
 * Scenario 2, reader is SPU, writer is SPU
 *  Reader needs to allocate space, and transfer data
 *  After this writer needs to free the pointer
 *
 * Scenario 3, reader is SPU, writer is PPU
 * Simple, reader needs to allocate space and transfer data
 *
 * Scenario 4, reader is PPU, writer is SPU
 *   Writer needs to transfer data, then notify the reader
 *
 * To handle these scenarios correctly, the PACKAGE_READ_RESPONSE has a flag indicating
 * if the data is located on the SPU.
 *
 */


#include <dsmcbe_csp.h>
#include <dsmcbe_csp_initializers.h>
#include <dsmcbe_initializers.h>
#include <SPUEventHandler.h>
#include <SPUEventHandler_CSP.h>
#include <SPUEventHandler_shared.h>
#include <SPUEventHandler_extrapackages.h>
#include <glib.h>
#include <stdlib.h>
#include <debug.h>
#include <malloc_align.h>
#include <free_align.h>

#define CREATE_PENDING_REQUEST(code) dsmcbe_spu_new_PendingRequest(state, requestId, code, id, NULL, 0, TRUE);

#define REMOVE_PENDING_REQUEST \
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId); \
	if (preq == NULL) { \
		REPORT_ERROR("Got response for non initiated request"); \
		return; \
	} \
	dsmcbe_spu_free_PendingRequest(preq);

//We need this to check the number of hardware threads
unsigned int dsmcbe_spu_ppu_threadcount;

//This function handles incoming create channel requests from an SPU
void dsmcbe_spu_csp_HandleChannelCreateRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	//Record the ongoing request
	unsigned int requestId = args->requestId;
	unsigned int id = args->id;
	CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_CREATE_REQUEST);

	//Create a matching request for the request coordinator
	struct dsmcbe_cspChannelCreateRequest* req;
	if (dsmcbe_new_cspChannelCreateRequest(&req, args->id, args->requestId, args->size, args->mode) != CSP_CALL_SUCCESS)
		exit(-1);

	//Forward the request
	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelCreateResponse(struct dsmcbe_spu_state* state, unsigned int packagecode, unsigned int requestId)
{
	REMOVE_PENDING_REQUEST

	dsmcbe_spu_SendMessagesToSPU(state, packagecode, requestId, 0, 0);
}

void dsmcbe_spu_csp_HandleChannelPoisonRequest_Eventhandler(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args) {
	dsmcbe_spu_csp_HandleChannelPoisonRequest(state, args->requestId, args->id);
}


//This function handles incoming create channel requests from an SPU
void dsmcbe_spu_csp_HandleChannelPoisonRequest(struct dsmcbe_spu_state* state, unsigned int requestId, unsigned int id)
{
	//If the channel is direct, we absorb it here before any malloc etc is done
	struct dsmcbe_spu_directChannelObject* channel = g_hash_table_lookup(state->csp_direct_channels, (void*)id);
	if (channel != NULL)
	{
		//Don't respond if the poison was initiated elsewhere
		if (requestId != UINT_MAX)
		{
			dsmcbe_spu_SendMessagesToSPU(
					state,
					channel->poisoned ? PACKAGE_NACK : PACKAGE_CSP_CHANNEL_POISON_RESPONSE,
					requestId,
					0,
					0
			);
		}

		channel->poisoned = TRUE;

		if (channel->readerRequestId != UINT_MAX && channel->writerRequestIds->count != 0)
		{
			//Odd situation, but there is a match and a transfer is in progress,
			// the poison came later and is thus deferred
		}
		else if (channel->readerRequestId != UINT_MAX)
		{
			dsmcbe_spu_SendMessagesToSPU(channel->readerState, PACKAGE_CSP_CHANNEL_POISONED_RESPONSE, channel->readerRequestId, 0, 0);
			channel->readerRequestId = UINT_MAX;
		}
		else if (channel->writerRequestIds->count == channel->writerRequestIds->size)
		{
			//Get the id of the blocked request
			unsigned int wReqId = (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->writerRequestIds, channel->writerRequestIds->count - 1);

			dsmcbe_spu_SendMessagesToSPU(channel->writerState, PACKAGE_CSP_CHANNEL_POISONED_RESPONSE, wReqId, 0, 0);

			//HACK: Remove the unused entries
			channel->writerRequestIds->count--;
			channel->writerDataEAs->count--;
			channel->dataObjs->count--;

			//TODO: If the transfer is in progress...
		}

		//If the channel is optimized to be on the SPE, notify the SPE too
		if (channel->readerState == channel->writerState)
		{
			//printf(WHERESTR "Forward external poison request to SPE for channel %d\n", WHEREARG, channel->id);
			dsmcbe_spu_SendMessagesToSPU(channel->writerState, PACKAGE_SPU_CSP_CHANNEL_POISON_DIRECT, UINT_MAX, channel->id, 0);
		}
		return;
	}

	//Don't forward if the poison was initiated elsewhere
	if (requestId != UINT_MAX)
	{
		//Record the ongoing request
		CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_POISON_REQUEST);

		//Create a matching request for the request coordinator
		struct dsmcbe_cspChannelPoisonRequest* req;
		if (dsmcbe_new_cspChannelPoisonRequest(&req, id, requestId) != CSP_CALL_SUCCESS)
			exit(-1);

		//Forward the request
		dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
	}
}

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelPoisonResponse(struct dsmcbe_spu_state* state, unsigned int packagecode, unsigned int requestId)
{
	REMOVE_PENDING_REQUEST

	dsmcbe_spu_SendMessagesToSPU(state, packagecode, requestId, 0, 0);
}

unsigned int dsmcbe_spu_csp_FlushItems(struct dsmcbe_spu_state* state, unsigned int requested_size)
{
#ifndef SPE_CSP_CHANNEL_EAGER_TRANSFER
	unsigned int freed;
	GHashTableIter it;
	void* key;
	struct dsmcbe_spu_dataObject* value;

	freed = 0;
	g_hash_table_iter_init(&it, state->csp_inactive_items);

	while(g_hash_table_iter_next(&it, &key, (void**)&value))
	{
		if (value->mode == CSP_ITEM_MODE_IN_TRANSIT)
			freed += value->size;
		else if (value->mode == CSP_ITEM_MODE_READY_FOR_TRANSFER)
		{
			if (value->EA == NULL)
				value->EA = MALLOC_ALIGN(ALIGNED_SIZE(value->size), 7);

			value->isDMAToSPU = FALSE;

			freed += value->size;

			//printf(WHERESTR "Delayed free of %d bytes @%u\n", WHEREARG, (unsigned int)value->size, (unsigned int)value->LS);

			value->mode = CSP_ITEM_MODE_IN_TRANSIT;
			struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_new_PendingRequest(state, NEXT_SEQ_NO(state->cspSeqNo, MAX_CSP_INACTIVE_ITEMS) + CSP_INACTIVE_BASE, PACKAGE_SPU_CSP_FLUSH_ITEM, value->id, NULL, 0, TRUE);
			dsmcbe_spu_TransferObject(state, preq, value);
		}

		if (freed >= requested_size)
			break;
	}

	return freed;
#else
	requested_size += state->dmaSeqNo; //Remove compiler warning
	return 0;
#endif
}

//Attempts to allocate the required amount of memory on the SPU
//If there is not enough memory left, but objects to remove,
// the request is delayed, otherwise a NACK message is sent
//The return value is NULL if the memory could not be allocated right now,
// a pointer otherwise
void* dsmcbe_spu_csp_attempt_get_pointer(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq, unsigned int size)
{
	void* ls = dsmcbe_spu_AllocateSpaceForObject(state, size);

	if (ls == NULL)
	{
		//No more space, wait until we get some
		if (g_queue_find(state->releaseWaiters, preq) == NULL)
			g_queue_push_tail(state->releaseWaiters, preq);

		//If we are all out, don't wait but respond with error
		if (dsmcbe_spu_EstimatePendingReleaseSize(state) == 0 && dsmcbe_spu_csp_FlushItems(state, size) == 0)
		{
			//Remove the mark
			g_queue_remove_all(state->releaseWaiters, preq);

			REPORT_ERROR2("Out of memory on SPU, requested size: %d", size);

			dsmcbe_spu_printMemoryStatus(state);

			printf("\n\n");

			dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, preq->requestId, 0, 0);
			FREE(preq->dataObj);
			dsmcbe_spu_free_PendingRequest(preq);
		}
		else
		{
			//printf(WHERESTR "Delayed request for %d bytes\n", WHEREARG, size);
		}
	}
	else
	{
		if (g_hash_table_lookup(state->csp_active_items, ls) != NULL)
		{
			REPORT_ERROR2("Newly allocated pointer @%u was found in csp table", (unsigned int)ls);
			dsmcbe_spu_DumpCurrentStates();
			dsmcbe_spu_printMemoryStatus(state);
			exit(-1);
		}
	}

	return ls;
}

//This function handles incoming create item requests from an SPU
void dsmcbe_spu_csp_HandleItemCreateRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	GUID id = 0;
	unsigned int requestId = args->requestId;
	struct dsmcbe_spu_pendingRequest*  preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST);
	struct dsmcbe_spu_dataObject* obj = dsmcbe_spu_new_dataObject(0, TRUE, CSP_ITEM_MODE_IN_USE, 0, UINT_MAX, NULL, NULL, UINT_MAX, args->size, TRUE, FALSE, preq);
	preq->dataObj = obj;

	obj->LS = dsmcbe_spu_csp_attempt_get_pointer(state, preq, args->size);

	if (obj->LS != NULL)
	{
		g_hash_table_insert(state->csp_active_items, obj->LS, obj);

		dsmcbe_spu_free_PendingRequest(preq);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE, requestId, (unsigned int)obj->LS, args->size);
	}

	//printf(WHERESTR "Handled item create request for %u\n", WHEREARG, (unsigned int)obj->LS);

}

//This function handles incoming free item requests from an SPU
void dsmcbe_spu_csp_HandleItemFreeRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_active_items, (void*)args->data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to release unknown pointer: %d", args->data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, args->requestId, 0, 0);
		return;
	}

	if (obj->mode != CSP_ITEM_MODE_IN_USE && obj->mode != CSP_ITEM_MODE_SENT)
	{
		REPORT_ERROR2("Unable to free item, as it is in an invalid state, %d", obj->mode);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, args->requestId, 0, 0);
		return;
	}

	//printf(WHERESTR "Handling item free request for %u\n", WHEREARG, args->data);

	FREE(obj);
	g_hash_table_remove(state->csp_active_items, (void*)args->data);
	dsmcbe_spu_memory_free(state->map, (void*)args->data);
	dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE, args->requestId, 0, 0);

	dsmcbe_spu_ManageDelayedAllocation(state);
}

//This function handles incoming channel read requests from an SPU
void dsmcbe_spu_csp_HandleChannelReadRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	//printf(WHERESTR "Context %u got read request for channel %u, with requestId: %u\n", WHEREARG, (unsigned int) state->context, id, requestId);
	//If the channel is direct, we absorb it here before any malloc etc is done
	struct dsmcbe_spu_directChannelObject* channel = g_hash_table_lookup(state->csp_direct_channels, (void*)args->id);
	if (channel != NULL)
	{
		if (channel->readerRequestId != UINT_MAX)
		{
			REPORT_ERROR2("Duplicate read request for direct channel %d", channel->id);
			dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, args->requestId, 0, 0);
		}
		else
			dsmcbe_spu_csp_HandleDirectReadRequest(state, channel, args->requestId);

		return;
	}

	//Record the ongoing request
	unsigned int requestId = args->requestId;
	unsigned int id = args->id;
	CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_READ_REQUEST)

	//Create a matching request for the request coordinator
	struct dsmcbe_cspChannelReadRequest* req;
	if (dsmcbe_new_cspChannelReadRequest_single(&req, args->id, args->requestId, (unsigned int)state) != CSP_CALL_SUCCESS)
	{
		printf("Error?\n");
		exit(-1);
	}

	//Forward the request
	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}

//This function handles incoming channel read requests from an SPU
void dsmcbe_spu_csp_HandleChannelReadRequestAlt(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	GUID id = CSP_SKIP_GUARD;

	//Record the ongoing request
	unsigned int requestId = args->requestId;
	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST);
	if (args->channelId != 0)
		preq->channelPointer = spe_ls_area_get(state->context) + args->channelId;

	//Create a matching request for the request coordinator
	struct dsmcbe_cspChannelReadRequest* req;
	if (dsmcbe_new_cspChannelReadRequest_multiple(&req, args->requestId, args->mode, spe_ls_area_get(state->context) + (unsigned int)args->channels, args->size) != CSP_CALL_SUCCESS)
		exit(-1);

	//Forward the request
	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}


//This function handles incoming channel read responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelReadResponse(struct dsmcbe_spu_state* state, void* package)
{
	struct dsmcbe_cspChannelReadResponse* resp = (struct dsmcbe_cspChannelReadResponse*)package;
	unsigned int requestId = resp->requestID;

	if (resp->packageCode != PACKAGE_CSP_CHANNEL_READ_RESPONSE)
	{
		REPORT_ERROR("Got invalid packageCode for readResponse");
	}

	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);

	if (preq == NULL)
		REPORT_ERROR2("Failed to find pending request with number %d", requestId);

#ifndef SPE_CSP_CHANNEL_EAGER_TRANSFER
	if (resp->speId == (unsigned int)state)
	{
		//Special case, we have communicated with ourselves
		struct dsmcbe_spu_dataObject* oldObj = g_hash_table_lookup(state->csp_inactive_items, resp->data);
		if (oldObj == NULL)
		{
			REPORT_ERROR("Broḱen self-transfer");
			exit(-1);
		}

		 if (oldObj->mode == CSP_ITEM_MODE_IN_TRANSIT )
		{
			//printf(WHERESTR "Hit item in transit \n", WHEREARG);
			if (oldObj->preq == NULL || oldObj->preq->operation != PACKAGE_SPU_CSP_FLUSH_ITEM)
			{
				REPORT_ERROR("Object was in transit but not being flushed?");
				exit(-1);
			}

			if (oldObj->flushPreq != NULL)
			{
				REPORT_ERROR("Object was being flushed but already in use?");
				exit(-1);
			}

			//Sane situation, register the read request so we can handle it after the flush has completed
			oldObj->flushPreq = dsmcbe_spu_new_PendingRequest(state, NEXT_SEQ_NO(state->releaseSeqNo, MAX_PENDING_RELEASE_REQUESTS) + RELEASE_NUMBER_BASE, resp->packageCode, resp->channelId, oldObj, 0, 0);
			oldObj->flushPreq->transferHandler = resp;
			return;
		}

		FREE(resp->transferManager);
		resp->transferManager = NULL;

		if (oldObj->mode == CSP_ITEM_MODE_FLUSHED)
		{
			//The transfer is for this SPE, but the item was flushed out, so we move it back in
			g_hash_table_remove(state->csp_inactive_items, resp->data);
			resp->speId = 0;
			resp->data = oldObj->EA;
			FREE(oldObj);
		}
		else if (oldObj->mode == CSP_ITEM_MODE_READY_FOR_TRANSFER)
		{
			//The transfer is for this SPE and the item is already on the SPE
			g_hash_table_remove(state->csp_inactive_items, resp->data);
			g_hash_table_insert(state->csp_active_items, oldObj->LS, oldObj);
			oldObj->mode = CSP_ITEM_MODE_IN_USE;

			if (preq->operation == PACKAGE_CSP_CHANNEL_READ_REQUEST)
			{
				dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_CSP_CHANNEL_READ_RESPONSE, preq->requestId, (unsigned int)oldObj->LS, oldObj->size);
				dsmcbe_spu_free_PendingRequest(preq);
			}
			else if (preq->operation == PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST)
			{
				if (preq->channelPointer != NULL)
					*((unsigned int*)preq->channelPointer) = resp->channelId;
				dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE, preq->requestId, (unsigned int)oldObj->LS, oldObj->size);
				dsmcbe_spu_free_PendingRequest(preq);
			}
			else
			{
				REPORT_ERROR2("Unexpected csp package code %d", preq->operation);
			}

			FREE(resp);
			return;
		}
		else
		{
			REPORT_ERROR2("Unsupported mode for item: %d, likely an unsupported race situation", oldObj->mode);
			exit(-1);
		}

	}
#endif

	struct dsmcbe_spu_dataObject* obj = dsmcbe_spu_new_dataObject(resp->channelId, TRUE, CSP_ITEM_MODE_IN_USE, 0, UINT_MAX, ((struct dsmcbe_cspChannelReadResponse*)resp)->data, NULL, UINT_MAX, ((struct dsmcbe_cspChannelReadResponse*)resp)->size, TRUE, TRUE, preq);

	obj->mode = CSP_ITEM_MODE_IN_USE;
	preq->dataObj = obj;
	preq->objId = resp->channelId;
	if (resp->speId != 0)
	{
		if (resp->transferManager == NULL)
			printf(WHERESTR "Unexpected missing transfer manager, speId = %d\n", WHEREARG, resp->speId);

		preq->transferHandler = resp;
		obj->EA = NULL; //This is not a valid pointer
	}
	else
	{
		preq->transferHandler = NULL;
		FREE(resp);
		resp = NULL;
	}

	obj->isDMAToSPU = TRUE;
	obj->LS = dsmcbe_spu_csp_attempt_get_pointer(state, preq, obj->size);
	if (preq->dataObj == NULL)
	{
		REPORT_ERROR("Out of memory on SPU");
		exit(-1);
	}

	if (obj->LS != NULL)
	{
		g_hash_table_insert(state->csp_active_items, obj->LS, obj);
		if (preq->transferHandler != NULL)
		{
			//printf(WHERESTR "Transfer was from SPU, handling by remote transfer, %u\n", WHEREARG, (unsigned int)preq);
			dsmcbe_spu_csp_RequestTransfer(state, preq);
		}
		else
		{
			//printf(WHERESTR "Transfer was from PPU, handling by local transfer\n", WHEREARG);
			dsmcbe_spu_TransferObject(state, preq, obj);
		}
	}
}

//Initiates a transfer request on another SPE
void dsmcbe_spu_csp_RequestTransfer(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq)
{
	//printf(WHERESTR "Transfer was from SPU, requesting transfer, local EA is %u, preq @%u, handler @%u\n", WHEREARG, (unsigned int)preq->dataObj->EA, (unsigned int)preq, (unsigned int)preq->transferHandler);

	struct dsmcbe_cspChannelReadResponse* resp = (struct dsmcbe_cspChannelReadResponse*)preq->transferHandler;
#ifdef USE_EVENTS
	pthread_cond_t* cond = &state->inCond;
#else
	pthread_cond_t* cond = NULL;
#endif

	//printf(WHERESTR "Requesting transfer, resp: @%u, resp->tfm: @%u, package: %s (%d), reqId: %d\n", WHEREARG, (unsigned int)resp, (unsigned int)resp->transferManager, PACKAGE_NAME(resp->packageCode), resp->packageCode, resp->requestID);

	struct dsmcbe_transferRequest* req = dsmcbe_new_transferRequest(resp->requestID, &state->inMutex, cond, &state->inQueue, resp->data, spe_ls_area_get(state->context)  + (unsigned int)preq->dataObj->LS);
	dsmcbe_rc_SendMessage(resp->transferManager, req);
	FREE(resp);

	preq->transferHandler = NULL;
}

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_active_items, (void*)args->data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to use unknown pointer for write: %d", args->data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, args->requestId, 0, 0);
		return;
	}

	//printf(WHERESTR "Context %u got write request for channel %u, with requestId: %u, dataObj: %u, mode: %u\n", WHEREARG, (unsigned int)state->context, id, requestId, (unsigned int)obj, obj->mode);

	//If the channel is direct, we absorb it here before any malloc etc is done
	struct dsmcbe_spu_directChannelObject* channel = g_hash_table_lookup(state->csp_direct_channels, (void*)args->id);
	if (channel != NULL)
	{
		dsmcbe_spu_csp_HandleDirectWriteRequest(state, channel, args->requestId, obj, TRUE);
		return;
	}

	//printf(WHERESTR "Context %u handling normal write request for channel %u, with requestId: %u, dataObj: %u, mode: %u\n", WHEREARG, (unsigned int)state->context, id, requestId, (unsigned int)obj, obj->mode);

	unsigned int requestId = args->requestId;
	unsigned int id = args->id;

#ifdef SPE_CSP_CHANNEL_EAGER_TRANSFER

	if (obj->EA == NULL)
		obj->EA = MALLOC_ALIGN(ALIGNED_SIZE(obj->size), 7);

	//Record the ongoing request
	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_WRITE_REQUEST);
	preq->dataObj = obj;
	obj->preq = preq;
	obj->isDMAToSPU = FALSE;

	dsmcbe_spu_TransferObject(state, preq, obj);

#else

	obj->csp_seq_no = NEXT_SEQ_NO(state->cspSeqNo, MAX_CSP_INACTIVE_ITEMS) + CSP_INACTIVE_BASE;

	int max_tries = MAX_CSP_INACTIVE_ITEMS + 1;
	while (max_tries-- > 0 && g_hash_table_lookup(state->csp_inactive_items, (void*)obj->csp_seq_no) != NULL)
	{
#ifdef DEBUG
		//printf(WHERESTR "Retry set id, seq_no=%d, tries left=%d, current pending count=%d, to avoid this increase MAX_CSP_INACTIVE_ITEMS which is currently %d\n", WHEREARG, obj->csp_seq_no, max_tries, g_hash_table_size(state->csp_inactive_items), MAX_CSP_INACTIVE_ITEMS);
#endif
		obj->csp_seq_no = NEXT_SEQ_NO(state->cspSeqNo, MAX_CSP_INACTIVE_ITEMS) + CSP_INACTIVE_BASE;
	}

	if (max_tries == 0)
	{
		REPORT_ERROR("Ran out of pending csp id's");
		exit(-1);
	}

	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_WRITE_REQUEST);

	struct dsmcbe_cspChannelWriteRequest* req;
	if(dsmcbe_new_cspChannelWriteRequest_single(&req, id, requestId, (void*)obj->csp_seq_no, obj->size, (unsigned int)state, dsmcbe_spu_createTransferManager(state)) != CSP_CALL_SUCCESS)
		exit(-1);

	//printf(WHERESTR "Creating a transferManager with Reqid: %u, and tfm: @%u\n", WHEREARG, req->requestID, (unsigned int)req->transferManager);

	preq->transferHandler = req->transferManager;
	preq->dataObj = obj;

	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);

#endif
}

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequestAlt(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	GUID id = CSP_SKIP_GUARD;

	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_active_items, (void*)args->data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to use unknown pointer for write: %d", args->data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, args->requestId, 0, 0);
		return;
	}

	unsigned int requestId = args->requestId;
#ifdef SPE_CSP_CHANNEL_EAGER_TRANSFER


	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST);
	preq->dataObj = obj;
	obj->preq = preq;
	obj->isDMAToSPU = FALSE;

	if (obj->EA == NULL)
		obj->EA = MALLOC_ALIGN(ALIGNED_SIZE(obj->size), 7);

	if (dsmcbe_new_cspChannelWriteRequest_multiple((struct dsmcbe_cspChannelWriteRequest**)&(preq->channelPointer), args->requestId, args->mode, spe_ls_area_get(state->context) + (unsigned int)args->channels, args->size, obj->size, obj->EA, 0, NULL) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_spu_TransferObject(state, preq, obj);

#else

	obj->csp_seq_no = NEXT_SEQ_NO(state->cspSeqNo, MAX_CSP_INACTIVE_ITEMS) + CSP_INACTIVE_BASE;

	int max_tries = CSP_INACTIVE_BASE + 1;
	while (max_tries-- > 0 && g_hash_table_lookup(state->csp_inactive_items, (void*)obj->csp_seq_no) != NULL)
	{
#ifdef DEBUG
		printf(WHERESTR "Retry set id, seq_no=%d, tries left=%d, current pending count=%d, to avoid this increase MAX_CSP_INACTIVE_ITEMS which is currently %d\n", WHEREARG, obj->csp_seq_no, max_tries, g_hash_table_size(state->csp_inactive_items), MAX_CSP_INACTIVE_ITEMS);
#endif
		obj->csp_seq_no = NEXT_SEQ_NO(state->cspSeqNo, CSP_INACTIVE_BASE) + CSP_INACTIVE_BASE;
	}

	if (max_tries == 0)
	{
		REPORT_ERROR("Ran out of pending csp id's");
		exit(-1);
	}

	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST);

	struct dsmcbe_cspChannelWriteRequest* req;
	if (dsmcbe_new_cspChannelWriteRequest_multiple(&req, args->requestId, args->mode, spe_ls_area_get(state->context) + (unsigned int)args->channels, args->size, obj->size, (void*)obj->csp_seq_no, (unsigned int)state, dsmcbe_spu_createTransferManager(state)) != CSP_CALL_SUCCESS)
		exit(-1);

	preq->transferHandler = req->transferManager;
	preq->dataObj = obj;

	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);

#endif

}

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelWriteResponse(struct dsmcbe_spu_state* state, void* resp)
{
	unsigned int requestId = ((struct dsmcbe_createRequest*)resp)->requestID;

	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);

	if (preq == NULL)
	{
		fprintf(stderr, "* ERROR * " WHERESTR "Context %u, Got unmatched response, requestId: %d\n", WHEREARG, (unsigned int)state->context, requestId);
		return;
	}

	if (((struct dsmcbe_createRequest*)resp)->packageCode != PACKAGE_CSP_CHANNEL_WRITE_RESPONSE)
	{
		REPORT_ERROR("Got a write response, with invalid package code");
	}

#ifdef SPE_CSP_CHANNEL_EAGER_TRANSFER
	if (preq->dataObj == NULL)
	{
		REPORT_ERROR("Got a write response, but found no data");
	}
	else
	{
		//Data is now delivered correctly, free the local space
		if (preq->dataObj->LS != NULL)
		{
			g_hash_table_remove(state->csp_active_items, preq->dataObj->LS);
			dsmcbe_spu_memory_free(state->map, preq->dataObj->LS);
			preq->dataObj->LS = NULL;
		}

		preq->dataObj->preq = NULL;
		FREE(preq->dataObj);
		preq->dataObj = NULL;
	}
#else
	preq->dataObj->mode = CSP_ITEM_MODE_READY_FOR_TRANSFER;
	//printf(WHERESTR "Moving item to inactive, %d\n", WHEREARG, (unsigned int)preq->dataObj->LS);
	g_hash_table_remove(state->csp_active_items, preq->dataObj->LS);

	if (g_hash_table_lookup(state->csp_inactive_items, (void*)preq->dataObj->csp_seq_no) != NULL)
	{
		REPORT_ERROR2("Attempting to re-use entry %u", preq->dataObj->csp_seq_no);
		REPORT_ERROR2("The table has %d entries", g_hash_table_size(state->csp_inactive_items));
	}

	g_hash_table_insert(state->csp_inactive_items, (void*)preq->dataObj->csp_seq_no, preq->dataObj);
#endif

	dsmcbe_spu_SendMessagesToSPU(state, preq->operation == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST ? PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE : PACKAGE_CSP_CHANNEL_WRITE_RESPONSE, requestId, ((struct dsmcbe_cspChannelWriteResponse*)resp)->channelId, 0);

	dsmcbe_spu_free_PendingRequest(preq);

	dsmcbe_spu_ManageDelayedAllocation(state);
}

//Handles any nack, poison or skip response
void dsmcbe_spu_csp_HandleChannelPoisonNACKorSkipResponse(struct dsmcbe_spu_state* state, void* resp)
{
	unsigned int requestId = ((struct dsmcbe_createRequest*)resp)->requestID;
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);

	if (preq == NULL)
	{
		REPORT_ERROR("Got unmatched response");
		return;
	}

	if (!preq->isCSP)
	{
		REPORT_ERROR("Got response for non-csp request");
		return;
	}

	if (preq->transferHandler != NULL && (preq->operation == PACKAGE_CSP_CHANNEL_WRITE_REQUEST || preq->operation == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST))
	{
		FREE(preq->transferHandler);
		preq->transferHandler = NULL;
	}

	dsmcbe_spu_SendMessagesToSPU(state, ((struct dsmcbe_createRequest*)resp)->packageCode, requestId, 0, 0);

	dsmcbe_spu_free_PendingRequest(preq);
}

//Handles a write request that crossed the directSetup request
void dsmcbe_spu_csp_HandleRoundTripWriteRequest(struct dsmcbe_spu_state* localState, void* _resp)
{
	struct dsmcbe_cspChannelWriteRequest* wreq = (struct dsmcbe_cspChannelWriteRequest*)_resp;

	//printf(WHERESTR "Handling crossed write request for channel id %d\n", WHEREARG, wreq->channelId);

	struct dsmcbe_spu_directChannelObject* chan = (struct dsmcbe_spu_directChannelObject*)g_hash_table_lookup(localState->csp_direct_channels, (void*)wreq->channelId);
	if (chan == NULL)
	{
		REPORT_ERROR2("Got crossed write request, but channel %d was not direct?", wreq->channelId);
		exit(-1);
	}

	if (wreq->speId != (unsigned int)chan->writerState)
	{
		REPORT_ERROR2("Multiple request found for optimized channel %d but with different sources", chan->id);
		exit(-1);
	}

	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(chan->writerState, wreq->requestID);
	if (preq == NULL)
	{
		REPORT_ERROR2("Got crossed write request for channel %d, but could not find the pending request", wreq->channelId);
		exit(-1);
	}

	//Extract the dataObj
	struct dsmcbe_spu_dataObject* obj = preq->dataObj;
	if (obj == NULL)
	{
		REPORT_ERROR2("Got crossed write request for channel %d, but there was no object at the address?", wreq->channelId);
		REPORT_ERROR2("Got crossed write request, id was: %d", (unsigned int)wreq->data);
		exit(-1);
	}

	if (obj->mode != CSP_ITEM_MODE_IN_USE)
	{
		REPORT_ERROR2("Got crossed write request for channel %d, but object has unsupported state?", wreq->channelId);
		REPORT_ERROR2("Unsupported state is: %d?", obj->mode);
		exit(-1);
	}

	//Clear resources for this preq
	dsmcbe_spu_free_PendingRequest(preq);

	dsmcbe_spu_csp_HandleDirectWriteRequest(chan->writerState, chan, wreq->requestID, obj, TRUE);
}

//Handles a request for setting up a direct transfer
void dsmcbe_spu_csp_HandleDirectSetupResponse(struct dsmcbe_spu_state* localState, void* _resp)
{
	if (dsmcbe_spu_ppu_threadcount != 1)
	{
		REPORT_ERROR("Optimized channels are NOT supported with multiple PPE hardware threads for the SPE handler");
		exit(-1);
	}

	struct dsmcbe_cspDirectSetupResponse* resp = (struct dsmcbe_cspDirectSetupResponse*)_resp;
	struct dsmcbe_cspChannelWriteRequest* wreq = (struct dsmcbe_cspChannelWriteRequest*)resp->writeRequest;
	struct dsmcbe_spu_state* remoteState = (struct dsmcbe_spu_state*)wreq->speId;

	if (g_hash_table_lookup(localState->csp_direct_channels, (void*)resp->channelId) != NULL || g_hash_table_lookup(remoteState->csp_direct_channels, (void*)resp->channelId) != NULL)
	{
		REPORT_ERROR("ChannelId was already registered as a direct channel");
		exit(-1);
	}

	struct dsmcbe_spu_directChannelObject* chan = (struct dsmcbe_spu_directChannelObject*)MALLOC(sizeof(struct dsmcbe_spu_directChannelObject));
	if (chan == NULL)
	{
		REPORT_ERROR("Out of memory");
		exit(-1);
	}

	//printf(WHERESTR "Got direct setup request for channel id %d\n", WHEREARG, resp->channelId);

	//Setup the direct transfer block as empty
	chan->id = resp->channelId;
	chan->readerState = localState;
	chan->writerState = remoteState;
	chan->poisoned = FALSE;

	chan->readerRequestId = UINT_MAX;
	chan->readerDataEA = NULL;

	chan->writerRequestIds = dsmcbe_ringbuffer_new(resp->bufferSize + 1);
	chan->writerDataEAs = dsmcbe_ringbuffer_new(resp->bufferSize + 1);
	chan->dataObjs = dsmcbe_ringbuffer_new(resp->bufferSize + 1);

	g_hash_table_insert(localState->csp_direct_channels, (void*)chan->id, chan);
	if (remoteState != localState)
		g_hash_table_insert(remoteState->csp_direct_channels, (void*)chan->id, chan);

	//printf(WHERESTR "Direct setup, readerState: %u, readerRequestId: %u, writerState: %u, writerRequestId: %u\n", WHEREARG, (unsigned int)localState, resp->requestID, (unsigned int)remoteState, wreq->requestID);

	//Extract the current pending requests
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(localState, resp->requestID);
	if (preq == NULL)
	{
		REPORT_ERROR("Pending request was not found");
		exit(-1);
	}
	dsmcbe_spu_free_PendingRequest(preq);

	resp->writeRequest = NULL;

	if (remoteState == localState)
	{
		//printf(WHERESTR "In setup, the item is intra SPE, setting up local on channel %d\n", WHEREARG, resp->channelId);

		//Allocate the ringbuffer on the SPE
		void* rb_ls = dsmcbe_spu_memory_malloc(localState->map, localState->context, sizeof(struct dsmcbe_ringbuffer) + ((sizeof(void*) * SPE_PENDING_WRITE_SIZE) * (resp->bufferSize + 1)));
		struct dsmcbe_ringbuffer* spe_rb = (struct dsmcbe_ringbuffer*)(spe_ls_area_get(localState->context) + (unsigned int)rb_ls);
		spe_rb->count = 0;
		spe_rb->head = 0;
		spe_rb->tail = 0;
		spe_rb->size = (resp->bufferSize + 1) * SPE_PENDING_WRITE_SIZE;

		while(!g_queue_is_empty((GQueue*)(resp->pendingWrites)))
		{
			QueueableItem tmpq = (QueueableItem)g_queue_pop_head(resp->pendingWrites);
			struct dsmcbe_cspChannelWriteRequest* tmpWReq = tmpq->dataRequest;

			//printf(WHERESTR "Channelid: %d, localState: %u, remoteState_ %u, speId: %u\n", WHEREARG, chan->id, (unsigned int)localState, (unsigned int)remoteState, (unsigned int)tmpWReq->speId);
			if (tmpWReq->speId != (unsigned int)localState)
			{
				REPORT_ERROR2("Multiple request found for optimized channel %d but with different sources", chan->id);
				exit(-1);
			}

			//printf(WHERESTR "Direct setup on channel %d, processing %u\n", WHEREARG, resp->channelId, tmpWReq->requestID);

			struct dsmcbe_spu_dataObject* obj;

			preq = dsmcbe_spu_FindPendingRequest(localState, tmpWReq->requestID);
			if (preq == NULL)
			{
				//If we get here, the response was already sent
				obj = g_hash_table_lookup(localState->csp_inactive_items, tmpWReq->data);
				if (obj == NULL)
				{
					REPORT_ERROR2("Unable to lookup WriteResponse with requestId: %d", tmpWReq->requestID);
					exit(-1);
				}

				if (obj->mode != CSP_ITEM_MODE_READY_FOR_TRANSFER)
				{
					REPORT_ERROR2("Unsupported object mode %d", obj->mode);
					exit(-1);
				}

				obj->mode = CSP_ITEM_MODE_IN_USE;
				g_hash_table_remove(remoteState->csp_inactive_items, tmpWReq->data);
				g_hash_table_insert(remoteState->csp_active_items, obj->LS, obj);

			}
			else
			{
				obj = preq->dataObj;

				//Clean the pending request
				preq->dataObj = NULL;
				dsmcbe_spu_free_PendingRequest(preq);
				preq = NULL;

				if (obj->mode != CSP_ITEM_MODE_IN_USE)
				{
					REPORT_ERROR2("Unsupported object mode %d", obj->mode);
					exit(-1);
				}
			}

			//Enqueue the write request the the SPE
			dsmcbe_ringbuffer_push(spe_rb, (void*)tmpWReq->requestID);
			dsmcbe_ringbuffer_push(spe_rb, obj->LS);
			dsmcbe_ringbuffer_push(spe_rb, (void*)obj->size);


			//printf(WHERESTR "Resp-stats, channelId: %d, spe_rb->count: %u, tmpq->GQueue: %u\n", WHEREARG, chan->id, spe_rb->count, (unsigned int)tmpq->Gqueue);

			if (spe_rb->count != spe_rb->size && tmpq->Gqueue != NULL)
			{
				//If Gqueue != NULL the RC has not responded to the message, so we need to do it here
				dsmcbe_spu_SendMessagesToSPU(chan->writerState, PACKAGE_CSP_CHANNEL_WRITE_RESPONSE, tmpWReq->requestID, 0, 0);
			}

			//printf(WHERESTR "Inserting write request with id %u, LS: %u, size: %u, rb: %u, rb->count: %u\n", WHEREARG, tmpWReq->requestID, (unsigned int)obj->LS, (unsigned int)obj->size, (unsigned int)rb_ls, spe_rb->count);

			//Clean up
			if (tmpWReq->transferManager != NULL)
			{
				FREE(tmpWReq->transferManager);
				tmpWReq->transferManager = NULL;
			}

			FREE(tmpWReq);
			tmpWReq = NULL;
			tmpq->dataRequest = NULL;
			FREE(tmpq);
			tmpq = NULL;
		}

		dsmcbe_spu_SendMessagesToSPU(localState, PACKAGE_SPU_CSP_CHANNEL_SETUP_DIRECT, resp->requestID, chan->id, (unsigned int)rb_ls);
	}
	else
	{
		while(!g_queue_is_empty((GQueue*)(resp->pendingWrites)))
		{
			QueueableItem tmpq = (QueueableItem)g_queue_pop_head(resp->pendingWrites);
			struct dsmcbe_cspChannelWriteRequest* tmpWReq = tmpq->dataRequest;

			if (tmpWReq->speId != (unsigned int)remoteState)
			{
				REPORT_ERROR2("Multiple request found for optimized channel %d but with different sources", chan->id);
				exit(-1);
			}

			struct dsmcbe_spu_dataObject* obj;
			preq = dsmcbe_spu_FindPendingRequest(remoteState, tmpWReq->requestID);
			if (preq == NULL)
			{
				obj = g_hash_table_lookup(remoteState->csp_inactive_items, tmpWReq->data);
				if (obj == NULL)
				{
					REPORT_ERROR2("Pending request with id: %d was not found", tmpWReq->requestID);
					exit(-1);
				}

				if (obj->mode != CSP_ITEM_MODE_READY_FOR_TRANSFER)
				{
					REPORT_ERROR2("Unsupported object mode %d", obj->mode);
					exit(-1);
				}

				//g_hash_table_remove(remoteState->csp_inactive_items, tmpWReq->data);
			}
			else
			{
				obj = preq->dataObj;

				if (obj->mode != CSP_ITEM_MODE_IN_USE)
				{
					REPORT_ERROR2("Unsupported object mode %d", obj->mode);
					exit(-1);
				}

				//Clean the pending request
				preq->dataObj = NULL;
				dsmcbe_spu_free_PendingRequest(preq);
				preq = NULL;
			}

			obj->id = chan->id;

			//printf(WHERESTR "Inserting write request with id %u, LS: %u, size: %u\n", WHEREARG, tmpWReq->requestID, (unsigned int)obj->LS, (unsigned int)obj->size);

			//Record the request locally, respond if required
			dsmcbe_spu_csp_HandleDirectWriteRequest(remoteState, chan, tmpWReq->requestID, obj, tmpq->Gqueue != NULL);

			//Clean up
			if (tmpWReq->transferManager != NULL)
			{
				FREE(tmpWReq->transferManager);
				tmpWReq->transferManager = NULL;
			}

			FREE(tmpWReq);
			tmpWReq = NULL;
			tmpq->dataRequest = NULL;
			FREE(tmpq);
			tmpq = NULL;
		}

		//This will trigger the transfer, regardless of the items current state
		dsmcbe_spu_csp_HandleDirectReadRequest(localState, chan, resp->requestID);
	}
}

void dsmcbe_spu_csp_PreTransferItem(struct dsmcbe_spu_directChannelObject* channel, struct dsmcbe_spu_dataObject* obj)
{
	//We need a preq and a dataObj for the transfer to work
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_new_PendingRequest(channel->readerState, NEXT_SEQ_NO(channel->readerState->cspSeqNo, MAX_CSP_INACTIVE_ITEMS) + CSP_INACTIVE_BASE, PACKAGE_SPU_CSP_DIRECT_TRANSFER, channel->id, obj, 0, TRUE);

	obj->preq = preq;
	preq->dataObj = obj;

	//This protects against a pre-transferred item
	if (obj->EA == NULL)
		obj->EA = dsmcbe_ringbuffer_peek(channel->writerDataEAs);
	obj->isDMAToSPU = TRUE;

	channel->readerDataEA = dsmcbe_spu_csp_attempt_get_pointer(channel->readerState, preq, obj->size);
	if (channel->readerDataEA == NULL)
	{
		obj->LS = NULL;

		//TODO: If we get here, do we record the requestId twice?
		printf(WHERESTR "No space on target SPE, delaying transfer, channel %d --- WARNING - UNTESTED!!!\n", WHEREARG, channel->id);
		return;
	}

	//printf(WHERESTR "Reader-Context %u, Writer allocated space on reader SPE, channel %d, got LS: %u\n", WHEREARG, (unsigned int)channel->readerState, channel->id, (unsigned int)channel->readerDataEA);

	//Get the LS to pretend the reader started the transfer
	obj->LS = channel->readerDataEA;

	//The pointer is returned in LS, convert to EA
	channel->readerDataEA = spe_ls_area_get(channel->readerState->context) + (unsigned int)channel->readerDataEA;

	//printf(WHERESTR "Writer is performing DMA transfer on channel %d, readerLS: %u\n", WHEREARG, channel->id, (unsigned int)obj->LS);

	//Transfer the item to the target SPE
	obj->mode = CSP_ITEM_MODE_IN_TRANSIT;
	dsmcbe_spu_TransferObject(channel->readerState, preq, obj);

}

//Handles a write request on a direct channel
void dsmcbe_spu_csp_HandleDirectWriteRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_directChannelObject* channel, unsigned int requestId, struct dsmcbe_spu_dataObject* obj, unsigned int allowBufferResponse)
{
	if (channel->poisoned)
	{
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_CSP_CHANNEL_POISONED_RESPONSE, requestId, 0, 0);
		return;
	}

	//printf(WHERESTR "Context: %u, Handling direct write request for channel %d\n", WHEREARG, (unsigned int)state->context, channel->id);
	obj->id = channel->id;

	if (channel->writerRequestIds->count == channel->writerRequestIds->size)
	{
		REPORT_ERROR2("Invalid use of optimized one2one channel %d", channel->id);
		exit(-1);
	}

	obj->id = channel->id;

	if (channel->readerState == state)
	{
		//We are on the same SPE, so the request must have crossed the setup, re-direct to SPE
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_CSP_CHANNEL_WRITE_REQUEST, requestId, (unsigned int)obj->LS, obj->size);
	}
	else
	{

		if (obj->mode == CSP_ITEM_MODE_IN_TRANSIT && obj->flushPreq != NULL) {
			REPORT_ERROR("Got a direct write request on an item in transit, and another package was already registered for flush");
			exit(-1);
		}

		//Record the request
		void* writerEA = spe_ls_area_get(channel->writerState->context) + (unsigned int)obj->LS;
		dsmcbe_ringbuffer_push(channel->writerRequestIds, (void*)requestId);
		dsmcbe_ringbuffer_push(channel->writerDataEAs, writerEA);
		dsmcbe_ringbuffer_push(channel->dataObjs, obj);

		//If the channel is buffered, respond to the writer immediately
		if (allowBufferResponse && channel->writerRequestIds->count != channel->writerRequestIds->size)
			dsmcbe_spu_SendMessagesToSPU(channel->writerState, PACKAGE_CSP_CHANNEL_WRITE_RESPONSE, requestId, 0, 0);

		//printf(WHERESTR "Direct write request for channel %d is INTER SPE, writerLS: %u, buffer->count: %u\n", WHEREARG, channel->id, (unsigned int)obj->LS, channel->writerRequestIds->count);

		if (obj->mode == CSP_ITEM_MODE_IN_TRANSIT)
		{
			obj->flushPreq = dsmcbe_spu_new_PendingRequest(state, requestId, PACKAGE_SPU_CSP_DIRECT_WRITE_REQUEST, obj->id, obj, 0, TRUE);
			//printf(WHERESTR "Created a flushPreq with code %d\n", WHEREARG, obj->flushPreq->operation);
			return;
		}

		//Allocate space on target SPE, if not already done
		if (channel->writerDataEAs->count == 1)
		{
			if (channel->readerDataEA != NULL)
			{
				REPORT_ERROR2("The reader EA was not null for channel %d", channel->id);
				exit(-1);
			}

			//printf(WHERESTR "Writer is allocating space on reader SPE, channel %d, obj->id: %d\n", WHEREARG, channel->id, obj->id);
			dsmcbe_spu_csp_PreTransferItem(channel, obj);
		}
		else
		{
			//printf(WHERESTR "Channel %d already had a transfered item, waiting with next transfer\n", WHEREARG, channel->id);
		}

	}
}

//Handles a read request on a direct channel
void dsmcbe_spu_csp_HandleDirectReadRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_directChannelObject* channel, unsigned int requestId)
{
	if (channel->poisoned && channel->writerRequestIds->count == 0)
	{
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_CSP_CHANNEL_POISONED_RESPONSE, requestId, 0, 0);
		return;
	}

	//printf(WHERESTR "Handling direct write request for channel %d\n", WHEREARG, channel->id);

	channel->readerRequestId = requestId;

	if (channel->writerRequestIds->count == 0)
	{
		//printf(WHERESTR "Read request for channel %d, write is not ready\n", WHEREARG, channel->id);
		//The data is not yet ready, just wait
		return;
	}

	struct dsmcbe_spu_dataObject* obj = (struct dsmcbe_spu_dataObject*)dsmcbe_ringbuffer_peek(channel->dataObjs);
	void* writerEA = dsmcbe_ringbuffer_peek(channel->writerDataEAs);

	//If the transfer is completed, just respond
	if (obj->mode == CSP_ITEM_MODE_SENT)
	{
		if (channel->writerState == state)
		{
			//printf(WHERESTR "Read request, writer is ready, exchanging intra SPE pointers, channel %d\n", WHEREARG, channel->id);
			dsmcbe_spu_SendMessagesToSPU(channel->readerState, PACKAGE_CSP_CHANNEL_READ_RESPONSE, requestId, (unsigned int)writerEA - (unsigned int)spe_ls_area_get(state->context), obj->size);
		}
		else
		{
			//printf(WHERESTR "Read request, writer is ready, exchanging INTER SPE pointers, channel %d\n", WHEREARG, channel->id);

			//Remove the object from the writers address space
			void* writerLS = writerEA - (unsigned int)spe_ls_area_get(channel->writerState->context);
			g_hash_table_remove(channel->writerState->csp_active_items, writerLS);
			dsmcbe_spu_memory_free(channel->writerState->map, writerLS);
			dsmcbe_spu_ManageDelayedAllocation(channel->writerState);

			//Insert the object in the readers address space
			void* readerLS = channel->readerDataEA - (unsigned int)spe_ls_area_get(channel->readerState->context);
			g_hash_table_insert(channel->readerState->csp_active_items, readerLS, obj);
			obj->EA = NULL;

			dsmcbe_spu_SendMessagesToSPU(channel->readerState, PACKAGE_CSP_CHANNEL_READ_RESPONSE, requestId, (unsigned int)readerLS, obj->size);
		}

		obj->mode = CSP_ITEM_MODE_IN_USE;

		//If this response pops up the last blocked item, notify it
		if (channel->writerRequestIds->count == channel->writerRequestIds->size)
			dsmcbe_spu_SendMessagesToSPU(channel->writerState, PACKAGE_CSP_CHANNEL_WRITE_RESPONSE, (unsigned int)dsmcbe_ringbuffer_peek_nth(channel->writerRequestIds, channel->writerRequestIds->count - 1), 0, 0);

		//Remove the entry
		dsmcbe_ringbuffer_pop(channel->writerRequestIds);
		dsmcbe_ringbuffer_pop(channel->writerDataEAs);
		dsmcbe_ringbuffer_pop(channel->dataObjs);

		channel->readerRequestId = UINT_MAX;
		channel->readerDataEA = NULL;

		//If there is another write request pending, start the transfer now
		if (channel->writerRequestIds->count != 0)
		{
			//printf(WHERESTR "Channel %d was claimed pre-transferring next item in queue, count: %d\n", WHEREARG, channel->id, channel->writerRequestIds->count);
			dsmcbe_spu_csp_PreTransferItem(channel, dsmcbe_ringbuffer_peek(channel->dataObjs));
		}
	}
	else
	{
		//The transfer should be in transit of sorts by now
		if (obj->mode != CSP_ITEM_MODE_IN_TRANSIT)
		{
			//REPORT_ERROR2("Item is not in transit as expected, hopefully it is awaiting a malloc, mode: %d", obj->mode);
		}
		else
		{
			//printf(WHERESTR "Read request, writer is currently transferring data, channel %d\n", WHEREARG, channel->id);
		}
	}
}

//Handles a completed direct transfer
void dsmcbe_spu_csp_HandleDirectTransferCompleted(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq)
{
	struct dsmcbe_spu_directChannelObject* channel = (struct dsmcbe_spu_directChannelObject*)g_hash_table_lookup(state->csp_direct_channels, (void*)preq->dataObj->id);
	if (channel == NULL)
	{
		REPORT_ERROR("Direct channel is missing");
		exit(-1);
	}

	//printf(WHERESTR "Context %u, Transfer of object on channel %d completed\n", WHEREARG, (unsigned int)state->context, channel->id);
	preq->dataObj->mode = CSP_ITEM_MODE_SENT;

	//The item is now transfered, if the reader is ready, send everything
	if (channel->readerRequestId != UINT_MAX)
	{
		//printf(WHERESTR "Transfer completed and reader ready for channel %d\n", WHEREARG, channel->id);
		dsmcbe_spu_csp_HandleDirectReadRequest(channel->readerState, channel, channel->readerRequestId);
	}
	else
	{
		//printf(WHERESTR "Transfer completed but reader was not ready for channel %d\n", WHEREARG, channel->id);

	}

	preq->dataObj = NULL;
	dsmcbe_spu_free_PendingRequest(preq);
	preq = NULL;
}

#ifdef SPU_STOP_AND_WAIT
int dsmcbe_spu_csp_callback(void* ls_base, unsigned int data_ptr)
{
	data_ptr += 0; //Remove compiler warning

	size_t i;
	for(i = 0; i < dsmcbe_spu_thread_count; i++)
		if (spe_ls_area_get(dsmcbe_spu_states[i].context) == ls_base)
		{
			pthread_mutex_lock(&dsmcbe_spu_states[i].csp_sleep_mutex);

			while(&dsmcbe_spu_states[i].csp_sleep_flag == 0)
				pthread_cond_wait(&dsmcbe_spu_states[i].csp_sleep_cond, &dsmcbe_spu_states[i].csp_sleep_mutex);

			dsmcbe_spu_states[i].csp_sleep_flag = 0;

			pthread_mutex_unlock(&dsmcbe_spu_states[i].csp_sleep_mutex);
			return 0;
		}

	printf(WHERESTR "SPE is stopped for request %d, base: %d, unable to find matching process\n", WHEREARG, data_ptr, (unsigned int)ls_base);
	return 0;
}
#endif

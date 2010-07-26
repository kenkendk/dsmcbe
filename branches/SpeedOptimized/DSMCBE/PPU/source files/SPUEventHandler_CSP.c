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

//This function handles incoming create channel requests from an SPU
void dsmcbe_spu_csp_HandleChannelCreateRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, unsigned int buffersize, unsigned int type)
{
	//Record the ongoing request
	CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_CREATE_REQUEST);

	//Create a matching request for the request coordinator
	struct dsmcbe_cspChannelCreateRequest* req;
	if (dsmcbe_new_cspChannelCreateRequest(&req, id, requestId, buffersize, type) != CSP_CALL_SUCCESS)
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

//This function handles incoming create channel requests from an SPU
void dsmcbe_spu_csp_HandleChannelPoisonRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id)
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
			REPORT_ERROR("Newly allocated pointer was found in csp table");
			exit(-1);
		}
	}

	return ls;
}

//This function handles incoming create item requests from an SPU
void dsmcbe_spu_csp_HandleItemCreateRequest(struct dsmcbe_spu_state* state, unsigned int requestId, unsigned int size)
{
	GUID id = 0;
	struct dsmcbe_spu_pendingRequest*  preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST);
	struct dsmcbe_spu_dataObject* obj = dsmcbe_spu_new_dataObject(0, TRUE, CSP_ITEM_MODE_IN_USE, 0, UINT_MAX, NULL, NULL, UINT_MAX, size, TRUE, FALSE, preq);
	preq->dataObj = obj;

	obj->LS = dsmcbe_spu_csp_attempt_get_pointer(state, preq, size);

	if (obj->LS != NULL)
	{
		g_hash_table_insert(state->csp_active_items, obj->LS, obj);

		dsmcbe_spu_free_PendingRequest(preq);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE, requestId, (unsigned int)obj->LS, size);
	}
}

//This function handles incoming free item requests from an SPU
void dsmcbe_spu_csp_HandleItemFreeRequest(struct dsmcbe_spu_state* state, unsigned int requestId, void* data)
{
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_active_items, data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to release unknown pointer: %d", (unsigned int)data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

	if (obj->mode != CSP_ITEM_MODE_IN_USE)
	{
		REPORT_ERROR2("Unable to free item, as it is in an invalid state, %d", obj->mode);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

	FREE(obj);
	g_hash_table_remove(state->csp_active_items, data);
	dsmcbe_spu_memory_free(state->map, data);
	dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE, requestId, 0, 0);

	dsmcbe_spu_ManageDelayedAllocation(state);
}

//This function handles incoming channel read requests from an SPU
void dsmcbe_spu_csp_HandleChannelReadRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id)
{
	//Record the ongoing request
	CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_READ_REQUEST)

	//Create a matching request for the request coordinator
	struct dsmcbe_cspChannelReadRequest* req;
	if (dsmcbe_new_cspChannelReadRequest_single(&req, id, requestId) != CSP_CALL_SUCCESS)
	{
		printf("Error?\n");
		exit(-1);
	}

	//Forward the request
	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}

//This function handles incoming channel read requests from an SPU
void dsmcbe_spu_csp_HandleChannelReadRequestAlt(struct dsmcbe_spu_state* state, unsigned int requestId, void* guids, unsigned int count, void* channelId, unsigned int mode)
{
	GUID id = CSP_SKIP_GUARD;

	//Record the ongoing request
	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST);
	if (channelId != NULL)
		preq->channelPointer = spe_ls_area_get(state->context) + (unsigned int)channelId;

	//Create a matching request for the request coordinator
	struct dsmcbe_cspChannelReadRequest* req;
	if (dsmcbe_new_cspChannelReadRequest_multiple(&req, requestId, mode, spe_ls_area_get(state->context) + (unsigned int)guids, count) != CSP_CALL_SUCCESS)
		exit(-1);

	//Forward the request
	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}


//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelReadResponse(struct dsmcbe_spu_state* state, void* package)
{
	struct dsmcbe_cspChannelReadResponse* resp = (struct dsmcbe_cspChannelReadResponse*)package;
	unsigned int requestId = resp->requestID;

	if (resp->packageCode != PACKAGE_CSP_CHANNEL_READ_RESPONSE)
	{
		REPORT_ERROR("Got invalid packageCode for readResponse");
	}

	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);

	if (resp->speId == (unsigned int)state)
	{
		//Special case, we have communicated with ourselves
		struct dsmcbe_spu_dataObject* oldObj = g_hash_table_lookup(state->csp_inactive_items, resp->data);
		if (oldObj == NULL)
		{
			REPORT_ERROR("Broḱen self-transfer");
			exit(-1);
		}

		if (oldObj->mode == CSP_ITEM_MODE_TRANSFERED)
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
					*((unsigned int*)preq->channelPointer) = preq->objId;
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

	struct dsmcbe_spu_dataObject* obj = dsmcbe_spu_new_dataObject(resp->channelId, TRUE, CSP_ITEM_MODE_IN_USE, 0, UINT_MAX, ((struct dsmcbe_cspChannelReadResponse*)resp)->data, NULL, UINT_MAX, ((struct dsmcbe_cspChannelReadResponse*)resp)->size, TRUE, TRUE, preq);

	obj->mode = CSP_ITEM_MODE_IN_USE;
	preq->dataObj = obj;
	preq->objId = resp->channelId;
	if (resp->speId != 0)
	{
		preq->transferHandler = resp;
		obj->EA = NULL; //This is not a valid pointer
	}
	else
	{
		preq->transferHandler = NULL;
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

	if (preq->transferHandler == NULL)
		FREE(resp);
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

	struct dsmcbe_transferRequest* req = dsmcbe_new_transferRequest(resp->requestID, &state->inMutex, cond, &state->inQueue, resp->data, spe_ls_area_get(state->context)  + (unsigned int)preq->dataObj->LS);
	dsmcbe_rc_SendMessage(resp->transferManager, req);
	FREE(resp);

	preq->transferHandler = NULL;
}

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, void* data)
{
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_active_items, data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to use unknown pointer for write: %d", (unsigned int)data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

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

	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_WRITE_REQUEST);

	struct dsmcbe_cspChannelWriteRequest* req;
	if(dsmcbe_new_cspChannelWriteRequest_single(&req, id, requestId, (void*)obj->csp_seq_no, obj->size, (unsigned int)state, dsmcbe_spu_createTransferManager(state)) != CSP_CALL_SUCCESS)
		exit(-1);

	preq->transferHandler = req->transferManager;
	preq->dataObj = obj;

	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);

#endif
}

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequestAlt(struct dsmcbe_spu_state* state, unsigned int requestId, void* data, void* guids, unsigned int count, unsigned int mode)
{
	GUID id = CSP_SKIP_GUARD;

	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_active_items, data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to use unknown pointer for write: %d", (unsigned int)data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

#ifdef SPE_CSP_CHANNEL_EAGER_TRANSFER

	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST);
	preq->dataObj = obj;
	obj->preq = preq;
	obj->isDMAToSPU = FALSE;

	if (obj->EA == NULL)
		obj->EA = MALLOC_ALIGN(ALIGNED_SIZE(obj->size), 7);

	if (dsmcbe_new_cspChannelWriteRequest_multiple((struct dsmcbe_cspChannelWriteRequest**)&(preq->channelPointer), requestId, mode, spe_ls_area_get(state->context) + (unsigned int)guids, count, obj->size, obj->EA, 0, NULL) != CSP_CALL_SUCCESS)
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
	if (dsmcbe_new_cspChannelWriteRequest_multiple(&req, requestId, mode, spe_ls_area_get(state->context) + (unsigned int)guids, count, obj->size, (void*)obj->csp_seq_no, (unsigned int)state, dsmcbe_spu_createTransferManager(state)) != CSP_CALL_SUCCESS)
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
		REPORT_ERROR("Got unmatched response");
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

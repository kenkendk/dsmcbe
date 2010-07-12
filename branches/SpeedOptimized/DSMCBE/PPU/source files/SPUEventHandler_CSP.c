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
 *  Reader needs to allocate space and transfer data
 *
 * Scenario 4, reader is PPU, writer is SPU
 *   Reader needs to allocate space
 *   Writer needs to transfer data, and free the pointer, then notify the reader
 *
 * To handle these scenarios correctly, the PACKAGE_READ_RESPONSE has a flag indicating
 * if the data is located on the SPU.
 *
 *
 */


#include <dsmcbe_csp.h>
#include <dsmcbe_csp_initializers.h>
#include <SPUEventHandler.h>
#include <SPUEventHandler_shared.h>
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
		if (dsmcbe_spu_EstimatePendingReleaseSize(state) == 0)
		{
			dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, preq->requestId, 0, 0);
			if (preq->dataObj->LS != NULL)
				g_hash_table_remove(state->csp_items, preq->dataObj->LS);
			FREE(preq->dataObj);
			dsmcbe_spu_free_PendingRequest(preq);

		}
		else
		{
			//No more space, wait until we get some
			if (g_queue_find(state->releaseWaiters, preq) == NULL)
				g_queue_push_tail(state->releaseWaiters, preq);
		}
	}
	else
	{
		if (g_hash_table_lookup(state->csp_items, ls) != NULL)
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
	struct dsmcbe_spu_pendingRequest*  preq = CREATE_PENDING_REQUEST(PACKAGE_CREATE_REQUEST)
	struct dsmcbe_spu_dataObject* obj = dsmcbe_spu_new_dataObject(0, TRUE, 0, 0, UINT_MAX, NULL, NULL, UINT_MAX, size, TRUE, FALSE, preq);
	preq->dataObj = obj;

	obj->LS = dsmcbe_spu_csp_attempt_get_pointer(state, preq, size);

	if (obj->LS != NULL)
	{
		g_hash_table_insert(state->csp_items, obj->LS, obj);

		dsmcbe_spu_free_PendingRequest(preq);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE, requestId, (unsigned int)obj->LS, size);
	}
}

//This function handles incoming free item requests from an SPU
void dsmcbe_spu_csp_HandleItemFreeRequest(struct dsmcbe_spu_state* state, unsigned int requestId, void* data)
{
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_items, data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to release unknown pointer: %d", (unsigned int)data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

	FREE(obj);
	g_hash_table_remove(state->csp_items, data);
	dsmcbe_spu_memory_free(state->map, data);
	dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE, requestId, 0, 0);

	//TODO: Handling releaseWaiters
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
void dsmcbe_spu_csp_HandleChannelReadResponse(struct dsmcbe_spu_state* state, void* resp)
{
	unsigned int requestId = ((struct dsmcbe_cspChannelReadResponse*)resp)->requestID;

	if (((struct dsmcbe_cspChannelReadResponse*)resp)->packageCode != PACKAGE_CSP_CHANNEL_READ_RESPONSE)
	{
		REPORT_ERROR("Got invalid packageCode for readResponse");
	}

	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);
	struct dsmcbe_spu_dataObject* obj = dsmcbe_spu_new_dataObject(((struct dsmcbe_cspChannelReadResponse*)resp)->channelId, TRUE, 0, 0, UINT_MAX, ((struct dsmcbe_cspChannelReadResponse*)resp)->data, NULL, UINT_MAX, ((struct dsmcbe_cspChannelReadResponse*)resp)->size, TRUE, TRUE, preq);

	preq->dataObj = obj;
	preq->objId = ((struct dsmcbe_cspChannelReadResponse*)resp)->channelId;

	obj->LS = dsmcbe_spu_csp_attempt_get_pointer(state, preq, obj->size);

	if (obj->LS != NULL)
	{
		g_hash_table_insert(state->csp_items, obj->LS, obj);
		dsmcbe_spu_TransferObject(state, preq, obj);
	}
}

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, void* data)
{
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_items, data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to use unknown pointer for write: %d", (unsigned int)data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

	if (obj->EA == NULL)
		obj->EA = MALLOC_ALIGN(obj->size, 7);

	//Record the ongoing request
	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_CSP_CHANNEL_WRITE_REQUEST);
	preq->dataObj = obj;
	obj->preq = preq;
	obj->isDMAToSPU = FALSE;

	dsmcbe_spu_TransferObject(state, preq, obj);
}

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequestAlt(struct dsmcbe_spu_state* state, unsigned int requestId, void* data, void* guids, unsigned int count, unsigned int mode)
{
	GUID id = CSP_SKIP_GUARD;

	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->csp_items, data);
	if (obj == NULL)
	{
		REPORT_ERROR2("Attempted to use unknown pointer for write: %d", (unsigned int)data);
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_NACK, requestId, 0, 0);
		return;
	}

	//Record the ongoing request
	struct dsmcbe_spu_pendingRequest* preq = CREATE_PENDING_REQUEST(PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST);
	preq->dataObj = obj;
	obj->preq = preq;
	obj->isDMAToSPU = FALSE;

	if (obj->EA == NULL)
		obj->EA = MALLOC_ALIGN(obj->size, 7);

	if (dsmcbe_new_cspChannelWriteRequest_multiple((struct dsmcbe_cspChannelWriteRequest**)&(preq->channelPointer), requestId, mode, spe_ls_area_get(state->context) + (unsigned int)guids, count, obj->size, obj->EA, TRUE) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_spu_TransferObject(state, preq, obj);
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

	if (preq->dataObj == NULL)
	{
		REPORT_ERROR("Got a write response, but found no data");
	}

	if (((struct dsmcbe_createRequest*)resp)->packageCode != PACKAGE_CSP_CHANNEL_WRITE_RESPONSE)
	{
		REPORT_ERROR("Got a write response, with invalid package code");
	}

	//Data is now delivered correctly, free the local space
	if (preq->dataObj != NULL)
	{
		if (preq->dataObj->LS != NULL)
		{
			g_hash_table_remove(state->csp_items, preq->dataObj->LS);
			dsmcbe_spu_memory_free(state->map, preq->dataObj->LS);
			preq->dataObj->LS = NULL;
		}

		preq->dataObj->preq = NULL;
		FREE(preq->dataObj);
		preq->dataObj = NULL;
	}

	dsmcbe_spu_SendMessagesToSPU(state, preq->operation == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST ? PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE : PACKAGE_CSP_CHANNEL_WRITE_RESPONSE, requestId, ((struct dsmcbe_cspChannelWriteResponse*)resp)->channelId, 0);

	dsmcbe_spu_free_PendingRequest(preq);
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

	if (preq->operation == PACKAGE_CSP_CHANNEL_WRITE_REQUEST || preq->operation == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST)
	{
		//If we get a NACK, Posion or skip here, we have not de-registered the object,
		// so it is still in csp_items, but we don't need the EA pointer anymore,
		// but we keep it, to avoid another malloc call
		if (preq->dataObj != NULL && preq->dataObj->EA != NULL)
		{
			FREE_ALIGN(preq->dataObj->EA);
			preq->dataObj->EA = NULL;
		}
	}

	dsmcbe_spu_SendMessagesToSPU(state, ((struct dsmcbe_createRequest*)resp)->packageCode, requestId, 0, 0);

	dsmcbe_spu_free_PendingRequest(preq);
}


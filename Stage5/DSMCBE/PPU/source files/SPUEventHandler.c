#include <pthread.h>
#include <glib/ghash.h>
#include <glib/gqueue.h>

#include "../header files/SPU_MemoryAllocator.h"
#include "../header files/SPU_MemoryAllocator_Shared.h"
#include "../header files/SPUEventHandler.h"
#include "../header files/RequestCoordinator.h"
#include "../../common/debug.h"
#include "../../common/datapackages.h"

#define ALIGNED_SIZE(x) (x + ((16 - x) % 16))

//The number of avalible DMA group ID's
//NOTE: On the PPU this is 0-15, NOT 0-31 as on the SPU! 
#define MAX_DMA_GROUPID 16

//This is the lowest number a release request can have.
//Make sure that this is larger than the MAX_PENDING_REQUESTS number defined on the SPU
#define RELEASE_NUMBER_BASE 2000

//The number of pending release requests allowed
#define MAX_PENDING_RELEASE_REQUESTS 500

//This object represents an item in on the SPU
struct spu_dataObject
{
	//The object GUID
	GUID id;
	//The current mode, either READ or WRITE
	unsigned int mode;
	//The number of times this object is acquired (only larger than 1 if mode == READ)
	unsigned int count;
	//The DMAGroupID, or UINT_MAX if there are no active transfers
	unsigned int DMAId;
	//A pointer to the object in main memory
	void* EA;
	//A pointer to the object in the SPU LS
	void* LS;
	//The id of the unanswered invalidate request.
	//If this value is not UINT_MAX, the object is invalid, 
	//and will be disposed as soon as it is released
	unsigned int invalidateId;
	//The size of the object
	unsigned long size;
};

//This structure represents a request that is not yet completed
struct spu_pendingRequest
{
	//The requestId
	unsigned int requestId;
	//The operation, either:
	// PACKAGE_CREATE_REQUEST, 
	// PACKAGE_ACQUIRE_READ_REQUEST, 
	// PACKAGE_ACQUIRE_WRITE_REQUEST,
	// PACKAGE_ACQUIRE_RELEASE_REQUEST,
	unsigned int operation;
	//The dataobject involved, may be null if the object is not yet created on the SPU
	GUID objId;
};

struct SPU_RC_Data
{
	//This event is signaled when data is comming from the request coordinator
	pthread_cond_t event;
	//This mutex protects the queue and the event
	pthread_mutex_t mutex;
	//This is the list of responses from the request coordinator
	GQueue* queue;
};

struct SPU_State
{
	//This is a list of all allocated objects on the SPU, key is GUID, value is dataObject*
	GHashTable* itemsById;
	//This is a list of all allocated objects on the SPU, key is LS pointer, value is dataObject*
	GHashTable* itemsByPointer;
	//This is a list of all pending requests, key is requestId, value is pendingRequest*
	GHashTable* pendingRequests;
	//This is a list of all active DMA transfers, key is DMAGroupID, value is pendingRequest*
	GHashTable* activeDMATransfers;
	//This is an ordered list of object GUID's, ordered so least recently used object is in front 
	GQueue* agedObjectMap;
	//This is a queue of all messages sent to the SPU.
	//This simulates a mailbox with infinite capacity
	GQueue* mailboxQueue;
	//This is a map of the SPU LS memory
	SPU_Memory_Map* map;
	//This is the SPU context
	spe_context_ptr_t context;
	//This is the next DMA group ID
	unsigned int dmaSeqNo; 
	//This is the next release request id
	unsigned int releaseSeqNo;
	//This is the data for communicating with the request coordinator
	struct SPU_RC_Data* rcdata;
};

struct SPU_State* spu_states;
pthread_t spu_mainthread;
unsigned int spe_thread_count;
unsigned int spu_terminate;

#define PUSH_TO_SPU(state, value) g_queue_push_tail(state->mailboxQueue, (void* )value);
//#define PUSH_TO_SPU(state, value) if (g_queue_get_length(state->mailboxQueue) != 0 || spe_in_mbox_status(state->context) == 0 || spe_in_mbox_write(state->context, &value, 1, SPE_MBOX_ALL_BLOCKING) != 1)  { g_queue_push_tail(state->mailboxQueue, (void*)value); } 

//This function releases all resources reserved for the object, and sends out invalidate responses, if required
void spuhandler_DisposeObject(struct SPU_State* state, struct spu_dataObject* obj)
{
	if (obj == NULL)
	{
		REPORT_ERROR("Tried to dispose a NULL object");
		return;
	}

	if (obj->count != 0)
	{
		REPORT_ERROR("Tried to dispose an object that was still being referenced");
		return;
	}

	printf(WHERESTR "Disposing item %d\n", WHEREARG, obj->id);

	if (obj->invalidateId != UINT_MAX)
		EnqueInvalidateResponse(obj->invalidateId);
	
	
	
	g_hash_table_remove(state->itemsById, (void*)obj->id);
	g_hash_table_remove(state->itemsByPointer, (void*)obj->LS);
	g_queue_remove(state->agedObjectMap, (void*)obj->id);
	spu_memory_free(state->map, obj->LS);
	free(obj);
}

//This function removes all objects from the state
void spuhandler_DisposeAllObject(struct SPU_State* state)
{
	while(!g_queue_is_empty(state->agedObjectMap))
	{
		GUID id = (GUID)g_queue_pop_head(state->agedObjectMap);
		
		struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
		if (obj == NULL) {
			REPORT_ERROR("An item was in the age map, but did not exist!");
		} else
			spuhandler_DisposeObject(state, obj);
	}
	
	if (g_hash_table_size(state->itemsById) != 0)
	{
		REPORT_ERROR("DisposeAll was called but som objects were not released!");
		
		GHashTableIter it;
		
		while (g_hash_table_size(state->itemsById) != 0)
		{
			void* key;
			void* obj;
			
			g_hash_table_iter_init(&it, state->itemsById);
			if (g_hash_table_iter_next(&it, &key, &obj))
				spuhandler_DisposeObject(state, (struct spu_dataObject*)obj);
		}	
	}
}

//This function allocates space for an object.
//If there is not enough space, unused objects are removed until there is enough space.
void* spuhandler_AllocateSpaceForObject(struct SPU_State* state, unsigned long size)
{
	void* temp = NULL;
	size_t i = 0;
	
	size = ALIGNED_SIZE(size);
	
	//If the requested size is larger than the total avalible space, don't discard objects
	if (size > state->map->size * 16)
		return NULL;
	
	//If there is no way the object can fit, skip the initial try
	if (state->map->free_mem >= size)
		temp = spu_memory_malloc(state->map, size);
	
	//While we have not recieved a valid pointer, and there are still objects to discard
	while(temp == NULL && !g_queue_is_empty(state->agedObjectMap))
	{
		unsigned int id = (unsigned int)g_queue_peek_nth(state->agedObjectMap, i);
		struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
		if (obj == NULL)
		{
			REPORT_ERROR("An item was in the age map, but did not exist!");
			g_queue_pop_nth(state->agedObjectMap, i);
		}
		else
		{
			if (obj->count != 0) 
			{
				REPORT_ERROR("An item was in the age map, but was acquired!");
				i++;
			}
			else
			{
				//Remove the object, and try to allocate again
				spuhandler_DisposeObject(state, obj);
				if (state->map->free_mem >= size)
					temp = spu_memory_malloc(state->map, size);
			}
		}
	}
	
	return temp;
}
 
//This function handles incoming acquire requests from an SPU
void spuhandler_HandleAcquireRequest(struct SPU_State* state, unsigned int requestId, GUID id, unsigned int packageCode)
{
	printf(WHERESTR "Handling acquire request\n", WHEREARG);
	
	if (packageCode == PACKAGE_ACQUIRE_REQUEST_READ)
	{
		struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
		
		//TODO: If the transfer is a write, we can transfer anyway.
		//TODO: If the transfer is ongoing, we should not forward the request to the RC
		
		//If the object is present, not acquired or acquired in read mode, there is no pending invalidates
		// and data is not being transfered, we can return the pointer directly
		if (obj != NULL && (obj->count == 0 || obj->mode == ACQUIRE_MODE_READ) && obj->invalidateId == UINT_MAX && obj->DMAId == UINT_MAX)
		{
			obj->count++;
			obj->mode = ACQUIRE_MODE_READ;
			PUSH_TO_SPU(state, requestId);
			PUSH_TO_SPU(state, obj->LS);
			PUSH_TO_SPU(state, obj->size);
			if (obj->count == 1)
				g_queue_remove(state->agedObjectMap, (void*)obj->id);
			return;
		}
	}
	
	struct spu_pendingRequest* preq;
	if ((preq = malloc(sizeof(struct spu_pendingRequest))) == NULL)
		REPORT_ERROR("malloc error");
		
	preq->objId = id;
	preq->operation = packageCode;
	preq->requestId = requestId;
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct acquireRequest* req;
	if ((req = malloc(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	req->packageCode = packageCode;
	req->requestID = requestId;
	req->dataItem = id;

	QueueableItem qi;
	if ((qi = malloc(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("malloc error");
	
	qi->dataRequest = req;
	qi->event = &state->rcdata->event;
	qi->mutex = &state->rcdata->mutex;
	qi->Gqueue = &state->rcdata->queue;

	printf(WHERESTR "Inserting item into queue\n", WHEREARG);
	EnqueItem(qi);
}

//This function handles incoming create requests from an SPU
void spuhandler_HandleCreateRequest(struct SPU_State* state, unsigned int requestId, GUID id, unsigned long size)
{
	if (g_hash_table_lookup(state->itemsById, (void*)id) != NULL)
	{
		PUSH_TO_SPU(state, requestId);
		PUSH_TO_SPU(state, NULL);
		PUSH_TO_SPU(state, 0);
		return;
	}
	
	struct spu_pendingRequest* preq;
	if ((preq = malloc(sizeof(struct spu_pendingRequest))) == NULL)
		REPORT_ERROR("malloc error");
		
	preq->objId = id;
	preq->operation = PACKAGE_CREATE_REQUEST;
	preq->requestId = requestId;
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct createRequest* req;
	if ((req = malloc(sizeof(struct createRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	req->packageCode = PACKAGE_CREATE_REQUEST;
	req->requestID = requestId;
	req->dataItem = id;
	req->dataSize = size;

	QueueableItem qi;
	if ((qi = malloc(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("malloc error");
	
	qi->dataRequest = req;
	qi->event = &state->rcdata->event;
	qi->mutex = &state->rcdata->mutex;
	qi->Gqueue = &state->rcdata->queue;

	printf(WHERESTR "Inserting item into queue\n", WHEREARG);
	EnqueItem(qi);
}

//This function handles an incoming release request from an SPU
void spuhandler_HandleReleaseRequest(struct SPU_State* state, void* data)
{
	printf(WHERESTR "Releasing object @: %d\n", WHEREARG, (unsigned int)data);

	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsByPointer, data);
	if (obj == NULL || obj->count == 0)
	{
		if (obj == NULL) {
			REPORT_ERROR("Attempted to release an object that does not exist");
		} else {
			REPORT_ERROR("Attempted to release an object that was not acquired");
		} 
		return;
	}

	//Read releases are handled locally
	if (obj->mode == ACQUIRE_MODE_READ)
	{
		obj->count--;
		if (obj->count == 0)
		{
			//If this was the last release, check for invalidates, and otherwise register in the age map
			if (obj->invalidateId != UINT_MAX)
				spuhandler_DisposeObject(state, obj);
			else
				g_queue_push_tail(state->agedObjectMap, (void*)obj->id);
		}
	}
	else /*if (obj->mode == ACQUIRE_MODE_WRITE)*/
	{
		//Get a group id, and register the active transfer
		obj->DMAId = NEXT_SEQ_NO(state->dmaSeqNo, MAX_DMA_GROUPID);
		
		struct spu_pendingRequest* req;
		if((req = malloc(sizeof(struct spu_pendingRequest))) == NULL)
			REPORT_ERROR("malloc error");
		
		req->objId = obj->id;
		req->requestId = 0;
		req->operation = PACKAGE_RELEASE_REQUEST;
		
		g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, req);

		//Inititate the DMA transfer		
		spe_mfcio_put(state->context, (unsigned int)obj->LS, obj->EA, ALIGNED_SIZE(obj->size), obj->DMAId, 0, 0);
	}	
}

//This function handles an acquireResponse package from the request coordinator
void spuhandler_HandleAcquireResponse(struct SPU_State* state, struct acquireResponse* data)
{
	printf(WHERESTR "Handling acquire response\n", WHEREARG);
	
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)data->requestID);
	if (preq == NULL)
	{
		REPORT_ERROR("Found response to an unknown request");
		return;
	}
	
	//TODO: If two threads acquire the same object, we cannot respond until the DMA is complete
	
	//Determine if data is already present on the SPU
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
	if (obj == NULL)
	{
		printf(WHERESTR "Allocating space on SPU\n", WHEREARG);
		void* ls = spuhandler_AllocateSpaceForObject(state, data->dataSize);
		if (ls == NULL)
		{
			printf(WHERESTR "Allocating space on SPU gave a NULL pointer\n", WHEREARG);
			PUSH_TO_SPU(state, preq->requestId);
			PUSH_TO_SPU(state, NULL);
			PUSH_TO_SPU(state, 0);
			
			g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
			free(preq);
			return;	
		}

		printf(WHERESTR "Allocating space on SPU succeeded with: %d\n", WHEREARG, (unsigned int)ls);
		
		if ((obj = malloc(sizeof(struct spu_dataObject))) == NULL)
			REPORT_ERROR("malloc error");
			
		obj->count = 1;
		obj->DMAId = NEXT_SEQ_NO(state->dmaSeqNo, MAX_DMA_GROUPID);;
		obj->EA = data->data;
		obj->id = preq->objId;
		obj->invalidateId = UINT_MAX;
		obj->mode = preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
		obj->size = data->dataSize;
		obj->LS = ls;
	
		g_hash_table_insert(state->itemsById, (void*)obj->id, obj);
		printf(WHERESTR "Inserting item with LS: %d\n", WHEREARG, (unsigned int)obj->LS); 
		g_hash_table_insert(state->itemsByPointer, obj->LS, obj);
		g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, preq);
		
		//printf(WHERESTR "Starting DMA transfer, from: %d, to: %d, size: %d, tag: %d\n", WHEREARG, (unsigned int)obj->LS, (unsigned int)obj->EA, (unsigned int)obj->size, obj->DMAId);
		
		//Perform the transfer
		spe_mfcio_get(state->context, (unsigned int)obj->LS, obj->EA, ALIGNED_SIZE(obj->size), obj->DMAId, 0, 0);
	}
	else
	{
		obj->count++;
		obj->mode = preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
		obj->EA = data->data;
		PUSH_TO_SPU(state, preq->requestId);
		PUSH_TO_SPU(state, obj->LS);
		PUSH_TO_SPU(state, obj->size);
		
		g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
		free(preq);	
	}
	
	if (obj->count == 1)
		g_queue_remove(state->agedObjectMap, (void*)obj->id);


}

//This function handles completed DMA transfers
void spuhandler_HandleDMATransferCompleted(struct SPU_State* state, unsigned int groupID)
{
	printf(WHERESTR "Handling completed DMA transfer\n", WHEREARG);
	
	//Get the corresponding request
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->activeDMATransfers, (void*)groupID);
	if (preq == NULL)
	{
		REPORT_ERROR("DMA completed, but was not initiated");
		return;
	}

	g_hash_table_remove(state->activeDMATransfers, (void*)groupID);
	
	//Get the corresponding data object
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
	if (obj == NULL)
	{
		free(preq);
		REPORT_ERROR("DMA was completed, but there was not allocated space?");
		return;
	}
	
	//If the transfer was from PPU to SPU, we can now give the pointer to the SPU
	if (preq->operation != PACKAGE_RELEASE_REQUEST)
	{
		PUSH_TO_SPU(state, preq->requestId);
		PUSH_TO_SPU(state, obj->LS);
		PUSH_TO_SPU(state, obj->size);

		free(preq);
	}
	else
	{
		//Data is transfered from SPU LS to EA, now notify the request coordinator
		
		struct releaseRequest* req;
		if ((req = malloc(sizeof(struct releaseRequest))) == NULL)
			REPORT_ERROR("malloc error");
			
		req->data = obj->EA;
		req->dataItem = obj->id;
		req->dataSize = obj->size;
		req->mode = ACQUIRE_MODE_WRITE;
		req->offset = 0;
		req->packageCode = PACKAGE_RELEASE_REQUEST;
		req->requestID = NEXT_SEQ_NO(state->releaseSeqNo, MAX_PENDING_RELEASE_REQUESTS) + RELEASE_NUMBER_BASE;
		
		g_hash_table_insert(state->pendingRequests, (void*)req->requestID, req);
		
		QueueableItem qi;
		if ((qi = malloc(sizeof(struct QueueableItemStruct))) == NULL)
			REPORT_ERROR("malloc error");
		
		qi->dataRequest = req;
		qi->event = &state->rcdata->event;
		qi->mutex = &state->rcdata->mutex;
		qi->Gqueue = &state->rcdata->queue;
	
		printf(WHERESTR "Inserting item into queue\n", WHEREARG);
		EnqueItem(qi);
	}
}

//This function handles incoming release responses from the request coordinator
void spuhandler_HandleReleaseResponse(struct SPU_State* state, unsigned int requestId)
{
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)requestId);
	if (preq == NULL)
	{
		REPORT_ERROR("Get release response for non initiated request");
		return;
	}
	
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);

	g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
	//free(preq);
	
	if (obj == NULL || obj->count == 0)
	{
		REPORT_ERROR("Release was completed, but the object was not acquired?");
		return;
	}
	
	obj->count--;
	if (obj->count == 0)
	{
		//If this was the last release, check for invalidates, and otherwise register in the age map
		if (obj->invalidateId != UINT_MAX)
			spuhandler_DisposeObject(state, obj);
		else
			g_queue_push_tail(state->agedObjectMap, (void*)obj->id);
	}
}

//This function handles incoming invalidate requests from the request coordinator
void spuhandler_HandleInvalidateRequest(struct SPU_State* state, unsigned int requestId, GUID id)
{
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
	if (obj == NULL)
		EnqueInvalidateResponse(requestId);
	else
	{
		obj->invalidateId = requestId;
		if (obj->count == 0)
			spuhandler_DisposeObject(state, obj);
	}
}


//Reads an processes any incoming mailbox messages
void spuhandler_SPUMailboxReader(struct SPU_State* state)
{
	if (spe_out_intr_mbox_status(state->context) != 0)
	{
		unsigned int packageCode;
		unsigned int requestId = 0;
		GUID id = 0;
		unsigned int size = 0;
		
		spe_out_intr_mbox_read(state->context, &packageCode, 1, SPE_MBOX_ALL_BLOCKING);
		switch(packageCode)
		{
			case PACKAGE_TERMINATE_REQUEST:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spuhandler_DisposeAllObject(state);			
				PUSH_TO_SPU(state, requestId);
				PUSH_TO_SPU(state, PACKAGE_TERMINATE_RESPONSE);
				break;
			case PACKAGE_SPU_MEMORY_SETUP:
				if (state->map != NULL) 
				{
					REPORT_ERROR("Tried to re-initialize SPU memory map");
				} 
				else
				{
					spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					spe_out_intr_mbox_read(state->context, &size, 1, SPE_MBOX_ALL_BLOCKING);
					state->map = spu_memory_create((unsigned int)requestId, size);
				}
				break;
				
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spe_out_intr_mbox_read(state->context, &id, 1, SPE_MBOX_ALL_BLOCKING);
				spuhandler_HandleAcquireRequest(state, requestId, id, packageCode);
				break;
			case PACKAGE_CREATE_REQUEST:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spe_out_intr_mbox_read(state->context, &id, 1, SPE_MBOX_ALL_BLOCKING);
				spe_out_intr_mbox_read(state->context, &size, 1, SPE_MBOX_ALL_BLOCKING);
				spuhandler_HandleCreateRequest(state, requestId, id, size);
				break;
			case PACKAGE_RELEASE_REQUEST:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spuhandler_HandleReleaseRequest(state, (void*)requestId);
				break;
			case PACKAGE_SPU_MEMORY_FREE:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spu_memory_free(state->map, (void*)requestId);
				break;
			case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spe_out_intr_mbox_read(state->context, &size, 1, SPE_MBOX_ALL_BLOCKING);
				printf(WHERESTR "malloc request %d for %d\n", WHEREARG, requestId, size);
				PUSH_TO_SPU(state, requestId);
				PUSH_TO_SPU(state, spuhandler_AllocateSpaceForObject(state, size));
				printf(WHERESTR "malloc request %d for %d, done\n", WHEREARG, requestId, size);
				break;
			default:
				REPORT_ERROR("Unknown package code recieved");			
				break;	
		}
	}
}

//This function reads and handles incomming requests and responses from the request coordinator
void spuhandler_HandleRequestCoordinatorMessages(struct SPU_State* state)
{
	struct acquireResponse* resp = NULL;
	
	while(TRUE)
	{
		pthread_mutex_lock(&state->rcdata->mutex);
		resp = g_queue_pop_head(state->rcdata->queue);
		pthread_mutex_unlock(&state->rcdata->mutex);

		if (resp == NULL)
			return;
			
		printf(WHERESTR "Handling request coordinator message: %s\n", WHEREARG, PACKAGE_NAME(resp->packageCode));			
			
		switch(resp->packageCode)
		{
			case PACKAGE_ACQUIRE_RESPONSE:
				spuhandler_HandleAcquireResponse(state, resp);
				break;
			case PACKAGE_RELEASE_RESPONSE:
				spuhandler_HandleReleaseResponse(state, ((struct releaseResponse*)resp)->requestID);
				break;
			case PACKAGE_INVALIDATE_REQUEST:
				spuhandler_HandleInvalidateRequest(state, ((struct invalidateRequest*)resp)->requestID, ((struct invalidateRequest*)resp)->dataItem);
				break;
			default:
				REPORT_ERROR("Invalid package recieved");
				break;			
		}
		free(resp);
	}
}

//This function handles completion of a DMA event
void spuhandler_HandleDMAEvent(struct SPU_State* state)
{
	//If we are not expecting any complete transfers, return quickly
	if (g_hash_table_size(state->activeDMATransfers) == 0)
		return;
	
	GHashTableIter it;
	void* key;
	void* req;
	
	unsigned int mask = 0;
	
	//Read the current transfer mask
	spe_mfcio_tag_status_read(state->context, 0, SPE_TAG_IMMEDIATE, &mask);
	
	//No action, so quickly return
	if (mask == 0)
		return;

	//See if the any of the completed transfers are in our wait list
	g_hash_table_iter_init(&it, state->activeDMATransfers);
	while(g_hash_table_iter_next(&it, &key, &req))
		if ((mask & (1 << ((unsigned int)key))) != 0)
		{
			spuhandler_HandleDMATransferCompleted(state, ((unsigned int)key));
			
			//Re-initialize the iterator
			g_hash_table_iter_init(&it, state->activeDMATransfers);
		}
}

//This function writes pending data to the spu mailbox while there is room 
void spuhandler_SPUMailboxWriter(struct SPU_State* state)
{
	while (!g_queue_is_empty(state->mailboxQueue) && spe_in_mbox_status(state->context) != 0)	
	{
		//printf(WHERESTR "Sending Mailbox message: %i\n", WHEREARG, (unsigned int)Gspu_mailboxQueues[i]->head->data);
		if (spe_in_mbox_write(state->context, (unsigned int*)&state->mailboxQueue->head->data, 1, SPE_MBOX_ALL_BLOCKING) != 1) {
			REPORT_ERROR("Failed to send message, even though it was blocking!"); 
		} else
			g_queue_pop_head(state->mailboxQueue);
	}
}

//This function repeatedly checks for events relating to the SPU's
void* SPU_MainThread(void* dummy)
{
	size_t i;
	
	while(!spu_terminate)
	{
		for(i = 0; i < spe_thread_count; i++)
		{
			//For each SPU, just repeat this
			spuhandler_SPUMailboxReader(&spu_states[i]);
			spuhandler_HandleRequestCoordinatorMessages(&spu_states[i]);
			spuhandler_HandleDMAEvent(&spu_states[i]);
			spuhandler_SPUMailboxWriter(&spu_states[i]);
		}
	}
	return dummy;
}

//This function sets up the SPU event handler
void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count)
{
	pthread_attr_t attr;
	size_t i;
	
	spe_thread_count = thread_count;
	spu_terminate = TRUE;
	
	if (thread_count == 0)
		return;
	
	spu_terminate = FALSE;
	
	if ((spu_states = malloc(sizeof(struct SPU_State) * thread_count)) == NULL)
		REPORT_ERROR("malloc error");
	
	for(i = 0; i < thread_count; i++)
	{
		spu_states[i].activeDMATransfers = g_hash_table_new(NULL, NULL);
		spu_states[i].agedObjectMap = g_queue_new();
		spu_states[i].context = threads[i];
		spu_states[i].dmaSeqNo = 0;
		spu_states[i].itemsById = g_hash_table_new(NULL, NULL);
		spu_states[i].itemsByPointer = g_hash_table_new(NULL, NULL);
		spu_states[i].mailboxQueue = g_queue_new();
		spu_states[i].map = NULL;
		spu_states[i].pendingRequests = g_hash_table_new(NULL, NULL);
		if((spu_states[i].rcdata = malloc(sizeof(struct SPU_RC_Data))) == NULL)
			REPORT_ERROR("malloc error");
		
		pthread_mutex_init(&spu_states[i].rcdata->mutex, NULL);
		pthread_cond_init(&spu_states[i].rcdata->event, NULL);
		spu_states[i].rcdata->queue = g_queue_new();
		
		RegisterInvalidateSubscriber(&spu_states[i].rcdata->mutex, &spu_states[i].rcdata->event, &spu_states[i].rcdata->queue);		
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	
	pthread_create(&spu_mainthread, &attr, SPU_MainThread, NULL);
}

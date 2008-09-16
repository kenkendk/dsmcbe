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

//Each call to wait should not return more than this many events
#define SPE_MAX_EVENT_COUNT 100

//This number is the max number of bytes the PPU can transfer without contacting the SPU
#define SPE_DMA_MAX_TRANSFERSIZE (16 * 1024)

//This number is the min number of bytes the PPU will transfer over a DMA, smaller requests use an alternate transfer method
//Set to zero to disable alternate transfer methods
#define SPE_DMA_MIN_TRANSFERSIZE 16

//Thise define determines if small transfers should use MMIO or mailbox for small message
#define SPE_DMA_MIN_USE_MMIO

//This structure represents an item on the SPU
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
	
	//This is a flag that determines if the writebuffer is ready for transfer
	unsigned int writebuffer_ready;
	
	//This is a flag that indicates if the transfer i going from the PPU to the SPU
	unsigned int isDMAToSPU;
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
	//This is the number of DMA requests that must complete before the entire transaction is done
	unsigned int DMAcount;
};

struct SPU_State
{
	//This is a flag indicating that the SPU thread has terminated
	unsigned int terminated;
	
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
	
	//This is a list of acquireRespons packages that cannot be forwarded until a 
	//releaseResponse arrives and free's memory
	GQueue* releaseWaiters;
	
	//This is the list of responses from the request coordinator
	GQueue* queue;
};

//This is an array of SPU states
struct SPU_State* spu_states;

//This is the SPU main thread
pthread_t spu_mainthread;

//This is the number of SPU's allocated 
unsigned int spe_thread_count;

//This is the flag that is used to terminate the SPU event handler 
volatile unsigned int spu_terminate;

//This mutex protects the queue and the event
pthread_mutex_t spu_rq_mutex;

#ifdef EVENT_BASED
//This event is signaled when data is comming from the request coordinator
pthread_cond_t spu_rq_event;

//This thread monitors events from the SPU
pthread_t spu_event_watcher;

//This is the SPE event handler
spe_event_handler_ptr_t spu_event_handler;
	
//This is the registered SPE events
spe_event_unit_t* registered_events;

#endif


//#define PUSH_TO_SPU(state, value) g_queue_push_tail(state->mailboxQueue, (void* )value);

unsigned int spuhandler_temp_mbox_value;
#define PUSH_TO_SPU(state, value) spuhandler_temp_mbox_value = (unsigned int)value; if (g_queue_get_length(state->mailboxQueue) != 0 || spe_in_mbox_status(state->context) == 0 || spe_in_mbox_write(state->context, &spuhandler_temp_mbox_value, 1, SPE_MBOX_ALL_BLOCKING) != 1)  { g_queue_push_tail(state->mailboxQueue, (void*)spuhandler_temp_mbox_value); } 

//Declarations for functions that have interdependencies
void spuhandler_HandleObjectRelease(struct SPU_State* state, struct spu_dataObject* obj);
void spuhandler_HandleDMATransferCompleted(struct SPU_State* state, unsigned int groupID);


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

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Disposing item %d\n", WHEREARG, obj->id);
#endif

	if (obj->invalidateId != UINT_MAX)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Disposing item %d and sending invalidate\n", WHEREARG, obj->id);
#endif
		EnqueInvalidateResponse(obj->invalidateId);
		obj->invalidateId = UINT_MAX;
	}

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Disposing item %d\n", WHEREARG, obj->id);
#endif
	
	g_hash_table_remove(state->itemsById, (void*)obj->id);
	g_hash_table_remove(state->itemsByPointer, (void*)obj->LS);
	g_queue_remove(state->agedObjectMap, (void*)obj->id);
	spu_memory_free(state->map, obj->LS);
	free(obj);
	
	if (state->terminated != UINT_MAX && g_hash_table_size(state->itemsById) == 0)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Signaling termination to SPU %d\n", WHEREARG, obj->id);
#endif
		PUSH_TO_SPU(state, state->terminated);
		PUSH_TO_SPU(state, PACKAGE_TERMINATE_RESPONSE);
	}

}

//This function creates and forwards a message to the request coordinator
void spuhandler_SendRequestCoordinatorMessage(struct SPU_State* state, void* req)
{
	QueueableItem qi;
	if ((qi = malloc(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("malloc error");
	
	qi->dataRequest = req;
#ifdef EVENT_BASED		
		qi->event = &spu_rq_event;
#else
		qi->event = NULL;
#endif
		qi->mutex = &spu_rq_mutex;
		qi->Gqueue = &state->queue;
		
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Inserting item into request coordinator queue\n", WHEREARG);
#endif
		EnqueItem(qi);
}


//This function removes all objects from the state
void spuhandler_DisposeAllObject(struct SPU_State* state)
{
	if (state->terminated != UINT_MAX && g_queue_is_empty(state->agedObjectMap) && g_hash_table_size(state->itemsById) == 0)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Signaling termination to SPU %d\n", WHEREARG, state->terminated);
#endif
		PUSH_TO_SPU(state, state->terminated);
		PUSH_TO_SPU(state, PACKAGE_TERMINATE_RESPONSE);
		return;
	}
	
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
		//This can happen, because the release call is async
		//REPORT_ERROR("DisposeAll was called but some objects were not released!");
		
		/*GHashTableIter it;

		void* key;
		struct spu_dataObject* obj;
		
		g_hash_table_iter_init(&it, state->itemsById);
		while(g_hash_table_iter_next(&it, &key, (void*)&obj))
			printf(WHERESTR "Found item: %d with count: %d", WHEREARG, obj->id, obj->count); 
	
		while (g_hash_table_size(state->itemsById) != 0)
		{
			g_hash_table_iter_init(&it, state->itemsById);
			if (g_hash_table_iter_next(&it, &key, (void*)&obj))
			{
				obj->count = 0;
				spuhandler_DisposeObject(state, (struct spu_dataObject*)obj);
			}
			
			if (g_hash_table_remove(state->itemsById, key))
				REPORT_ERROR("An object in the table was attempted disposed, but failed");
		}*/
	}
}

//This function allocates space for an object.
//If there is not enough space, unused objects are removed until there is enough space.
void* spuhandler_AllocateSpaceForObject(struct SPU_State* state, unsigned long size)
{
	void* temp = NULL;
	size_t i = 0;

	struct spu_dataObject* obj;
	unsigned int id;	
	size = ALIGNED_SIZE(size);
	
	//If the requested size is larger than the total avalible space, don't discard objects
	if (size > state->map->totalmem)
		return NULL;
	
	//If there is no way the object can fit, skip the initial try
	if (state->map->free_mem >= size)
		temp = spu_memory_malloc(state->map, size);

	//Try to remove a object of the same size as the object
	// we want to alloc.
	size_t j;
	size_t lc = g_queue_get_length(state->agedObjectMap);
	if (lc < 10)
	{
		for(j = 0; j < lc; j++)
		{
			id = (unsigned int)g_queue_peek_nth(state->agedObjectMap, j);
			obj = g_hash_table_lookup(state->itemsById, (void*)id);
			if(obj->size == size && obj->count == 0)
			{
				//Remove the object, and try to allocate again
				//printf(WHERESTR "Disposing object: %d\n", WHEREARG, obj->id);
				spuhandler_DisposeObject(state, obj);
				if (state->map->free_mem >= size)
					temp = spu_memory_malloc(state->map, size);
				lc = g_queue_get_length(state->agedObjectMap);			
			}		
		}
	}	

	//While we have not recieved a valid pointer, and there are still objects to discard
	while(temp == NULL && !g_queue_is_empty(state->agedObjectMap))
	{
		id = (unsigned int)g_queue_peek_nth(state->agedObjectMap, i);
		obj = g_hash_table_lookup(state->itemsById, (void*)id);
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
				//printf(WHERESTR "Disposing object: %d\n", WHEREARG, obj->id);
				spuhandler_DisposeObject(state, obj);
				if (state->map->free_mem >= size)
					temp = spu_memory_malloc(state->map, size);
			}
		}
	}

#ifdef DEBUG_FRAGMENTATION	
	if (temp == NULL)
	{
		GHashTableIter iter;
		g_hash_table_iter_init(&iter, state->itemsById);
		GUID key;
		struct spu_dataObject* value;
		
		while(g_hash_table_iter_next(&iter, (void*)&key, (void*)&value))
		{
			//printf(WHERESTR "Found item with id: %d and count %d\n", WHEREARG, key, value->count);
			if (value->count == 0)
				g_queue_push_tail(state->agedObjectMap, (void*)key);
		}
				
		if (!g_queue_is_empty(state->agedObjectMap))
		{
			REPORT_ERROR("Extra unused objects were found outside the agedObjectMap");			
			return spuhandler_AllocateSpaceForObject(state, size);
		}
		else
			return NULL;
	}
#endif
	
	return temp;
}
 
//This function handles incoming acquire requests from an SPU
void spuhandler_HandleAcquireRequest(struct SPU_State* state, unsigned int requestId, GUID id, unsigned int packageCode)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif
	
	if (packageCode == PACKAGE_ACQUIRE_REQUEST_READ && g_queue_is_empty(state->releaseWaiters))
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

	spuhandler_SendRequestCoordinatorMessage(state, req);
}

//This function initiates a DMA transfer to or from the SPU
void spuhandler_InitiateDMATransfer(struct SPU_State* state, unsigned int toSPU, unsigned int EA, unsigned int LS, unsigned int size, unsigned int groupId)
{
	if (size > SPE_DMA_MIN_TRANSFERSIZE)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Initiating DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, size, groupId);
#endif

		if (toSPU)
		{
			if (spe_mfcio_get(state->context, LS, (void*)EA, ALIGNED_SIZE(size), groupId, 0, 0) != 0)
			{
				REPORT_ERROR("DMA transfer from EA to LS failed");
				fprintf(stderr, WHERESTR "Initiating DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, size, groupId);
			}
		}
		else
		{
			if (spe_mfcio_put(state->context, LS, (void*)EA, ALIGNED_SIZE(size), groupId, 0, 0) != 0)
			{
				REPORT_ERROR("DMA transfer from LS to EA failed");
				fprintf(stderr, WHERESTR "Initiating DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, size, groupId);
			}
		}
	}
	else
	{

#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Initiating simulated DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, size, groupId);
#endif
		
#ifdef SPE_DMA_MIN_USE_MMIO
		if (toSPU)
		{
			void* spu_ls = spe_ls_area_get(state->context) + LS;
			memcpy(spu_ls, (void*)EA, size);
		}
		else
		{
			void* spu_ls = spe_ls_area_get(state->context) + LS;
			memcpy((void*)EA, spu_ls, size);
		}
		
		spuhandler_HandleDMATransferCompleted(state, groupId);
			
#else
		//We can only send 4 byte messages :(
		size_t count = (size + 3) / 4;
		
		if (toSPU)
		{
			size_t i;
						
			PUSH_TO_SPU(state, UINT_MAX);
			PUSH_TO_SPU(state, SPU_DMA_LS_TO_EA_MBOX);
			PUSH_TO_SPU(state, LS);
			PUSH_TO_SPU(state, count);
			for(i = 0; i < count; i++)
				PUSH_TO_SPU(state, ((unsigned int*)EA)[i]);
				
			//The messages are queue'd, so this is valid
			spuhandler_HandleDMATransferCompleted(state, groupId);
		}		
		else
		{
			PUSH_TO_SPU(state, UINT_MAX);
			PUSH_TO_SPU(state, SPU_DMA_EA_TO_LS_MBOX);
			PUSH_TO_SPU(state, groupId);
			PUSH_TO_SPU(state, LS);
			PUSH_TO_SPU(state, EA);
			PUSH_TO_SPU(state, count);
			//We must wait for the SPU response
		}	
#endif

	}
		
	
}


//This function handles incoming create requests from an SPU
void spuhandler_HandleCreateRequest(struct SPU_State* state, unsigned int requestId, GUID id, unsigned long size)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling create request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

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

	spuhandler_SendRequestCoordinatorMessage(state, req);
}

//This function handles incoming barrier requests from an SPU
void spuhandler_handleBarrierRequest(struct SPU_State* state, unsigned int requestId, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling barrier request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

	struct spu_pendingRequest* preq;
	if ((preq = malloc(sizeof(struct spu_pendingRequest))) == NULL)
		REPORT_ERROR("malloc error");
		
	preq->objId = id;
	preq->operation = PACKAGE_ACQUIRE_BARRIER_REQUEST;
	preq->requestId = requestId;
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct acquireBarrierRequest* req;
	if ((req = malloc(sizeof(struct acquireBarrierRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	req->packageCode = PACKAGE_ACQUIRE_BARRIER_REQUEST;
	req->requestID = requestId;
	req->dataItem = id;

	spuhandler_SendRequestCoordinatorMessage(state, req);
}


//This function handles an incoming release request from an SPU
void spuhandler_HandleReleaseRequest(struct SPU_State* state, void* data)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Releasing object @: %d\n", WHEREARG, (unsigned int)data);
#endif
	unsigned int i;
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsByPointer, data);
	if (obj == NULL || obj->count == 0)
	{
		if (obj == NULL)
			fprintf(stderr, "* ERROR * " WHERESTR ": Attempted to release an object that was unknown: %d\n", WHEREARG,  (unsigned int)data);
		else
			fprintf(stderr, "* ERROR * " WHERESTR ": Attempted to release an object that was not acquired: %d\n", WHEREARG,  (unsigned int)data);
		
		return;
	}

	//Read releases are handled locally
	if (obj->mode == ACQUIRE_MODE_READ)
	{
		spuhandler_HandleObjectRelease(state, obj);
	}
	else /*if (obj->mode == ACQUIRE_MODE_WRITE)*/
	{
		//Get a group id, and register the active transfer
		struct spu_pendingRequest* preq;
		if((preq = malloc(sizeof(struct spu_pendingRequest))) == NULL)
			REPORT_ERROR("malloc error");
		
		preq->objId = obj->id;
		preq->requestId = NEXT_SEQ_NO(state->releaseSeqNo, MAX_PENDING_RELEASE_REQUESTS) + RELEASE_NUMBER_BASE;
		preq->operation = PACKAGE_RELEASE_REQUEST;
		

		preq->DMAcount = MAX(obj->size, SPE_DMA_MAX_TRANSFERSIZE) / SPE_DMA_MAX_TRANSFERSIZE;
		for(i = 0; i < preq->DMAcount; i++)
		{			
			obj->DMAId = NEXT_SEQ_NO(state->dmaSeqNo, MAX_DMA_GROUPID);
			g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, preq);
		}
		
		obj->isDMAToSPU = FALSE;

		//Inititate the DMA transfer if the buffer is ready		
		if (obj->writebuffer_ready)
		{
#ifdef DEBUG_COMMUNICATION
			printf(WHERESTR "DMAcount is %i\n", WHEREARG, preq->DMAcount);
#endif			
			unsigned int sizeRemain = obj->size;
			unsigned int sizeDone = 0;
			for(i = preq->DMAcount; i > 0; i--)
			{			
#ifdef DEBUG_COMMUNICATION	
				printf(WHERESTR "Write buffer is ready, transfering data immediately, dmaId: %d, obj id: %d\n", WHEREARG, obj->DMAId, obj->id);
#endif
				//spuhandler_InitiateDMATransfer(state, FALSE, (unsigned int)obj->EA, (unsigned int)obj->LS, obj->size, obj->DMAId);
				spuhandler_InitiateDMATransfer(state, FALSE, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), (obj->DMAId + 16 - i + 1) % 16);
				sizeRemain -= SPE_DMA_MAX_TRANSFERSIZE;
				sizeDone += SPE_DMA_MAX_TRANSFERSIZE;
			}
		}
		else
		{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Write buffer is not ready, delaying transfer, mode: %d, dmaId: %d\n", WHEREARG, obj->mode, obj->DMAId);
#endif
		}
	}	
}

//This function handles a writebuffer ready message from the request coordinator
void spuhandler_HandleWriteBufferReady(struct SPU_State* state, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling WriteBufferReady for itemId: %d\n", WHEREARG, id);
#endif
	unsigned int i;
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
	if (obj == NULL)
	{
		for(i = 0; i < g_queue_get_length(state->releaseWaiters); i++)
		{
			struct acquireResponse* data = g_queue_peek_nth(state->releaseWaiters, i);
			if (data->dataItem == id)
			{
				data->mode = ACQUIRE_MODE_WRITE_OK;
				return;
			}
		}
		printf(WHERESTR "Broken WRITEBUFFERREADY for object: %d\n", WHEREARG, id);
		REPORT_ERROR("Recieved a writebuffer ready request, but the object did not exist");
		return;
	}
	
	obj->writebuffer_ready = TRUE;
	
	unsigned int DMAcount = MAX(obj->size, SPE_DMA_MAX_TRANSFERSIZE) / SPE_DMA_MAX_TRANSFERSIZE;
	
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "DMAcount is %i\n", WHEREARG, DMAcount);
#endif		

	//If we are just waiting for the signal, then start the DMA transfer
	if (obj->DMAId != UINT_MAX && !obj->isDMAToSPU)
	{
		unsigned int sizeRemain = obj->size;
		unsigned int sizeDone = 0;
		for(i = DMAcount; i > 0; i--)
		{			
		
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "WriteBufferReady triggered a DMA transfer, dmaId: %d\n", WHEREARG, obj->DMAId);
#endif
			//spuhandler_InitiateDMATransfer(state, FALSE, (unsigned int)obj->EA, (unsigned int)obj->LS, obj->size, obj->DMAId);
			spuhandler_InitiateDMATransfer(state, FALSE, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), (obj->DMAId + 16 - i + 1) % 16);
			sizeRemain -= SPE_DMA_MAX_TRANSFERSIZE;
			sizeDone += SPE_DMA_MAX_TRANSFERSIZE;
		}
	}
}

//This function handles an acquireResponse package from the request coordinator
int spuhandler_HandleAcquireResponse(struct SPU_State* state, struct acquireResponse* data)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire response for id: %d, requestId: %d\n", WHEREARG, data->dataItem, data->requestID);
#endif
	
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)data->requestID);
	unsigned int i;
	if (preq == NULL)
	{
		REPORT_ERROR("Found response to an unknown request");
		return -1;
	}

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire response for id: %d, requestId: %d\n", WHEREARG, preq->objId, data->requestID);
#endif
	
	//TODO: If two threads acquire the same object, we cannot respond until the DMA is complete
	
	//Determine if data is already present on the SPU
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
	if (obj == NULL)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Allocating space on SPU\n", WHEREARG);
#endif
		void* ls = spuhandler_AllocateSpaceForObject(state, data->dataSize);
		if (ls == NULL)
		{
			//We have no space on the SPU, so wait until we get some
			//TODO: Verify that we have a pending release request
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "No more space, delaying acquire\n", WHEREARG);	
#endif
			g_queue_push_tail(state->releaseWaiters, data);
			return FALSE;
			
			/*fprintf(stderr, "* ERROR * " WHERESTR ": Failed to allocate %d bytes on the SPU, allocated objects: %d, free memory: %d, allocated blocks: %d", WHEREARG, (int)data->dataSize, g_hash_table_size(state->itemsById), state->map->free_mem, g_hash_table_size(state->map->allocated));
			
			GHashTableIter iter;
			g_hash_table_iter_init(&iter, state->itemsById);
			GUID key;
			struct spu_dataObject* value;
			
			while(g_hash_table_iter_next(&iter, (void*)&key, (void*)&value))
				fprintf(stderr, "* ERROR * " WHERESTR ": Item %d is allocated at %d and takes up %d bytes, count: %d\n", WHEREARG, key, (unsigned int)value->LS, (unsigned int)value->size, value->count);
			
			sleep(5); 
			PUSH_TO_SPU(state, preq->requestId);
			PUSH_TO_SPU(state, NULL);
			PUSH_TO_SPU(state, 0);
			
			g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
			free(preq);
			return;*/	
		}

#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling acquire response for id: %d, requestId: %d, object did not exist, creating\n", WHEREARG, preq->objId, data->requestID);
#endif

		if ((obj = malloc(sizeof(struct spu_dataObject))) == NULL)
			REPORT_ERROR("malloc error");
			
		obj->count = 1;
		obj->EA = data->data;
		obj->id = preq->objId;
		obj->invalidateId = UINT_MAX;
		obj->mode = preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
		obj->size = data->dataSize;
		obj->LS = ls;
		obj->writebuffer_ready = preq->operation == PACKAGE_CREATE_REQUEST || data->mode == ACQUIRE_MODE_WRITE_OK;
		obj->isDMAToSPU = TRUE;
	
		g_hash_table_insert(state->itemsById, (void*)obj->id, obj);
		g_hash_table_insert(state->itemsByPointer, obj->LS, obj);
		
		g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
		
		preq->DMAcount = MAX(obj->size, SPE_DMA_MAX_TRANSFERSIZE) / SPE_DMA_MAX_TRANSFERSIZE;

#ifdef DEBUG_COMMUNICATION
		printf(WHERESTR "DMAcount is %i, size is %li\n", WHEREARG, preq->DMAcount, obj->size);
#endif		
		unsigned int sizeRemain = obj->size;
		unsigned int sizeDone = 0;
		for(i = 0; i < preq->DMAcount; i++)
		{
			obj->DMAId = NEXT_SEQ_NO(state->dmaSeqNo, MAX_DMA_GROUPID);
			g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, preq);
			
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "Starting DMA transfer, from: %d, to: %d, size: %d, tag: %d\n", WHEREARG, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), obj->DMAId);
#endif
			spuhandler_InitiateDMATransfer(state, TRUE, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), obj->DMAId);
			sizeRemain -= SPE_DMA_MAX_TRANSFERSIZE; 
			sizeDone += SPE_DMA_MAX_TRANSFERSIZE;
		}
	}
	else
	{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire response for id: %d, requestId: %d, object existed, returning pointer\n", WHEREARG, preq->objId, data->requestID);
#endif

		obj->count++;	
		obj->mode = preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
		obj->EA = data->data;
		obj->writebuffer_ready = preq->operation == PACKAGE_CREATE_REQUEST;
		
		PUSH_TO_SPU(state, preq->requestId);
		PUSH_TO_SPU(state, obj->LS);
		PUSH_TO_SPU(state, obj->size);
		
		if (preq->operation != PACKAGE_ACQUIRE_REQUEST_WRITE)
		{
			g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
			free(preq);
		}	
	}
	
	if (obj->count == 1)
		g_queue_remove(state->agedObjectMap, (void*)obj->id);

	return TRUE;

}

//This function deals with acquire requests that are waiting for a release response
void spuhandler_ManageDelayedAcquireResponses(struct SPU_State* state)
{
	while (!g_queue_is_empty(state->releaseWaiters))
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Attempting to process a new delayed acquire\n", WHEREARG);	
#endif
		struct acquireResponse* resp = g_queue_pop_head(state->releaseWaiters);
		if (!spuhandler_HandleAcquireResponse(state, resp))
			return;
	}
}

//This function deals with cleaning up and possibly freeing objects
void spuhandler_HandleObjectRelease(struct SPU_State* state, struct spu_dataObject* obj)
{
		obj->count--;
		if (obj->count == 0)
		{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "This was the last holder, check for invalidate\n", WHEREARG);	
#endif
			//If this was the last release, check for invalidates, and otherwise register in the age map
			
			if (obj->invalidateId != UINT_MAX || !g_queue_is_empty(state->releaseWaiters) || state->terminated != UINT_MAX)			
			{
#ifdef DEBUG_COMMUNICATION	
				if (state->terminated != UINT_MAX)
					printf(WHERESTR "Releasing object after termination: %d\n", WHEREARG, obj->id);
				else	
					printf(WHERESTR "Releasing object: %d\n", WHEREARG, obj->id);
#endif
				
				spuhandler_DisposeObject(state, obj);
				spuhandler_ManageDelayedAcquireResponses(state);
			}
			else
				g_queue_push_tail(state->agedObjectMap, (void*)obj->id);
		}
}

//This function handles completed DMA transfers
void spuhandler_HandleDMATransferCompleted(struct SPU_State* state, unsigned int groupID)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling completed DMA transfer, dmaId: %d\n", WHEREARG, groupID);
#endif
	
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
	
	obj->DMAId = UINT_MAX;
	preq->DMAcount--;
	
	//If the transfer was from PPU to SPU, we can now give the pointer to the SPU
	if (preq->operation != PACKAGE_RELEASE_REQUEST)
	{
		if (preq->DMAcount != 0)
			return;
			
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, id: %d, notifying SPU\n", WHEREARG, groupID, preq->objId);
#endif
		PUSH_TO_SPU(state, preq->requestId);
		PUSH_TO_SPU(state, obj->LS);
		PUSH_TO_SPU(state, obj->size);
		
		if (preq->operation != PACKAGE_ACQUIRE_REQUEST_WRITE)
			free(preq);
	}
	else if(preq->DMAcount == 0)
	{
		//Data is transfered from SPU LS to EA, now notify the request coordinator
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, id: %d, notifying RC\n", WHEREARG, groupID, preq->objId);
#endif
		
		struct releaseRequest* req;
		if ((req = malloc(sizeof(struct releaseRequest))) == NULL)
			REPORT_ERROR("malloc error");
			
		req->data = obj->EA;
		req->dataItem = obj->id;
		req->dataSize = obj->size;
		req->mode = ACQUIRE_MODE_WRITE;
		req->offset = 0;
		req->packageCode = PACKAGE_RELEASE_REQUEST;
		req->requestID = preq->requestId;
		
		g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);
		
		spuhandler_SendRequestCoordinatorMessage(state, req);
	}
}

//This function handles incoming release responses from the request coordinator
void spuhandler_HandleReleaseResponse(struct SPU_State* state, unsigned int requestId)
{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Handling release respone for requestId: %d\n", WHEREARG, requestId);
#endif	
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)requestId);
	if (preq == NULL)
	{
		REPORT_ERROR("Get release response for non initiated request");
		return;
	}

	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);

	g_hash_table_remove(state->pendingRequests, (void*)requestId);
	free(preq);

	if (obj == NULL || obj->count == 0)
	{
		REPORT_ERROR("Release was completed, but the object was not acquired?");
		return;
	}
	
	spuhandler_HandleObjectRelease(state, obj);
}

//This function handles incoming barrier responses from the request coordinator
void spuhandler_HandleBarrierResponse(struct SPU_State* state, unsigned int requestId)
{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Handling barrier respone for requestId: %d\n", WHEREARG, requestId);
#endif	
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)requestId);
	if (preq == NULL)
	{
		REPORT_ERROR("Get release response for non initiated request");
		return;
	}

	g_hash_table_remove(state->pendingRequests, (void*)requestId);
	free(preq);

	PUSH_TO_SPU(state, requestId);
}


//This function handles incoming invalidate requests from the request coordinator
void spuhandler_HandleInvalidateRequest(struct SPU_State* state, unsigned int requestId, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling invalidate request for id: %d, request id: %d\n", WHEREARG, id, requestId);
#endif

	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
	if (obj == NULL)
		EnqueInvalidateResponse(requestId);
	else
	{
		if (obj->count == 1 && obj->mode == ACQUIRE_MODE_WRITE)
		{
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "The Invalidate was for an object in WRITE mode\n", WHEREARG);	
#endif
			EnqueInvalidateResponse(requestId);
		}
#ifdef DEBUG_COMMUNICATION
		else	
			printf(WHERESTR "The Invalidate was for an object in READ mode\n", WHEREARG);	
#endif
		
		
		if (obj->count == 0 || obj->mode == ACQUIRE_MODE_READ)
		{
			obj->invalidateId = requestId;
			if (obj->count == 0)
				spuhandler_DisposeObject(state, obj);
		}
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
#ifdef DEBUG_COMMUNICATION	
				printf(WHERESTR "Terminate request recieved\n", WHEREARG);
#endif
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				state->terminated = requestId;
				spuhandler_DisposeAllObject(state);		
				break;
			case PACKAGE_SPU_MEMORY_SETUP:
				if (state->map != NULL) 
				{
					REPORT_ERROR("Tried to re-initialize SPU memory map");
				} 
				else
				{
					state->terminated = UINT_MAX;
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
				PUSH_TO_SPU(state, requestId);
				PUSH_TO_SPU(state, spuhandler_AllocateSpaceForObject(state, size));
				break;
			case PACKAGE_ACQUIRE_BARRIER_REQUEST:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spe_out_intr_mbox_read(state->context, &id, 1, SPE_MBOX_ALL_BLOCKING);
				spuhandler_handleBarrierRequest(state, requestId, id);
				break;
			default:
				REPORT_ERROR("Unknown package code recieved");			
				break;	
		}
	}
}

//This function reads and handles incomming requests and responses from the request coordinator
//BEWARE: The queue mutex MUST be locked before calling this method
void spuhandler_HandleRequestCoordinatorMessages(struct SPU_State* state)
{
	struct acquireResponse* resp = NULL;
	
	while(TRUE)
	{
		resp = g_queue_pop_head(state->queue);

		if (resp == NULL)
			return;
			
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling request coordinator message: %s\n", WHEREARG, PACKAGE_NAME(resp->packageCode));			
#endif			
		switch(resp->packageCode)
		{
			case PACKAGE_ACQUIRE_RESPONSE:
				if (spuhandler_HandleAcquireResponse(state, resp) == FALSE)
					resp = NULL;
				break;
			case PACKAGE_RELEASE_RESPONSE:
				spuhandler_HandleReleaseResponse(state, ((struct releaseResponse*)resp)->requestID);
				break;
			case PACKAGE_INVALIDATE_REQUEST:
				spuhandler_HandleInvalidateRequest(state, ((struct invalidateRequest*)resp)->requestID, ((struct invalidateRequest*)resp)->dataItem);
				break;
			case PACKAGE_WRITEBUFFER_READY:
				spuhandler_HandleWriteBufferReady(state, ((struct writebufferReady*)resp)->dataItem);
				break;
			case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
				spuhandler_HandleBarrierResponse(state, ((struct acquireBarrierResponse*)resp)->requestID);
				break;
			default:
				REPORT_ERROR("Invalid package recieved");
				break;			
		}
		
		if (resp != NULL)
			free(resp);
		resp = NULL;
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
int spuhandler_SPUMailboxWriter(struct SPU_State* state)
{
	while (!g_queue_is_empty(state->mailboxQueue) && spe_in_mbox_status(state->context) != 0)	
	{
		unsigned int data = (unsigned int)state->mailboxQueue->head->data;
		//printf(WHERESTR "Sending Mailbox message: %i\n", WHEREARG, (unsigned int)Gspu_mailboxQueues[i]->head->data);
		if (spe_in_mbox_write(state->context, &data, 1, SPE_MBOX_ALL_BLOCKING) != 1) {
			REPORT_ERROR("Failed to send message, even though it was blocking!"); 
		} else
			g_queue_pop_head(state->mailboxQueue);
	}
	
	return !g_queue_is_empty(state->mailboxQueue);
}

//This function repeatedly checks for events relating to the SPU's
void* SPU_MainThread(void* dummy)
{
	size_t i;
	unsigned int pending_out_data;
	
	//Event base, keeps the mutex locked, until we wait for events
#ifdef EVENT_BASED
	struct timespec waittime;
	pthread_mutex_lock(&spu_rq_mutex);
#endif

	while(!spu_terminate)
	{

		pending_out_data = 0;

//Non-event based just needs the lock for accessing the queue	
#ifndef EVENT_BASED
		//Lock the mutex once, and read all the data
		pthread_mutex_lock(&spu_rq_mutex);

		for(i = 0; i < spe_thread_count; i++)
			spuhandler_HandleRequestCoordinatorMessages(&spu_states[i]);

		pthread_mutex_unlock(&spu_rq_mutex);
#endif

		for(i = 0; i < spe_thread_count; i++)
		{
			//For each SPU, just repeat this
#ifdef EVENT_BASED
			spuhandler_HandleRequestCoordinatorMessages(&spu_states[i]);
#endif
			spuhandler_SPUMailboxReader(&spu_states[i]);
			spuhandler_HandleDMAEvent(&spu_states[i]);
			pending_out_data |= spuhandler_SPUMailboxWriter(&spu_states[i]);
		}
		
#ifdef EVENT_BASED
		if (pending_out_data)
		{
			clock_gettime(CLOCK_REALTIME, &waittime);
			waittime.tv_nsec += 10000000;
			pthread_cond_timedwait(&spu_rq_event, &spu_rq_mutex, &waittime);
		}
		else
			pthread_cond_wait(&spu_rq_event, &spu_rq_mutex);
#endif
	}

#ifdef EVENT_BASED
		pthread_mutex_unlock(&spu_rq_mutex);
#endif
	return dummy;
}

#ifdef EVENT_BASED
void* SPU_EventWatcher(void* dummy)
{
	int event_count;
	spe_event_unit_t event;
	
	while(!spu_terminate)
	{
		event_count = spe_event_wait(spu_event_handler, &event, SPE_MAX_EVENT_COUNT, 5000);
		if (event_count == -1)
			REPORT_ERROR("spe_event_wait failed");
		
		//After each event, trigger this 
		pthread_mutex_lock(&spu_rq_mutex);
		pthread_cond_signal(&spu_rq_event);
		pthread_mutex_unlock(&spu_rq_mutex);
		
		//This code is used to troubleshoot jiggy event handling code
		if (event_count == 0 && !spu_terminate)
			printf(WHERESTR "Jiggy! Jiggy! Jiggy!\n", WHEREARG);
		
	}
	
	return dummy;
}
#endif

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
		
	if (pthread_mutex_init(&spu_rq_mutex, NULL) != 0) REPORT_ERROR("Mutex initialization failed");

#ifdef EVENT_BASED
	if (pthread_cond_init(&spu_rq_event, NULL) != 0) REPORT_ERROR("Cond initialization failed");

	spu_event_handler = spe_event_handler_create();
	if (spu_event_handler == NULL)
		REPORT_ERROR("Broken event handler");
		
	if ((registered_events = malloc(sizeof(spe_event_unit_t) * spe_thread_count)) == NULL)
		REPORT_ERROR("malloc error");
#endif

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
		spu_states[i].releaseWaiters = g_queue_new();
		spu_states[i].queue = g_queue_new();
		spu_states[i].terminated = UINT_MAX;
		
#ifdef EVENT_BASED
		RegisterInvalidateSubscriber(&spu_rq_mutex, &spu_rq_event, &spu_states[i].queue);

		//The SPE_EVENT_IN_MBOX is enabled whenever there is data in the queue
		registered_events[i].spe = spu_states[i].context;
		registered_events[i].events = SPE_EVENT_OUT_INTR_MBOX | SPE_EVENT_TAG_GROUP;
		registered_events[i].data.ptr = (void*)i;

		if (spe_event_handler_register(spu_event_handler, &registered_events[i]) != 0)
			REPORT_ERROR("Register failed");
#else
		RegisterInvalidateSubscriber(&spu_rq_mutex, NULL, &spu_states[i].queue);
#endif		
	}


	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

#ifdef EVENT_BASED
	pthread_create(&spu_event_watcher, &attr, SPU_EventWatcher, NULL);
#endif
	
	pthread_create(&spu_mainthread, &attr, SPU_MainThread, NULL);
}

//This function cleans up used resources 
void TerminateSPUHandler(int force)
{
	size_t i;
	
	//Remove warning
	spu_terminate = force | TRUE;
	
#ifdef EVENT_BASED
	pthread_mutex_lock(&spu_rq_mutex);
	pthread_cond_signal(&spu_rq_event);
	pthread_mutex_unlock(&spu_rq_mutex);
	
	pthread_join(spu_event_watcher, NULL);
#endif 
	pthread_join(spu_mainthread, NULL); 
	
	pthread_mutex_destroy(&spu_rq_mutex);

	for(i = 0; i < spe_thread_count; i++)
	{
		g_hash_table_destroy(spu_states[i].activeDMATransfers);
		g_queue_free(spu_states[i].agedObjectMap);
		g_hash_table_destroy(spu_states[i].itemsById);
		g_hash_table_destroy(spu_states[i].itemsByPointer);
		g_queue_free(spu_states[i].mailboxQueue);
		g_hash_table_destroy(spu_states[i].pendingRequests);
		g_queue_free(spu_states[i].releaseWaiters);
		g_queue_free(spu_states[i].queue);

		spu_memory_destroy(spu_states[i].map);

		UnregisterInvalidateSubscriber(&spu_states[i].queue);
		
#ifdef EVENT_BASED
		if (spe_event_handler_deregister(spu_event_handler, &registered_events[i]) != 0)
			REPORT_ERROR("Unregister failed");
#endif		
	}
	
#ifdef EVENT_BASED
	pthread_cond_destroy(&spu_rq_event);
	spe_event_handler_destroy(&spu_event_handler);
#endif
	free(spu_states);
	
}

/*
//This function tries to determine if there is no possibility that the given object is using the EA buffer
//WARNING: The mutex MUST be locked before calling this method
int spuhandler_IsWriteBufferInUse(GQueue** queue, GUID id)
{
	size_t i;
	
	for(i = 0; i < spe_thread_count; i++)
		if (&spu_states[i].queue == queue)
		{
			//TODO: This is a dirty read in the hashtable, if we are not using events
			struct spu_dataObject* obj = g_hash_table_lookup(spu_states[i].itemsById, (void*)id);
			return !(obj == NULL || obj->DMAId == UINT_MAX || obj->mode == ACQUIRE_MODE_WRITE);
		}
		
	//The queue was now for an SPE, so just to be safe, we say it's in use
	return TRUE;
}
*/

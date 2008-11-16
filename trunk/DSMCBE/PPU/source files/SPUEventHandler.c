#include <pthread.h>
#include <glib/ghash.h>
#include <glib/gqueue.h>
#include <unistd.h>

#include "../header files/SPU_MemoryAllocator.h"
#include "../header files/SPU_MemoryAllocator_Shared.h"
#include "../header files/SPUEventHandler.h"
#include "../header files/RequestCoordinator.h"
#include "../../common/debug.h"
#include "../../common/datapackages.h"
#include "../header files/NetworkHandler.h"


//The number of avalible DMA group ID's
//NOTE: On the PPU this is 0-15, NOT 0-31 as on the SPU! 
#define MAX_DMA_GROUPID 16

//Disable keeping data on the SPU after release
//#define DISABLE_SPU_CACHE

//#define DEBUG_COMMUNICATION

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
#define SPE_DMA_MIN_TRANSFERSIZE (32)

//unsigned int spu_dma_seq_no;
#define DMA_SEQ_NO state->dmaSeqNo

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
	//This is a list of all pending requests, key is requestId, value is dataObject*
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

	//This is the stream queue
	GQueue* streamItems;
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

//This event is signaled when data is comming from the request coordinator
pthread_cond_t spu_rq_event;

//This first macro tries to bypass the queue, if possible
unsigned int spuhandler_temp_mbox_value;
#define PUSH_TO_SPU(state, value) spuhandler_temp_mbox_value = (unsigned int)value; if (g_queue_get_length(state->mailboxQueue) != 0 || spe_in_mbox_status(state->context) == 0 || spe_in_mbox_write(state->context, &spuhandler_temp_mbox_value, 1, SPE_MBOX_ALL_BLOCKING) != 1)  { g_queue_push_tail(state->mailboxQueue, (void*)spuhandler_temp_mbox_value); }

//This second macro always uses the queue 
//#define PUSH_TO_SPU(state, value) g_queue_push_tail(state->mailboxQueue, (void* )value);

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
	FREE(obj);
	obj = NULL;
	
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
	if ((qi = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("malloc error");
	
		qi->callback = NULL;
		qi->dataRequest = req;
		qi->event = NULL;
		qi->mutex = &spu_rq_mutex;
		qi->Gqueue = &state->queue;
		
		//printf(WHERESTR "Inserting item into request coordinator queue (%d)\n", WHEREARG, (unsigned int)qi);
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
	if (temp == NULL)
	{
		size_t j;
		size_t lc = g_queue_get_length(state->agedObjectMap);
		if (lc < 10 && lc > 0)
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
					temp = spu_memory_malloc(state->map, size);
					break;			
				}		
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
	if ((preq = MALLOC(sizeof(struct spu_pendingRequest))) == NULL)
		REPORT_ERROR("malloc error");

	// Setting DMAcount to UINT_MAX to initialize the struct.
	preq->DMAcount = UINT_MAX;		
	//printf(WHERESTR "New pointer: %d\n", WHEREARG, (unsigned int)preq);

	preq->objId = id;
	preq->operation = packageCode;
	preq->requestId = requestId;
	preq->DMAcount = 0;
	//printf(WHERESTR "Assigned reqId: %d\n", WHEREARG, preq->requestId);
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct acquireRequest* req;
	if ((req = MALLOC(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	req->packageCode = packageCode;
	req->requestID = requestId;
	req->dataItem = id;
	req->originator = dsmcbe_host_number;
	req->originalRecipient = UINT_MAX;
	req->originalRequestID = UINT_MAX;

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
		/*spe_mfc_command_area_t* x = spe_ps_area_get(state->context, SPE_MFC_COMMAND_AREA);
		if (x->MFC_QStatus <= 0) {
			REPORT_ERROR("DMA Overflow");
			sleep(5);
		}*/

		unsigned int transfersize = ALIGNED_SIZE(size);
		if (toSPU)
		{
			if (spe_mfcio_get(state->context, LS, (void*)EA, transfersize, groupId, 0, 0) != 0)
			{
				REPORT_ERROR("DMA transfer from EA to LS failed");
				fprintf(stderr, WHERESTR "Initiating DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, transfersize, groupId);
			}
		}
		else
		{
			if (spe_mfcio_put(state->context, LS, (void*)EA, transfersize, groupId, 0, 0) != 0)
			{
				REPORT_ERROR("DMA transfer from LS to EA failed");
				fprintf(stderr, WHERESTR "Initiating DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, transfersize, groupId);
			}
		}
	}
	else
	{

#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Initiating simulated DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, size, groupId);
#endif
		
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
	if ((preq = MALLOC(sizeof(struct spu_pendingRequest))) == NULL)
		REPORT_ERROR("malloc error");
		
	// Setting DMAcount to UINT_MAX to initialize the struct.
	preq->DMAcount = UINT_MAX;
	preq->objId = id;
	preq->operation = PACKAGE_CREATE_REQUEST;
	preq->requestId = requestId;
	preq->DMAcount = 0;
	//printf(WHERESTR "Assigned reqId: %d\n", WHEREARG, preq->requestId);
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct createRequest* req;
	if ((req = MALLOC(sizeof(struct createRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	req->packageCode = PACKAGE_CREATE_REQUEST;
	req->requestID = requestId;
	req->dataItem = id;
	req->dataSize = size;
	req->originator = dsmcbe_host_number;
	req->originalRecipient = UINT_MAX;
	req->originalRequestID = UINT_MAX;

	spuhandler_SendRequestCoordinatorMessage(state, req);
}

//This function handles incoming barrier requests from an SPU
void spuhandler_handleBarrierRequest(struct SPU_State* state, unsigned int requestId, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling barrier request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

	struct spu_pendingRequest* preq;
	if ((preq = MALLOC(sizeof(struct spu_pendingRequest))) == NULL)
		REPORT_ERROR("malloc error");

	// Setting DMAcount to UINT_MAX to initialize the struct.
	preq->DMAcount = UINT_MAX;		
	preq->objId = id;
	preq->operation = PACKAGE_ACQUIRE_BARRIER_REQUEST;
	preq->requestId = requestId;
	preq->DMAcount = 0;
	//printf(WHERESTR "Assigned reqId: %d\n", WHEREARG, preq->requestId);
		
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct acquireBarrierRequest* req;
	if ((req = MALLOC(sizeof(struct acquireBarrierRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	req->packageCode = PACKAGE_ACQUIRE_BARRIER_REQUEST;
	req->requestID = requestId;
	req->dataItem = id;
	req->originator = dsmcbe_host_number;
	req->originalRecipient = UINT_MAX;
	req->originalRequestID = UINT_MAX;

	spuhandler_SendRequestCoordinatorMessage(state, req);
}

//This function transfers an entire object to or from the SPU
void spuhandler_TransferObject(struct SPU_State* state, struct spu_pendingRequest* preq, struct spu_dataObject* obj)
{
	unsigned int i;
	preq->DMAcount = (MAX(ALIGNED_SIZE(obj->size), SPE_DMA_MAX_TRANSFERSIZE) + (SPE_DMA_MAX_TRANSFERSIZE - 1)) / SPE_DMA_MAX_TRANSFERSIZE;

#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "DMAcount is %i, size is %li\n", WHEREARG, preq->DMAcount, obj->size);
#endif		
	unsigned long sizeRemain = obj->size;
	unsigned long sizeDone = 0;
	unsigned int DMACount = preq->DMAcount;
	for(i = 0; i < DMACount; i++)
	{
		while(TRUE)
		{
			if (g_hash_table_size(state->activeDMATransfers) >= MAX_DMA_GROUPID)
			{
				//TODO: It's possible to handle this by keeping track of pending transfers
				//and starting transfers after a completed one
				REPORT_ERROR("DMA Sequence number overflow! This won't be pretty!");
				exit(-6);
			}
			
			obj->DMAId = NEXT_SEQ_NO(DMA_SEQ_NO, MAX_DMA_GROUPID);
			if (g_hash_table_lookup(state->activeDMATransfers, (void*)obj->DMAId) != NULL)
				continue;
			g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, preq);
			break;
		}
		
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Starting DMA transfer, from: %d, to: %d, size: %d, tag: %d\n", WHEREARG, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), obj->DMAId);
#endif
		spuhandler_InitiateDMATransfer(state, obj->isDMAToSPU, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), obj->DMAId);
		sizeRemain -= SPE_DMA_MAX_TRANSFERSIZE; 
		sizeDone += SPE_DMA_MAX_TRANSFERSIZE;
	}
}

//This function handles an incoming release request from an SPU
void spuhandler_HandleReleaseRequest(struct SPU_State* state, void* data)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Releasing object @: %d\n", WHEREARG, (unsigned int)data);
#endif
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
		//printf(WHERESTR "Handling write release\n", WHEREARG);
		//Get a group id, and register the active transfer
		struct spu_pendingRequest* preq;
		if((preq = MALLOC(sizeof(struct spu_pendingRequest))) == NULL)
			REPORT_ERROR("malloc error");
		
		preq->objId = obj->id;
		preq->requestId = NEXT_SEQ_NO(state->releaseSeqNo, MAX_PENDING_RELEASE_REQUESTS) + RELEASE_NUMBER_BASE;
		preq->operation = PACKAGE_RELEASE_REQUEST;
		preq->DMAcount = (MAX(ALIGNED_SIZE(obj->size), SPE_DMA_MAX_TRANSFERSIZE) + (SPE_DMA_MAX_TRANSFERSIZE - 1)) / SPE_DMA_MAX_TRANSFERSIZE;

		obj->isDMAToSPU = FALSE;

		//Inititate the DMA transfer if the buffer is ready		
		if (obj->writebuffer_ready)
		{
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "WriteBuffer was ready, performing transfer without delay: %d\n", WHEREARG, obj->id);
#endif
			spuhandler_TransferObject(state, preq, obj);
		}
		else
		{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "WriteBuffer was NOT ready, registering a dummy object: %d\n", WHEREARG, obj->id);
#endif
			//Otherwise just register it as active to avoid disposing
			while(TRUE)
			{
				if (g_hash_table_size(state->activeDMATransfers) >= MAX_DMA_GROUPID)
				{
					REPORT_ERROR("DMA Sequence number overflow! This won't be pretty!");
					exit(-6);
				}
				
				obj->DMAId = NEXT_SEQ_NO(DMA_SEQ_NO, MAX_DMA_GROUPID);
				if (g_hash_table_lookup(state->activeDMATransfers, (void*)obj->DMAId) != NULL)
					continue;
				g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, preq);
				break;
			}

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Write buffer is not ready, delaying transfer for %d, mode: %d, dmaId: %d\n", WHEREARG, obj->id, obj->mode, obj->DMAId);
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
	struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
	if (obj == NULL)
	{
		REPORT_ERROR2("Recieved a writebuffer ready request, but object %d did not exist", id);
		return;
	}
	
	if (obj->writebuffer_ready == TRUE)
		REPORT_ERROR("Writebuffer ready already set?");
	obj->writebuffer_ready = TRUE;

	//If we are just waiting for the signal, then start the DMA transfer
	//The DMAId is not set until the object is released
	if (obj->DMAId != UINT_MAX && !obj->isDMAToSPU)
	{
		struct spu_pendingRequest* preq = g_hash_table_lookup(state->activeDMATransfers, (void*)obj->DMAId);
		if (preq == NULL) {
			REPORT_ERROR2("Delayed DMA write for %d was missing control object!", obj->DMAId);
			exit(-5);
		} else {
			g_hash_table_remove(state->activeDMATransfers, (void*)obj->DMAId);
		}
		
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "WriteBufferReady triggered a DMA transfer: %d\n", WHEREARG, id);
#endif
		
		spuhandler_TransferObject(state, preq, obj);
	}
}

//This function calculates the amount of space that is in the process of being released,
// and thus will be avalible for use
unsigned int spuhandler_EstimatePendingReleaseSize(struct SPU_State* state)
{
	GHashTableIter iter;
	g_hash_table_iter_init(&iter, state->pendingRequests);
	void* key;
	void* value;
	unsigned int pendingSize = 0;
	struct spu_dataObject* obj;
	
	while(g_hash_table_iter_next(&iter, &key, (void**)&value))
	{
		struct spu_pendingRequest* v = (struct spu_pendingRequest*)value;
		if (v->operation == PACKAGE_RELEASE_REQUEST)
		{
			obj = g_hash_table_lookup(state->itemsById, (void*)v->objId);
			if (obj != NULL)
			{
#ifdef DEBUG_COMMUNICATION	
				printf(WHERESTR "Found obj %d with size %d in pending\n", WHEREARG, obj->id, obj->size);
#endif
				pendingSize += obj->size;
			}
		}
	}
	
	g_hash_table_iter_init(&iter, state->activeDMATransfers);
	while(g_hash_table_iter_next(&iter, &key, (void**)&value))
	{
		//TODO: This gives a misleading count if there are large blocks,
		//because each entry indicates the same object, and thus the full
		//object size, and not the block size
		struct spu_pendingRequest* v = (struct spu_pendingRequest*)value;
		if (v->operation == PACKAGE_RELEASE_REQUEST)
		{
			obj = g_hash_table_lookup(state->itemsById, (void*)v->objId);
			if (obj != NULL)
			{
#ifdef DEBUG_COMMUNICATION	
				printf(WHERESTR "Found obj %d with size %d in pending\n", WHEREARG, obj->id, obj->size);
#endif
				pendingSize += obj->size;
			}
		}
	}

	return pendingSize;
}

//This function handles an acquireResponse package from the request coordinator
int spuhandler_HandleAcquireResponse(struct SPU_State* state, struct acquireResponse* data)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire response for id: %d, requestId: %d\n", WHEREARG, data->dataItem, data->requestID);
#endif
	
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)data->requestID);
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
			if (spuhandler_EstimatePendingReleaseSize(state) == 0)
			{
				REPORT_ERROR2("No more space, denying acquire request for %d", data->dataItem);

				/*fprintf(stderr, "* ERROR * " WHERESTR ": Failed to allocate %d bytes on the SPU, allocated objects: %d, free memory: %d, allocated blocks: %d\n", WHEREARG, (int)data->dataSize, g_hash_table_size(state->itemsById), state->map->free_mem, g_hash_table_size(state->map->allocated));
			
				GHashTableIter iter;
				g_hash_table_iter_init(&iter, state->itemsById);
				GUID key;
				struct spu_dataObject* value;
				
				while(g_hash_table_iter_next(&iter, (void*)&key, (void*)&value))
					fprintf(stderr, "* ERROR * " WHERESTR ": Item %d is allocated at %d and takes up %d bytes, count: %d\n", WHEREARG, key, (unsigned int)value->LS, (unsigned int)value->size, value->count);

				g_hash_table_iter_init(&iter, state->pendingRequests);
				while(g_hash_table_iter_next(&iter, &key, (void**)&value))
				{
					struct spu_pendingRequest* v = (struct spu_pendingRequest*)value;
					fprintf(stderr, "* ERROR * " WHERESTR ": Item %d is in pending\n", WHEREARG, v->objId);
				}
	
				g_hash_table_iter_init(&iter, state->activeDMATransfers);
				while(g_hash_table_iter_next(&iter, &key, (void**)&value))
				{
					struct spu_pendingRequest* v = (struct spu_pendingRequest*)value;
					fprintf(stderr, "* ERROR * " WHERESTR ": Item %d is in DMATransfer\n", WHEREARG, v->objId);
				}*/
				
				sleep(5);
				
				PUSH_TO_SPU(state, preq->requestId;);
				PUSH_TO_SPU(state, NULL);
				PUSH_TO_SPU(state, 0);
				g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
				FREE(preq);
				return TRUE;
				
			}
		} 
			

#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling acquire response for id: %d, requestId: %d, object did not exist, creating\n", WHEREARG, preq->objId, data->requestID);
#endif

		if ((obj = MALLOC(sizeof(struct spu_dataObject))) == NULL)
			REPORT_ERROR("malloc error");
			
		obj->count = 1;
		obj->EA = data->data;
		obj->id = preq->objId;
		obj->invalidateId = UINT_MAX;
		obj->mode = preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
		obj->size = data->dataSize;
		obj->LS = ls;
		obj->writebuffer_ready = preq->operation == PACKAGE_CREATE_REQUEST;
		obj->isDMAToSPU = TRUE;
	
		g_hash_table_insert(state->itemsById, (void*)obj->id, obj);
		g_hash_table_insert(state->itemsByPointer, obj->LS, obj);
		
		g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);

		if (ls == NULL)
		{
#ifdef DEBUG_COMMUNICATION	
				printf(WHERESTR "No more space, delaying acquire until space becomes avalible, free mem: %d, pendingSize: %d, reqSize: %d\n", WHEREARG, state->map->free_mem, pendingSize, data->dataSize);
#endif
				if (g_queue_find(state->releaseWaiters, preq) == NULL)
					g_queue_push_tail(state->releaseWaiters, preq);
		}
		else
		{
			//Send it!
			spuhandler_TransferObject(state, preq, obj);
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
		
		//if (preq->operation != PACKAGE_ACQUIRE_REQUEST_WRITE)
		{
			g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
			FREE(preq);
			preq = NULL;
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
		struct spu_pendingRequest* preq = g_queue_peek_head(state->releaseWaiters);
		
		if (preq == NULL)
			REPORT_ERROR("Pending request was missing?");
	
		struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
		if (obj == NULL)
			REPORT_ERROR("Object was missing in delayed acquire");		
		
		if (obj->LS != NULL)
			REPORT_ERROR("Waiter was allocated?");
		obj->LS = spuhandler_AllocateSpaceForObject(state, obj->size);
		if (obj->LS == NULL)
		{
			if (spuhandler_EstimatePendingReleaseSize(state) == 0)
				REPORT_ERROR("Out of memory on SPU");
			return;
		}
		else
		{
			g_queue_pop_head(state->releaseWaiters);
			spuhandler_TransferObject(state, preq, obj);
		}
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
			
#ifdef DISABLE_SPU_CACHE
			if (TRUE)
#else
			if (obj->invalidateId != UINT_MAX || !g_queue_is_empty(state->releaseWaiters) || state->terminated != UINT_MAX)
#endif
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
		FREE(preq);
		preq = NULL;
		REPORT_ERROR("DMA was completed, but there was not allocated space?");
		return;
	}

	preq->DMAcount--;
	if (preq->DMAcount == 0)
		obj->DMAId = UINT_MAX;

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, count: %d, DMAId: %d\n", WHEREARG, groupID, preq->DMAcount, obj->DMAId);
#endif

	
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
		
		FREE(preq);
		preq = NULL;
	}
	else if(preq->DMAcount == 0)
	{
		//Data is transfered from SPU LS to EA, now notify the request coordinator
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, id: %d, notifying RC\n", WHEREARG, groupID, preq->objId);
#endif
		//__asm__ __volatile__("lwsync" : : : "memory");
		
		struct releaseRequest* req;
		if ((req = MALLOC(sizeof(struct releaseRequest))) == NULL)
			REPORT_ERROR("malloc error");
			
		/*if (obj->id == 103)
		{
			char* ea = (char*)obj->EA;
			char* spu_ls = (char*)((unsigned int)spe_ls_area_get(state->context) + (unsigned int)obj->LS);
			printf(WHERESTR "Testing id %d with %d bytes\n", WHEREARG, obj->id, (int)obj->size);
			size_t x;
			for(x = 0; x < obj->size; x++)
				if (spu_ls[x] != ea[x])
				{
					printf(WHERESTR "Found mismatch in byte %d of %d bytes\n", WHEREARG, x, (int)obj->size);
					sleep(2);
				}
			printf(WHERESTR "Done testing %d bytes\n", WHEREARG, (int)obj->size);
		}*/
		
		/*printf(WHERESTR "Operation: %d\n", WHEREARG, preq->operation);
		printf(WHERESTR "Objid: %d\n", WHEREARG, preq->objId);
		printf(WHERESTR "DMACount: %d\n", WHEREARG, preq->DMAcount);
		printf(WHERESTR "requestId: %d\n", WHEREARG, preq->requestId);
		printf(WHERESTR "Pointer: %d\n", WHEREARG, (unsigned int)preq);*/
			
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
	FREE(preq);
	preq = NULL;

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
	FREE(preq);
	preq = NULL;

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
	{
		EnqueInvalidateResponse(requestId);
	}
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
		{	
			printf(WHERESTR "The Invalidate was for an object in READ mode\n", WHEREARG);
		}	
#endif
		
		
		if (obj->count == 0 || obj->mode == ACQUIRE_MODE_READ)
		{
			if (obj->id != id)
				REPORT_ERROR("Corrupted memory detected");
			if (requestId == UINT_MAX)
				REPORT_ERROR("Bad requestId");
			obj->invalidateId = requestId;
			if (obj->count == 0)
				spuhandler_DisposeObject(state, obj);
		}
	}
}

void spuhandler_PrintDebugStatus(struct SPU_State* state, unsigned int requestId)
{
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)requestId);
	if (preq == NULL) 
	{
		REPORT_ERROR2("No entry for requestId %d", requestId);
	}
	else
	{
		printf(WHERESTR "Preq stats: DMACount: %d, requestId: %d, objId: %d, operation: %d (%s)\n", WHEREARG,
		preq->DMAcount,
		preq->requestId,
		preq->objId,
		preq->operation,
		PACKAGE_NAME(preq->operation)
		);
		
		struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
		if (obj == NULL)
		{
			REPORT_ERROR2("No entry for obj %d", preq->objId);
		}
		else
		{
			printf(WHERESTR "Obj stats, count: %d, DMAId: %d, EA: %d, Id: %d, invalidateId: %d, isDmaToSpu: %d, LS: %u, mode: %d, size: %lu, wbr: %d\n", WHEREARG,
			obj->count, obj->DMAId, (unsigned int)obj->EA, obj->id, obj->invalidateId, obj->isDMAToSPU, 
			(unsigned int)obj->LS, obj->mode, obj->size, obj->writebuffer_ready);
		}
		
		sleep(1);
		exit(-5);
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
			case PACKAGE_DEBUG_PRINT_STATUS:
				spe_out_intr_mbox_read(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spuhandler_PrintDebugStatus(state, requestId);
				break;
			default:
				REPORT_ERROR2("Unknown package code recieved: %d", packageCode);			
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
			FREE(resp);
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
	while(!spu_terminate)
	{

		pending_out_data = 0;

		//Lock the mutex once, and read all the data
		//printf(WHERESTR "locking mutex\n", WHEREARG);
		pthread_mutex_lock(&spu_rq_mutex);
		//printf(WHERESTR "locked mutex\n", WHEREARG);

		for(i = 0; i < spe_thread_count; i++)
			spuhandler_HandleRequestCoordinatorMessages(&spu_states[i]);

		pthread_mutex_unlock(&spu_rq_mutex);

		for(i = 0; i < spe_thread_count; i++)
		{
			//For each SPU, just repeat this
			spuhandler_SPUMailboxReader(&spu_states[i]);
			spuhandler_HandleDMAEvent(&spu_states[i]);
			pending_out_data |= spuhandler_SPUMailboxWriter(&spu_states[i]);
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
	
	if ((spu_states = MALLOC(sizeof(struct SPU_State) * thread_count)) == NULL)
		REPORT_ERROR("malloc error");
		
	if (pthread_mutex_init(&spu_rq_mutex, NULL) != 0) REPORT_ERROR("Mutex initialization failed");

	//DMA_SEQ_NO = 0;
	
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
		spu_states[i].releaseSeqNo = 0;
		spu_states[i].streamItems = g_queue_new();
		
		RegisterInvalidateSubscriber(&spu_rq_mutex, NULL, &spu_states[i].queue);
	}


	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	pthread_create(&spu_mainthread, &attr, SPU_MainThread, NULL);
}

//This function cleans up used resources 
void TerminateSPUHandler(int force)
{
	size_t i;
	
	//Remove warning
	spu_terminate = force | TRUE;
	
	pthread_join(spu_mainthread, NULL); 
	
	pthread_mutex_destroy(&spu_rq_mutex);

	for(i = 0; i < spe_thread_count; i++)
	{
		g_hash_table_destroy(spu_states[i].activeDMATransfers);
		g_queue_free(spu_states[i].agedObjectMap);
		spu_states[i].agedObjectMap = NULL;
		g_hash_table_destroy(spu_states[i].itemsById);
		g_hash_table_destroy(spu_states[i].itemsByPointer);
		g_queue_free(spu_states[i].mailboxQueue);
		spu_states[i].mailboxQueue = NULL;
		g_hash_table_destroy(spu_states[i].pendingRequests);
		g_queue_free(spu_states[i].releaseWaiters);
		spu_states[i].releaseWaiters = NULL;
		g_queue_free(spu_states[i].queue);
		spu_states[i].queue = NULL;
		g_queue_free(spu_states[i].streamItems);
		spu_states[i].streamItems = NULL;

		spu_memory_destroy(spu_states[i].map);

		UnregisterInvalidateSubscriber(&spu_states[i].queue);
	}
	
	FREE(spu_states);
	spu_states = NULL;
	
}

#include <pthread.h>
#include <unistd.h>
#include <SPU_MemoryAllocator.h>
#include <SPU_MemoryAllocator_Shared.h>
#include <SPUEventHandler.h>
#include <RequestCoordinator.h>
#include <debug.h>
#include <datapackages.h>
#include <NetworkHandler.h>
#include <glib.h>

//TOTO: Remove ask Morten
unsigned int doPrint = FALSE;

//Indicate if Events should be used instead of the great spinning lock.
//#define USE_EVENTS

//The number of available DMA group ID's
//NOTE: On the PPU this is 0-15, NOT 0-31 as on the SPU! 
#define MAX_DMA_GROUPID 16

//The default number of hardware threads used for spinning
#define DEFAULT_PPU_THREAD_COUNT 1

//Disable keeping data on the SPU after release
//#define DISABLE_SPU_CACHE

//#define DEBUG_COMMUNICATION
//#define DEBUG_EVENT

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

	struct spu_pendingRequest* preq;
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

	struct spu_dataObject* dataObj;
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
	//GHashTable* activeDMATransfers;
	struct spu_pendingRequest* activeDMATransfers[MAX_DMA_GROUPID];
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

	//This is the list of incoming work
	GQueue* inQueue;

	//The incoming queue mutex
	pthread_mutex_t inMutex;

#ifdef USE_EVENTS
	//The incoming queue condition
	pthread_cond_t inCond;
#endif
	//This is a list of acquireRespons packages that cannot be forwarded until a 
	//releaseResponse arrives and free's memory
	GQueue* releaseWaiters;

	//This is the stream queue
	GQueue* streamItems;

	//The main thread
	pthread_t main;

#ifdef USE_EVENTS
	//The thread that writes the inbox
	pthread_t inboxWriter;

	//The thread that read DMA events
	pthread_t eventHandler;

	//The mutex that protects the writer queue
	pthread_mutex_t writerMutex;
	//The condition used to signal the writer thread
	pthread_cond_t writerCond;
#endif

	//The list of messages to send to the SPU
	GQueue* writerQueue;
	//A dirty flag used to bypass the SPU writer
	volatile unsigned int writerDirtyReadFlag;
	//Flag indicating if threads should shutdown
	unsigned int shutdown;

	struct spu_pendingRequest* pendingRequestsPointer[MAX_DMA_GROUPID];
	unsigned int currentPendingRequest;
};



//This is an array of SPU states
struct SPU_State* spu_states;

//This is the SPU main threads
pthread_t* spu_mainthread = NULL;

//This is the number of SPU's allocated 
unsigned int spe_thread_count;

//This is the number of threads in use for spinning
unsigned int spe_ppu_threadcount = DEFAULT_PPU_THREAD_COUNT;


//This is the flag that is used to terminate the SPU event handler 
volatile unsigned int spu_terminate;

//Declarations for functions that have interdependencies
void spuhandler_HandleObjectRelease(struct SPU_State* state, struct spu_dataObject* obj);
void spuhandler_HandleDMATransferCompleted(struct SPU_State* state, unsigned int groupID);

struct spu_pendingRequest* MallocPendingRequest(struct SPU_State* state)
{
	state->currentPendingRequest = (state->currentPendingRequest + 1) % MAX_DMA_GROUPID;
	return state->pendingRequestsPointer[state->currentPendingRequest];
}

void DSMCBE_SetHardwareThreads(unsigned int count)
{
	if (spu_mainthread != NULL) {
		REPORT_ERROR("Unable to set hardware thread count after initialize is called");
	} else {
		spe_ppu_threadcount = count;
	}
}

void spuhandler_SendMessagesToSPU(struct SPU_State* state, unsigned int packageCode, unsigned int requestId, unsigned int data, unsigned int size)
{
	//printf(WHERESTR "Sending mbox response to SPU: %s (%d)\n", WHEREARG, PACKAGE_NAME(packageCode), packageCode);

	if (state->writerDirtyReadFlag == 0)
	{
		//If there is space, send directly
		switch(packageCode)
		{
			case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
				if (spe_in_mbox_status(state->context) >= 1)
				{
					spe_in_mbox_write(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					return;
				}
			case PACKAGE_TERMINATE_RESPONSE:
			case PACKAGE_SPU_MEMORY_MALLOC_RESPONSE:
				if (spe_in_mbox_status(state->context) >= 2)
				{
					spe_in_mbox_write(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &data, 1, SPE_MBOX_ALL_BLOCKING);
					return;
				}
			case PACKAGE_ACQUIRE_RESPONSE:
				if (spe_in_mbox_status(state->context) >= 3)
				{
					spe_in_mbox_write(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &data, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &size, 1, SPE_MBOX_ALL_BLOCKING);
					return;
				}
			default:
				break;
		}
	}

#ifdef USE_EVENTS
	printf(WHERESTR "Mbox is full, forwarding to writer: %d\n", WHEREARG, packageCode);

	//Forward to writer
	struct internalMboxArgs* args = MALLOC(sizeof(struct internalMboxArgs));
	args->packageCode = packageCode;
	args->requestId = requestId;
	args->data = data;
	args->size = size;

	pthread_mutex_lock(&state->writerMutex);
	state->writerDirtyReadFlag = 1;
	g_queue_push_tail(state->writerQueue, args);
	pthread_cond_signal(&state->writerCond);
	pthread_mutex_unlock(&state->writerMutex);
#else
	switch(packageCode)
	{
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			g_queue_push_tail(state->mailboxQueue, (void*)requestId);
			return;
		case PACKAGE_TERMINATE_RESPONSE:
		case PACKAGE_SPU_MEMORY_MALLOC_RESPONSE:
			g_queue_push_tail(state->mailboxQueue, (void*)requestId);
			g_queue_push_tail(state->mailboxQueue, (void*)data);
			return;
		case PACKAGE_ACQUIRE_RESPONSE:
			g_queue_push_tail(state->mailboxQueue, (void*)requestId);
			g_queue_push_tail(state->mailboxQueue, (void*)data);
			g_queue_push_tail(state->mailboxQueue, (void*)size);
			return;
		default:
			break;
	}
#endif
}

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
	
	if (state->terminated != UINT_MAX && g_hash_table_size(state->itemsById) == 0)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Signaling termination to SPU %d\n", WHEREARG, obj->id);
#endif
		spuhandler_SendMessagesToSPU(state, PACKAGE_TERMINATE_RESPONSE, state->terminated, PACKAGE_TERMINATE_RESPONSE, 0);
	}

	FREE(obj);
	obj = NULL;
}

//This function creates and forwards a message to the request coordinator
void spuhandler_SendRequestCoordinatorMessage(struct SPU_State* state, void* req)
{
	QueueableItem qi = MALLOC(sizeof(struct QueueableItemStruct));
	
	qi->callback = NULL;
	qi->dataRequest = req;
#ifdef USE_EVENTS
	qi->event = &state->inCond;
#else
	qi->event = NULL;
#endif
	qi->mutex = &state->inMutex;
	qi->Gqueue = &state->inQueue;
	
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
		spuhandler_SendMessagesToSPU(state, PACKAGE_TERMINATE_RESPONSE, state->terminated, 0, 0);

		state->shutdown = TRUE;
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
	
	//If the requested size is larger than the total available space, don't discard objects
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
	
	//While we have not received a valid pointer, and there are still objects to discard
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

	struct spu_dataObject* obj = NULL;

	if (packageCode == PACKAGE_ACQUIRE_REQUEST_READ && g_queue_is_empty(state->releaseWaiters))
	{
		obj = g_hash_table_lookup(state->itemsById, (void*)id);
		
		//TODO: If the transfer is a write, we can transfer anyway.
		//TODO: If the transfer is ongoing, we should not forward the request to the RC
		
		//If the object is present, not acquired or acquired in read mode, there is no pending invalidates
		// and data is not being transfered, we can return the pointer directly
		if (obj != NULL && (obj->count == 0 || obj->mode == ACQUIRE_MODE_READ) && obj->invalidateId == UINT_MAX && obj->DMAId == UINT_MAX)
		{
			obj->count++;
			obj->mode = ACQUIRE_MODE_READ;
			spuhandler_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, requestId, (unsigned int)obj->LS, obj->size);

			if (obj->count == 1)
				g_queue_remove(state->agedObjectMap, (void*)obj->id);
			return;
		}
	}
	
	//struct spu_pendingRequest* preq = MALLOC(sizeof(struct spu_pendingRequest));
	struct spu_pendingRequest* preq = MallocPendingRequest(state);

	// Setting DMAcount to UINT_MAX to initialize the struct.
	preq->DMAcount = UINT_MAX;		
	//printf(WHERESTR "New pointer: %d\n", WHEREARG, (unsigned int)preq);

	preq->objId = id;
	preq->operation = packageCode;
	preq->requestId = requestId;
	preq->DMAcount = 0;
	preq->dataObj = obj;

	if (obj != NULL)
		obj->preq = preq;

	//printf(WHERESTR "Assigned reqId: %d\n", WHEREARG, preq->requestId);
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct acquireRequest* req = MALLOC(sizeof(struct acquireRequest));
	
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
		spuhandler_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, requestId, 0, 0);
		return;
	}
	
	//struct spu_pendingRequest* preq = MALLOC(sizeof(struct spu_pendingRequest));
	struct spu_pendingRequest* preq = MallocPendingRequest(state);
			
	// Setting DMAcount to UINT_MAX to initialize the struct.
	preq->DMAcount = UINT_MAX;
	preq->objId = id;
	preq->operation = PACKAGE_CREATE_REQUEST;
	preq->requestId = requestId;
	preq->DMAcount = 0;
	preq->dataObj = NULL;
	//printf(WHERESTR "Assigned reqId: %d\n", WHEREARG, preq->requestId);
	
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct createRequest* req = MALLOC(sizeof(struct createRequest));
	
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
void spuhandler_HandleBarrierRequest(struct SPU_State* state, unsigned int requestId, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling barrier request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

	//struct spu_pendingRequest* preq = MALLOC(sizeof(struct spu_pendingRequest));
	struct spu_pendingRequest* preq = MallocPendingRequest(state);

	// Setting DMAcount to UINT_MAX to initialize the struct.
	preq->DMAcount = UINT_MAX;		
	preq->objId = id;
	preq->operation = PACKAGE_ACQUIRE_BARRIER_REQUEST;
	preq->requestId = requestId;
	preq->DMAcount = 0;
	//printf(WHERESTR "Assigned reqId: %d\n", WHEREARG, preq->requestId);
		
	g_hash_table_insert(state->pendingRequests, (void*)preq->requestId, preq);

	struct acquireBarrierRequest* req = MALLOC(sizeof(struct acquireBarrierRequest));
	
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

	preq->dataObj = obj;
	obj->preq = preq;


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
			obj->DMAId = NEXT_SEQ_NO(DMA_SEQ_NO, MAX_DMA_GROUPID);
			
			if (state->activeDMATransfers[obj->DMAId] != NULL)
				continue;

			state->activeDMATransfers[obj->DMAId] = preq;
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
		{
			fprintf(stderr, "* ERROR * " WHERESTR ": Attempted to release an object that was unknown: %d\n", WHEREARG,  (unsigned int)data);

			/*
			GHashTableIter iter;
			g_hash_table_iter_init(&iter, state->itemsById);
			GUID key;
			struct spu_dataObject* value;

			printf(WHERESTR "Printing itemsById\n", WHEREARG);
			while(g_hash_table_iter_next(&iter, (void*)&key, (void*)&value))
				printf(WHERESTR "Item %d is allocated at %d and takes up %d bytes, count: %d\n", WHEREARG, key, (unsigned int)value->LS, (unsigned int)value->size, value->count);

			printf(WHERESTR "Done printing\n", WHEREARG);
			printf(WHERESTR "Printing itemsByPointer\n", WHEREARG);

			g_hash_table_iter_init(&iter, state->itemsByPointer);
			while(g_hash_table_iter_next(&iter, (void*)&key, (void*)&value))
				printf(WHERESTR "Item %d is allocated at %d and takes up %d bytes, count: %d\n", WHEREARG, key, (unsigned int)value->LS, (unsigned int)value->size, value->count);

			printf(WHERESTR "Done printing\n", WHEREARG);
			*/
		}
		else
			fprintf(stderr, "* ERROR * " WHERESTR ": Attempted to release an object that was not acquired: %d\n", WHEREARG,  (unsigned int)data);
		
		return;
	}

	//Read releases are handled locally
	if (obj->mode == ACQUIRE_MODE_READ)
	{
		//printf(WHERESTR "Handling read release %d, @%d\n", WHEREARG, obj->id, (unsigned int)data);
		spuhandler_HandleObjectRelease(state, obj);
	}
	else /*if (obj->mode == ACQUIRE_MODE_WRITE)*/
	{
		//Get a group id, and register the active transfer
		//struct spu_pendingRequest* preq = MALLOC(sizeof(struct spu_pendingRequest));
		struct spu_pendingRequest* preq = MallocPendingRequest(state);
		
		preq->objId = obj->id;
		preq->requestId = NEXT_SEQ_NO(state->releaseSeqNo, MAX_PENDING_RELEASE_REQUESTS) + RELEASE_NUMBER_BASE;
		preq->operation = PACKAGE_RELEASE_REQUEST;
		preq->DMAcount = (MAX(ALIGNED_SIZE(obj->size), SPE_DMA_MAX_TRANSFERSIZE) + (SPE_DMA_MAX_TRANSFERSIZE - 1)) / SPE_DMA_MAX_TRANSFERSIZE;

		obj->isDMAToSPU = FALSE;

		//Initiate the DMA transfer if the buffer is ready
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
				/*
				if (g_hash_table_size(state->activeDMATransfers) >= MAX_DMA_GROUPID)
				{
					REPORT_ERROR("DMA Sequence number overflow! This won't be pretty!");
					exit(-6);
				}
				*/
				
				obj->DMAId = NEXT_SEQ_NO(DMA_SEQ_NO, MAX_DMA_GROUPID);
				//if (g_hash_table_lookup(state->activeDMATransfers, (void*)obj->DMAId) != NULL)
				if (state->activeDMATransfers[obj->DMAId] != NULL)
					continue;

				//g_hash_table_insert(state->activeDMATransfers, (void*)obj->DMAId, preq);
				state->activeDMATransfers[obj->DMAId] = preq;
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
		REPORT_ERROR2("Received a writebuffer ready request, but object %d did not exist", id);
		return;
	}
	
	if (obj->writebuffer_ready == TRUE)
		REPORT_ERROR("Writebuffer ready already set?");
	obj->writebuffer_ready = TRUE;

	//If we are just waiting for the signal, then start the DMA transfer
	//The DMAId is not set until the object is released
	if (obj->DMAId != UINT_MAX && !obj->isDMAToSPU)
	{
		//struct spu_pendingRequest* preq = g_hash_table_lookup(state->activeDMATransfers, (void*)obj->DMAId);
		struct spu_pendingRequest* preq = state->activeDMATransfers[obj->DMAId];
		if (preq == NULL) {
			REPORT_ERROR2("Delayed DMA write for %d was missing control object!", obj->DMAId);
			exit(-5);
		}
		else
		{
			//g_hash_table_remove(state->activeDMATransfers, (void*)obj->DMAId);
			state->activeDMATransfers[obj->DMAId] = NULL;
		}
		
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "WriteBufferReady triggered a DMA transfer: %d\n", WHEREARG, id);
#endif
		
		spuhandler_TransferObject(state, preq, obj);
	}
}

//This function calculates the amount of space that is in the process of being released,
// and thus will be available for use
unsigned int spuhandler_EstimatePendingReleaseSize(struct SPU_State* state)
{
	GHashTableIter iter;
	g_hash_table_iter_init(&iter, state->pendingRequests);
	void* key;
	void* value;
	unsigned int pendingSize = 0;
	struct spu_dataObject* obj;
	size_t i;

	while(g_hash_table_iter_next(&iter, &key, (void**)&value))
	{
		struct spu_pendingRequest* v = (struct spu_pendingRequest*)value;
		if (v->operation == PACKAGE_RELEASE_REQUEST)
		{
			//obj = g_hash_table_lookup(state->itemsById, (void*)v->objId);
			obj = v->dataObj;
			if (obj != NULL)
			{
#ifdef DEBUG_COMMUNICATION	
				printf(WHERESTR "Found obj %d with size %d in pending\n", WHEREARG, obj->id, obj->size);
#endif
				pendingSize += obj->size;
			}
		}
	}

	key = value = NULL;

	for (i = 0; i < MAX_DMA_GROUPID; i++)
	{
		if (state->activeDMATransfers[i] != NULL)
		{
			if (state->activeDMATransfers[i]->operation == PACKAGE_RELEASE_REQUEST)
			{
				obj = g_hash_table_lookup(state->itemsById, (void*)state->activeDMATransfers[i]->objId);
				if (obj != NULL)
				{
	#ifdef DEBUG_COMMUNICATION
					printf(WHERESTR "Found obj %d with size %d in pending\n", WHEREARG, obj->id, obj->size);
	#endif
					pendingSize += obj->size;
				}
			}
		}
	}
	/*
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
	*/

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

	if (preq->objId != data->dataItem)
	{
		REPORT_ERROR2("Found invalid response, reqested Id: %d", preq->objId);
		REPORT_ERROR2("----------------------, actual   Id: %d", data->dataItem);
	}
	
	//TODO: If two threads acquire the same object, we cannot respond until the DMA is complete
	
	//Determine if data is already present on the SPU
	struct spu_dataObject* obj = preq->dataObj;

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
				
				//sleep(5);

				spuhandler_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, preq->requestId, 0, 0);

				g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);

				//FREE(preq);
				//exit(-5);
				return TRUE;
				
			}
		} 
			

#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling acquire response for id: %d, requestId: %d, object did not exist, creating\n", WHEREARG, preq->objId, data->requestID);
#endif

		obj = MALLOC(sizeof(struct spu_dataObject));

		if (obj == NULL)
			REPORT_ERROR("Malloc returned NULL")

		obj->count = 1;
		obj->EA = data->data;
		obj->id = preq->objId;
		obj->invalidateId = UINT_MAX;
		obj->mode = preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
		obj->size = data->dataSize;
		obj->LS = ls;
		obj->writebuffer_ready = (preq->operation == PACKAGE_CREATE_REQUEST || data->writeBufferReady);
		obj->isDMAToSPU = TRUE;

		g_hash_table_insert(state->itemsById, (void*)obj->id, obj);
		g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);

		if (ls == NULL)
		{
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "No more space, delaying acquire until space becomes avalible, free mem: %d, reqSize: %d\n", WHEREARG, state->map->free_mem, data->dataSize);
#endif
			if (g_queue_find(state->releaseWaiters, preq) == NULL)
				g_queue_push_tail(state->releaseWaiters, preq);
		}
		else
		{
			//Send it!
			g_hash_table_insert(state->itemsByPointer, obj->LS, obj);
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
		obj->writebuffer_ready = preq->operation == PACKAGE_CREATE_REQUEST || data->writeBufferReady;
		
		spuhandler_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, preq->requestId, (unsigned int)obj->LS, obj->size);

		//if (preq->operation != PACKAGE_ACQUIRE_REQUEST_WRITE)
		{
			g_hash_table_remove(state->pendingRequests, (void*)preq->requestId);
			obj->preq = NULL;
			//FREE(preq);
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
			{
				REPORT_ERROR("Out of memory on SPU");
				exit(-3);
			}
			return;
		}
		else
		{
			g_queue_pop_head(state->releaseWaiters);
			g_hash_table_insert(state->itemsByPointer, obj->LS, obj);

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
	//struct spu_pendingRequest* preq = g_hash_table_lookup(state->activeDMATransfers, (void*)groupID);
	struct spu_pendingRequest* preq = state->activeDMATransfers[groupID];

	if (preq == NULL)
	{
		REPORT_ERROR("DMA completed, but was not initiated");
		return;
	}

	//g_hash_table_remove(state->activeDMATransfers, (void*)groupID);
	state->activeDMATransfers[groupID] = NULL;

	//Get the corresponding data object
	struct spu_dataObject* obj = preq->dataObj;
	//struct spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
	if (obj == NULL)
	{
		//FREE(preq);
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
		
		spuhandler_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, preq->requestId, (unsigned int)obj->LS, obj->size);

		obj->preq = NULL;
		//FREE(preq);
		preq = NULL;
	}
	else if(preq->DMAcount == 0)
	{
		//Data is transfered from SPU LS to EA, now notify the request coordinator
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, id: %d, notifying RC\n", WHEREARG, groupID, preq->objId);
#endif
		//__asm__ __volatile__("lwsync" : : : "memory");
		
		struct releaseRequest* req = MALLOC(sizeof(struct releaseRequest));
			
		req->data = obj->EA;
		req->dataItem = obj->id;
		req->dataSize = obj->size;
		req->mode = ACQUIRE_MODE_WRITE;
		req->offset = 0;
		req->packageCode = PACKAGE_RELEASE_REQUEST;
		req->requestID = preq->requestId;
		
		spuhandler_SendRequestCoordinatorMessage(state, req);

		obj->preq = NULL;
		//FREE(preq);
		if (obj == NULL || obj->count == 0)
		{
			REPORT_ERROR("Release was completed, but the object was not acquired?");
			return;
		}
		spuhandler_HandleObjectRelease(state, obj);
	}
}

//This function handles incoming barrier responses from the request coordinator
void spuhandler_HandleBarrierResponse(struct SPU_State* state, unsigned int requestId)
{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Handling barrier response for requestId: %d\n", WHEREARG, requestId);
#endif	
	struct spu_pendingRequest* preq = g_hash_table_lookup(state->pendingRequests, (void*)requestId);
	if (preq == NULL)
	{
		REPORT_ERROR("Get release response for non initiated request");
		return;
	}

	g_hash_table_remove(state->pendingRequests, (void*)requestId);
	//FREE(preq);
	preq = NULL;

	spuhandler_SendMessagesToSPU(state, PACKAGE_ACQUIRE_BARRIER_RESPONSE, requestId, 0, 0);
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
		else
			REPORT_ERROR2("The Invalidate was for %d in NON read mode or obj->count was NOT 0", obj->id);
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

void ForwardInternalMbox(struct SPU_State* state, struct internalMboxArgs* args)
{
	pthread_mutex_lock(&state->inMutex);
	g_queue_push_tail(state->inQueue, args);

#ifdef USE_EVENTS
	pthread_cond_signal(&state->inCond);
#endif

	pthread_mutex_unlock(&state->inMutex);
}

//Reads an processes any incoming mailbox messages
void spuhandler_MailboxHandler(struct SPU_State* state, struct internalMboxArgs* args)
{
	if (args != NULL)
	{
		switch(args->packageCode)
		{
			case PACKAGE_TERMINATE_REQUEST:
#ifdef DEBUG_COMMUNICATION
				printf(WHERESTR "Terminate request received\n", WHEREARG);
#endif
				state->terminated = args->requestId;
				spuhandler_DisposeAllObject(state);
				break;
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				spuhandler_HandleAcquireRequest(state, args->requestId, args->id, args->packageCode);
				break;
			case PACKAGE_CREATE_REQUEST:
				spuhandler_HandleCreateRequest(state, args->requestId, args->id, args->size);
				break;
			case PACKAGE_RELEASE_REQUEST:
				spuhandler_HandleReleaseRequest(state, (void*)args->requestId);
				break;
			case PACKAGE_SPU_MEMORY_FREE:
				spu_memory_free(state->map, (void*)args->requestId);
				break;
			case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
				spuhandler_SendMessagesToSPU(state, PACKAGE_SPU_MEMORY_MALLOC_RESPONSE, args->requestId, (unsigned int)spuhandler_AllocateSpaceForObject(state, args->size), 0);
				break;
			case PACKAGE_ACQUIRE_BARRIER_REQUEST:
				spuhandler_HandleBarrierRequest(state, args->requestId, args->id);
				break;
			case PACKAGE_DEBUG_PRINT_STATUS:
				spuhandler_PrintDebugStatus(state, args->requestId);
				break;
			default:
				REPORT_ERROR2("Unknown package code received: %d", args->packageCode);
				break;
		}

		FREE(args);
		args = NULL;
	}
}

void spuhandler_SPUMailboxReader(struct SPU_State* state)
{
	struct internalMboxArgs* args = MALLOC(sizeof(struct internalMboxArgs));

#ifndef USE_EVENTS
	if (spe_out_intr_mbox_status(state->context) <= 0)
		return;
#endif

	spe_out_intr_mbox_read(state->context, &args->packageCode, 1, SPE_MBOX_ALL_BLOCKING);

	//printf(WHERESTR "Read mbox: %s (%d)\n", WHEREARG, PACKAGE_NAME(args->packageCode), args->packageCode);

	switch(args->packageCode)
	{
		case PACKAGE_TERMINATE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_SPU_MEMORY_SETUP:
			if (state->map != NULL)
			{
				REPORT_ERROR("Tried to re-initialize SPU memory map");
			}
			else
			{
				state->terminated = UINT_MAX;
				spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
				spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
				state->map = spu_memory_create((unsigned int)args->requestId, args->size);
				FREE(args);
				args = NULL;
			}
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_CREATE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_RELEASE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_SPU_MEMORY_FREE:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_DEBUG_PRINT_STATUS:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		default:
			REPORT_ERROR2("Unknown package code recieved: %d", args->packageCode);
			break;
	}

	if (args != NULL)
	{
#ifdef USE_EVENTS
		ForwardInternalMbox(state, args);
#else
		spuhandler_MailboxHandler(state, args);
#endif
	}
}

//This function writes pending data to the spu mailbox while there is room
int spuhandler_SPUMailboxWriter(struct SPU_State* state)
{
	while (!g_queue_is_empty(state->mailboxQueue) && spe_in_mbox_status(state->context) != 0)
	{
		unsigned int data = (unsigned int)state->mailboxQueue->head->data;

		//printf(WHERESTR "Sending Mailbox message: %i\n", WHEREARG, (unsigned int)Gspu_mailboxQueues[i]->head->data);
		if (spe_in_mbox_write(state->context, &data, 1, SPE_MBOX_ALL_BLOCKING) != 1)
			REPORT_ERROR("Failed to send message, even though it was blocking!")
		else
			g_queue_pop_head(state->mailboxQueue);
	}

	return !g_queue_is_empty(state->mailboxQueue);
}

#ifdef USE_EVENTS
void* spuhandler_thread_SPUMailboxWriter(void* outdata)
{
	struct SPU_State* state = (struct SPU_State*)outdata;
	struct timespec ts;

	while(!state->shutdown || g_queue_get_length(state->writerQueue) > 0)
	{
		pthread_mutex_lock(&state->writerMutex);

		while (g_queue_get_length(state->writerQueue) == 0)
		{
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 5;

			state->writerDirtyReadFlag = 0;

#ifdef DEBUG_EVENT
			printf(WHERESTR "Writer waiting for work\n", WHEREARG);
#endif

			pthread_cond_timedwait(&state->writerCond, &state->writerMutex, &ts);

#ifdef DEBUG_EVENT
			printf(WHERESTR "Writer got work or timeout\n", WHEREARG);
#endif

			if (state->shutdown)
			{
				printf(WHERESTR "SPUMailboxWriter terminating\n", WHEREARG);
				pthread_mutex_unlock(&state->writerMutex);
				return NULL;
			}

			if (g_queue_get_length(state->writerQueue) > 0)
				printf(WHERESTR "We need to write\n", WHEREARG);

			state->writerDirtyReadFlag = 1;
		}

#ifdef DEBUG_EVENT
		printf(WHERESTR "We need to write\n", WHEREARG);
#endif
		struct internalMboxArgs* args = (struct internalMboxArgs*)g_queue_pop_head(state->writerQueue);

		pthread_mutex_unlock(&state->writerMutex);

#ifdef DEBUG_EVENT
		printf(WHERESTR "Writing to Mailbox from thread, %s (%d) \n", WHEREARG, PACKAGE_NAME(args->packageCode), args->packageCode);
#endif

		//TODO: Should be able to send multiple items
		switch(args->packageCode)
		{
			case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
				spe_in_mbox_write(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
				break;
			case PACKAGE_TERMINATE_RESPONSE:
			case PACKAGE_SPU_MEMORY_MALLOC_RESPONSE:
				spe_in_mbox_write(state->context, &args->requestId, 2, SPE_MBOX_ALL_BLOCKING);
				break;
			case PACKAGE_ACQUIRE_RESPONSE:
				spe_in_mbox_write(state->context, &args->requestId, 3, SPE_MBOX_ALL_BLOCKING);
				break;
			default:
				break;
		}

		FREE(args);
		args = NULL;
	}

	printf(WHERESTR "WARNING: SPUMailboxWriter saying goodbye\n", WHEREARG);

	return NULL;
}
#endif

//This function handles completion of a DMA event
void spuhandler_HandleDMAEvent(struct SPU_State* state)
{
	size_t i;
	unsigned int mask = 0;

	//Read the current transfer mask
	spe_mfcio_tag_status_read(state->context, 0, SPE_TAG_IMMEDIATE, &mask);

	//No action, so quickly return
	if (mask == 0)
		return;

	for(i = 0; i < MAX_DMA_GROUPID; i++)
		if (state->activeDMATransfers[i] != NULL && (mask & (1 << ((unsigned int)i))) != 0)
			spuhandler_HandleDMATransferCompleted(state, ((unsigned int)i));
}


//This function handles completion of a DMA event
void* spuhandler_thread_HandleEvents()
{
	unsigned int mask = 0;
	size_t i = 0;

	spe_event_unit_t* eventArgsReg = MALLOC(sizeof(spe_event_unit_t) * spe_thread_count);
	spe_event_unit_t* eventPend = MALLOC(sizeof(spe_event_unit_t) * spe_thread_count);
	unsigned int* SPU_IsReg = MALLOC(sizeof(unsigned int) * spe_thread_count);

	spe_event_handler_ptr_t eventhandler = spe_event_handler_create();

	//printf(WHERESTR "SPU count is %d\n", WHEREARG, spe_thread_count);

	for(i = 0; i < spe_thread_count; i++)
	{
		eventArgsReg[i].events = (SPE_EVENT_ALL_EVENTS) & (~SPE_EVENT_IN_MBOX);
		eventArgsReg[i].spe = spu_states[i].context;
		eventArgsReg[i].data.u64 = 0;
		eventArgsReg[i].data.u32 = i;

		if (spe_event_handler_register(eventhandler, &eventArgsReg[i]) != 0)
		{
			REPORT_ERROR("Event Handler Register failed");
		}
		else
			SPU_IsReg[i] = 1;
	}

	int eventCount = 0;
	int j = 0;

	unsigned int doStop = FALSE;

	while(!doStop)
	{
		mask = 0;
		eventCount = 0;

		if (doPrint)
		{
			doStop = TRUE;

			for(i = 0; i < spe_thread_count; i++)
				if (!spu_states[i].shutdown)
					doStop = FALSE;

			if (doStop)
			{
				printf(WHERESTR "Stopping Event handler thread\n", WHEREARG);
				return NULL;
			}
		}

#ifdef DEBUG_EVENT
		printf(WHERESTR "Waiting for new event\n", WHEREARG);
#endif

		if ((eventCount = spe_event_wait(eventhandler, eventPend, spe_thread_count, 5000)) < 1)
		{
			REPORT_ERROR("Event Wait Failed");
			//eventArgsIn.events = SPE_EVENT_SPE_STOPPED;
		}

#ifdef DEBUG_EVENT
		printf(WHERESTR "Recieved event\n", WHEREARG);
#endif

		for(j = 0; j < eventCount; j++)
		{
			struct SPU_State* state = &spu_states[eventPend[j].data.u32];

			if (SPU_IsReg[eventPend[j].data.u32] == 0)
			{
#ifdef DEBUG_EVENT
				printf(WHERESTR "Skipped event\n", WHEREARG);
#endif
				continue;
			}

			if ((eventPend[j].events & SPE_EVENT_TAG_GROUP) == SPE_EVENT_TAG_GROUP)
			{
#ifdef DEBUG_EVENT
				printf(WHERESTR "DMA event\n", WHEREARG);
#endif

				//Read the current transfer mask
				spe_mfcio_tag_status_read(state->context, 0, SPE_TAG_ANY, &mask);

				struct internalMboxArgs* args = (struct internalMboxArgs*)MALLOC(sizeof(struct internalMboxArgs));

				args->packageCode = PACKAGE_DMA_COMPLETE;
				args->requestId = mask;

				//Insert package into inQueue
				pthread_mutex_lock(&state->inMutex);

				g_queue_push_tail(state->inQueue, args);

#ifdef USE_EVENTS
				pthread_cond_signal(&state->inCond);
#endif
				pthread_mutex_unlock(&state->inMutex);
			}

			if ((eventPend[j].events & SPE_EVENT_OUT_INTR_MBOX) == SPE_EVENT_OUT_INTR_MBOX)
			{
#ifdef DEBUG_EVENT
				printf(WHERESTR "Out MBOX event\n", WHEREARG);
#endif

				spuhandler_SPUMailboxReader(state);
			}

			if ((eventPend[j].events & SPE_EVENT_IN_MBOX) == SPE_EVENT_IN_MBOX)
			{
#ifdef DEBUG_EVENT
				printf(WHERESTR "In MBOX event\n", WHEREARG);
#endif
			}

			if ((eventPend[j].events & SPE_EVENT_SPE_STOPPED) == SPE_EVENT_SPE_STOPPED)
			{
				printf(WHERESTR "In terminate event\n", WHEREARG);
				doPrint = TRUE;

				if (SPU_IsReg[eventPend[j].data.u32] == 1)
				{
					printf(WHERESTR "Deregistering\n", WHEREARG);
					spe_event_handler_deregister(eventhandler, &eventArgsReg[eventPend[j].data.u32]);
					printf(WHERESTR "Done deregistering\n", WHEREARG);
					SPU_IsReg[eventPend[j].data.u32] = 0;
				}
				else
					printf(WHERESTR "\n\n\n************* Hmm... ****************\n\n\n", WHEREARG);

				//spe_event_handler_destroy(eventhandler);
				//return NULL;
			}

			if ((eventPend[j].events & ~(SPE_EVENT_SPE_STOPPED | SPE_EVENT_IN_MBOX | SPE_EVENT_OUT_INTR_MBOX | SPE_EVENT_TAG_GROUP)) != 0)
				REPORT_ERROR2("Unknown SPE event: %d\n", eventPend[j].events);
		}
	}

	printf(WHERESTR "WARNING: SPU event handler saying Goodbye\n", WHEREARG);
	return NULL;
}

//This function reads and handles incoming requests and responses from the request coordinator
void spuhandler_HandleMessagesFromQueue(struct SPU_State* state, struct acquireResponse* resp)
{
	size_t i;

#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Handling request message: %s (%d)\n", WHEREARG, PACKAGE_NAME(resp->packageCode), resp->packageCode);
#endif
	switch(resp->packageCode)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			if (spuhandler_HandleAcquireResponse(state, resp) == FALSE)
				resp = NULL;
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
		case PACKAGE_DMA_COMPLETE:
			//See if the any of the completed transfers are in our wait list

			/*
			g_hash_table_iter_init(&iter, state->activeDMATransfers);

			while(g_hash_table_iter_next(&iter, &key, &value))
				if (((((struct internalMboxArgs*)resp)->requestId) & (1 << ((unsigned int)key))) != 0)
				{
					spuhandler_HandleDMATransferCompleted(state, ((unsigned int)key));

					//Re-initialize the iterator
					g_hash_table_iter_init(&iter, state->activeDMATransfers);
				}
			*/
			for(i = 0; i < MAX_DMA_GROUPID; i++)
				if (state->activeDMATransfers[i] != NULL && ((((struct internalMboxArgs*)resp)->requestId) & (1 << ((unsigned int)i))) != 0)
					spuhandler_HandleDMATransferCompleted(state, ((unsigned int)i));
			break;
		default:
			spuhandler_MailboxHandler(state, (struct internalMboxArgs*)resp);
			resp = NULL; //Handled in the mbox reader
			break;
	}

	if (resp != NULL)
		FREE(resp);

	resp = NULL;
}

//This function repeatedly checks for events relating to the SPU's
void* SPU_MainThreadSpinning(void* threadranges)
{
	size_t i;
	unsigned int pending_out_data;
	unsigned int spu_thread_min = ((unsigned int*)threadranges)[0];
	unsigned int spu_thread_max = ((unsigned int*)threadranges)[1];
	struct SPU_State* state = NULL;
	struct acquireResponse* resp = NULL;
	FREE(threadranges);

	printf(WHERESTR "This run is using SPINNING\n", WHEREARG);

	//Event base, keeps the mutex locked, until we wait for events
	while(!spu_terminate)
	{

		pending_out_data = 0;

		for(i = spu_thread_min; i < spu_thread_max; i++)
		{
			resp = NULL;
			state = &spu_states[i];

			while(TRUE)
			{
				//BEWARE: Dirty read:
				//Case 1: The item is zero, but items are in queue
				//    -> The processing will be defered to next round
				//Case 2: The item is non-zero, but no items are in queue
				//    -> The lock below prevents reading an empty queue
				if (state->inQueue->length == 0)
					break;

				pthread_mutex_lock(&state->inMutex);
				//Returns NULL if the queue is empty
				resp = g_queue_pop_head(state->inQueue);
				pthread_mutex_unlock(&state->inMutex);

				if (resp == NULL)
					break;

				spuhandler_HandleMessagesFromQueue(state, resp);
			}
		}

		for(i = spu_thread_min; i < spu_thread_max; i++)
		{
			state = &spu_states[i];

			//For each SPU, just repeat this
			spuhandler_SPUMailboxReader(state);
			spuhandler_HandleDMAEvent(&spu_states[i]);
			pending_out_data |= spuhandler_SPUMailboxWriter(&spu_states[i]);
		}
	}

	printf(WHERESTR "Main thread stopping\n", WHEREARG);

	return NULL;
}

//This function repeatedly checks for events relating to the SPU's
void* SPU_MainThreadEvents(void* input)
{
	struct SPU_State* state = (struct SPU_State*)input;
	void* req = NULL;
	struct timespec ts;

	printf(WHERESTR "This run is using EVENTS\n", WHEREARG);

	while(!state->shutdown)
	{
		pthread_mutex_lock(&state->inMutex);
		while(g_queue_get_length(state->inQueue) == 0)
		{
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 5;

#ifdef DEBUG_EVENT
				printf(WHERESTR "Waiting for message\n", WHEREARG);
#endif

#ifdef USE_EVENTS
			pthread_cond_timedwait(&state->inCond, &state->inMutex, &ts);
#endif

#ifdef DEBUG_EVENT
			printf(WHERESTR "Got new message\n", WHEREARG);
#endif

			if (state->shutdown)
			{
				printf(WHERESTR "MainThread terminating\n", WHEREARG);
				pthread_mutex_unlock(&state->inMutex);
				return NULL;
			}
		}

		req = g_queue_pop_head(state->inQueue);
		pthread_mutex_unlock(&state->inMutex);

		spuhandler_HandleMessagesFromQueue(state, req);
	}

	printf(WHERESTR "WARNING: HandleRequest messages stopping\n", WHEREARG);

	return NULL;
}

//This function sets up the SPU event handler
void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count)
{
	pthread_attr_t attr;
	size_t i,j;
	
	spe_thread_count = thread_count;
	spu_terminate = TRUE;
	
	if (thread_count == 0)
		return;

	spu_terminate = FALSE;


	spu_states = MALLOC(sizeof(struct SPU_State) * thread_count);
	//if (pthread_mutex_init(&spu_rq_mutex, NULL) != 0) REPORT_ERROR("Mutex initialization failed");

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	//DMA_SEQ_NO = 0;
	
	for(i = 0; i < thread_count; i++)
	{
		spu_states[i].shutdown = FALSE;
		spu_states[i].agedObjectMap = g_queue_new();
		spu_states[i].context = threads[i];
		spu_states[i].dmaSeqNo = 0;
		spu_states[i].itemsById = g_hash_table_new(NULL, NULL);
		spu_states[i].itemsByPointer = g_hash_table_new(NULL, NULL);
		spu_states[i].mailboxQueue = g_queue_new();
		spu_states[i].map = NULL;
		spu_states[i].pendingRequests = g_hash_table_new(NULL, NULL);
		spu_states[i].releaseWaiters = g_queue_new();
		spu_states[i].inQueue = g_queue_new();
		spu_states[i].terminated = UINT_MAX;
		spu_states[i].releaseSeqNo = 0;
		spu_states[i].streamItems = g_queue_new();

		pthread_mutex_init(&spu_states[i].inMutex, NULL);

#ifdef USE_EVENTS
		pthread_cond_init(&spu_states[i].inCond, NULL);
		pthread_mutex_init(&spu_states[i].writerMutex, NULL);
		pthread_cond_init(&spu_states[i].writerCond, NULL);
#endif

		spu_states[i].writerDirtyReadFlag = 0;
		spu_states[i].writerQueue = g_queue_new();

#ifdef USE_EVENTS
		RegisterInvalidateSubscriber(&spu_states[i].inMutex, &spu_states[i].inCond, &spu_states[i].inQueue, -1);
#else
		RegisterInvalidateSubscriber(&spu_states[i].inMutex, NULL, &spu_states[i].inQueue, -1);
#endif

		for(j = 0; j < MAX_DMA_GROUPID; j++)
			spu_states[i].pendingRequestsPointer[j] = MALLOC(sizeof(struct spu_pendingRequest));

		spu_states[i].currentPendingRequest = 0;
	}

#ifdef USE_EVENTS
	//Start SPU mailbox readers
	for(i = 0; i < thread_count; i++)
	{
		if (pthread_create(&spu_states[i].main, &attr, SPU_MainThreadEvents, &spu_states[i]) != 0)
			REPORT_ERROR("Failed to start an SPU service thread")

		if (pthread_create(&spu_states[i].inboxWriter, &attr, spuhandler_thread_SPUMailboxWriter, &spu_states[i]) != 0)
			REPORT_ERROR("Failed to start an SPU service thread")
	}

	if (pthread_create(&spu_states[0].eventHandler, &attr, spuhandler_thread_HandleEvents, NULL) != 0)
		REPORT_ERROR("Failed to start an SPU service thread")
#else
		unsigned int cur_thread = 0;

		if (spe_thread_count < spe_ppu_threadcount)
			spe_ppu_threadcount = spe_thread_count;

		spu_mainthread = MALLOC(sizeof(pthread_t) * spe_ppu_threadcount);

		unsigned int* tmp = MALLOC(sizeof(unsigned int) * spe_ppu_threadcount);
		memset(tmp, 0, sizeof(unsigned int) * spe_ppu_threadcount);

		unsigned int remaining_spu_threads = spe_thread_count;

		i = 0;
		while(remaining_spu_threads > 0)
		{
			tmp[i]++;
			i = (i+1) % spe_ppu_threadcount;
			remaining_spu_threads--;
		}

		for(i = 0; i < spe_ppu_threadcount; i++)
		{
			//printf(WHERESTR "Starting thread %d with SPU %d to %d\n", WHEREARG, i, cur_thread, cur_thread + tmp[i]);
			//sleep(5);
			unsigned int* ranges = MALLOC(sizeof(unsigned int) * 2);
			ranges[0] = cur_thread;
			cur_thread += tmp[i];
			ranges[1] = cur_thread;
			pthread_create(&spu_mainthread[i], &attr, SPU_MainThreadSpinning, ranges);
		}

		FREE(tmp);
#endif
}

//This function cleans up used resources 
void TerminateSPUHandler(int force)
{
	printf("\n\n\n\n\n\n\n\n\n TERMINATING!!! \n\n\n\n\n\n\n\n\n");
	sleep(5);

	size_t i;
	
	//Remove warning
	spu_terminate = force | TRUE;
	
	for(i = 0; i < spe_ppu_threadcount; i++)
		pthread_join(spu_mainthread[i], NULL); 
	
	FREE(spu_mainthread);
	
	for(i = 0; i < spe_thread_count; i++)
	{
		UnregisterInvalidateSubscriber(&spu_states[i].inQueue);

		//g_hash_table_destroy(spu_states[i].activeDMATransfers);
		FREE(spu_states[i].activeDMATransfers);
		g_queue_free(spu_states[i].agedObjectMap);
		spu_states[i].agedObjectMap = NULL;
		g_hash_table_destroy(spu_states[i].itemsById);
		g_hash_table_destroy(spu_states[i].itemsByPointer);
		g_queue_free(spu_states[i].mailboxQueue);
		spu_states[i].mailboxQueue = NULL;
		g_hash_table_destroy(spu_states[i].pendingRequests);
		g_queue_free(spu_states[i].releaseWaiters);
		spu_states[i].releaseWaiters = NULL;
		g_queue_free(spu_states[i].inQueue);
		spu_states[i].inQueue = NULL;
		g_queue_free(spu_states[i].streamItems);
		spu_states[i].streamItems = NULL;
		spu_memory_destroy(spu_states[i].map);

		pthread_mutex_destroy(&spu_states[i].inMutex);

#ifdef USE_EVENTS
		pthread_cond_destroy(&spu_states[i].inCond);
		pthread_mutex_destroy(&spu_states[i].writerMutex);
		pthread_cond_destroy(&spu_states[i].writerCond);
#endif
	}
	
	FREE(spu_states);
	spu_states = NULL;
}

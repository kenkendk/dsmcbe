#include <pthread.h>
#include <unistd.h>
#include <SPU_MemoryAllocator.h>
#include <SPUEventHandler.h>
#include <RequestCoordinator.h>
#include <debug.h>
#include <datapackages.h>
#include <NetworkHandler.h>
#include <glib.h>
#include <dsmcbe_initializers.h>
#include <dsmcbe_csp_initializers.h>
#include <stdlib.h>
#include <SPUEventHandler_shared.h>
#include <dsmcbe_csp.h>
#include <SPUEventHandler_CSP.h>

//TOTO: Remove ask Morten
unsigned int doPrint = FALSE;

//Indicate if Events should be used instead of the great spinning lock.
//#define USE_EVENTS

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

//This is an array of SPU states
struct dsmcbe_spu_state* dsmcbe_spu_states;

//This is the SPU main threads
pthread_t* dsmcbe_spu_mainthread = NULL;

//This is the number of SPU's allocated 
unsigned int dsmcbe_spu_thread_count;

//This is the number of threads in use for spinning
unsigned int dsmcbe_spu_ppu_threadcount = DEFAULT_PPU_THREAD_COUNT;


//This is the flag that is used to terminate the SPU event handler 
volatile unsigned int dsmcbe_spu_do_terminate;

#ifdef USE_EVENTS

struct dsmcbe_spu_internalMboxArgs* dsmcbe_spu_new_internalMboxArgs(GUID id, unsigned int packageCode, unsigned int requestId, unsigned int size, unsigned int data)
{
	struct dsmcbe_spu_internalMboxArgs* res = (struct dsmcbe_spu_internalMboxArgs*)MALLOC(sizeof(struct dsmcbe_spu_internalMboxArgs));

	res->packageCode = packageCode;
	res->requestId = requestId;
	res->size = size;
	res->data = data;
	res->id = id;

	return res;
}

#endif

struct dsmcbe_spu_dataObject* dsmcbe_spu_new_dataObject(GUID id, unsigned int isCSP, unsigned int mode, unsigned int count, unsigned int DMAId, void* EA, void* LS, unsigned int invalidateId, unsigned long size, unsigned int writebuffer_ready, unsigned int isDMAToSPU, struct dsmcbe_spu_pendingRequest* preq)
{
	struct dsmcbe_spu_dataObject* res = (struct dsmcbe_spu_dataObject*)MALLOC(sizeof(struct dsmcbe_spu_dataObject));

	res->id = id;
	res->mode = mode;
	res->count = count;
	res->DMAId = DMAId;
	res->EA = EA;
	res->LS = LS;
	res->invalidateId = invalidateId;
	res->size = size;
	res->writebuffer_ready = writebuffer_ready;
	res->isDMAToSPU = isDMAToSPU;
	res->preq = preq;
	res->isCSP = isCSP;

	return res;
}

struct dsmcbe_spu_pendingRequest* dsmcbe_spu_new_PendingRequest(struct dsmcbe_spu_state* state, unsigned int requestId, unsigned int operation, GUID objId, struct dsmcbe_spu_dataObject* dataObj, unsigned int DMAcount, unsigned int isCSP)
{
	struct dsmcbe_spu_pendingRequest* preq;

	preq = state->pendingRequestsPointer[NEXT_SEQ_NO(state->currentPendingRequest, MAX_DMA_GROUPID)];

	//For most cases we simply skip this
	if (preq->requestId != UINT_MAX)
	{
		int i = MAX_DMA_GROUPID;
		while(preq->requestId != UINT_MAX && --i >= 0)
			preq = state->pendingRequestsPointer[NEXT_SEQ_NO(state->currentPendingRequest, MAX_DMA_GROUPID)];

		if (preq->requestId != UINT_MAX)
		{
			REPORT_ERROR("Ran out of requestId's, this is likely a bug in DSMCBE, dumping package codes for pending requests");
			for(i = 0; i < MAX_DMA_GROUPID; i++)
				fprintf(stderr, "Package %i has type %s (%i)\n", i, PACKAGE_NAME(state->pendingRequestsPointer[i]->operation), state->pendingRequestsPointer[i]->operation);

			exit(-1);
		}
	}

	preq->requestId = requestId;
	preq->operation = operation;
	preq->objId = objId;
	preq->dataObj = dataObj;
	preq->DMAcount = DMAcount;
	preq->isCSP = isCSP;
	preq->channelPointer = NULL;

	return preq;
}

void dsmcbe_spu_free_PendingRequest(struct dsmcbe_spu_pendingRequest* preq)
{
	preq->requestId = UINT_MAX;
	preq->operation = UINT_MAX;
	preq->objId = UINT_MAX;
	preq->dataObj = NULL;
	preq->DMAcount = UINT_MAX;
	preq->isCSP = UINT_MAX;
	preq->channelPointer = NULL;
}

struct dsmcbe_spu_pendingRequest* dsmcbe_spu_FindPendingRequest(struct dsmcbe_spu_state* state, unsigned int requestId)
{
	struct dsmcbe_spu_pendingRequest* preq = state->pendingRequestsPointer[state->currentPendingRequest];

	//For most cases we simply return here
	if (preq->requestId == requestId)
		return preq;

	size_t i;
	for(i = 0; i < MAX_DMA_GROUPID - 1; i++)
	{
		//Look backwards from current, more likely to hit
		preq = state->pendingRequestsPointer[(state->currentPendingRequest - i) % MAX_DMA_GROUPID];
		if (preq->requestId == requestId)
			return preq;
	}

	return NULL;
}

void dsmcbe_SetHardwareThreads(unsigned int count)
{
	if (dsmcbe_spu_mainthread != NULL) {
		REPORT_ERROR("Unable to set hardware thread count after initialize is called");
	} else {
		dsmcbe_spu_ppu_threadcount = count;
	}
}

void dsmcbe_spu_SendMessagesToSPU(struct dsmcbe_spu_state* state, unsigned int packageCode, unsigned int requestId, unsigned int data, unsigned int size)
{
	//TODO: Determine if it is possible to accidentially insert a barrier response because it fits in mbox, but still have messages in queue
	// this would lead to mixing of package data
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
			case PACKAGE_CSP_CHANNEL_CREATE_RESPONSE:
			case PACKAGE_CSP_CHANNEL_POISON_RESPONSE:
			case PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE:
			case PACKAGE_CSP_CHANNEL_SKIP_RESPONSE:
			case PACKAGE_CSP_CHANNEL_POISONED_RESPONSE:
			case PACKAGE_CSP_CHANNEL_WRITE_RESPONSE:
			case PACKAGE_NACK:
				if (spe_in_mbox_status(state->context) >= 2)
				{
					spe_in_mbox_write(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &packageCode, 1, SPE_MBOX_ALL_BLOCKING);
					return;
				}
			case PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE:
				if (spe_in_mbox_status(state->context) >= 3)
				{
					spe_in_mbox_write(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &packageCode, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &data, 1, SPE_MBOX_ALL_BLOCKING);
					return;
				}
			case PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE:
			case PACKAGE_CSP_CHANNEL_READ_RESPONSE:
			case PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE:
				if (spe_in_mbox_status(state->context) >= 4)
				{
					spe_in_mbox_write(state->context, &requestId, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &packageCode, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &data, 1, SPE_MBOX_ALL_BLOCKING);
					spe_in_mbox_write(state->context, &size, 1, SPE_MBOX_ALL_BLOCKING);
					return;
				}
			default:
				REPORT_ERROR2("Unexpected response code %d", packageCode);
				break;
		}
	}

#ifdef USE_EVENTS
	printf(WHERESTR "Mbox is full, forwarding to writer: %d\n", WHEREARG, packageCode);

	//Forward to writer
	struct internalMboxArgs* args = dsmcbe_spu_new_internalMboxArgs(0, packageCode, requestId, size, data);

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
		case PACKAGE_CSP_CHANNEL_CREATE_RESPONSE:
		case PACKAGE_CSP_CHANNEL_POISON_RESPONSE:
		case PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE:
		case PACKAGE_CSP_CHANNEL_SKIP_RESPONSE:
		case PACKAGE_CSP_CHANNEL_POISONED_RESPONSE:
		case PACKAGE_CSP_CHANNEL_WRITE_RESPONSE:
		case PACKAGE_NACK:
			g_queue_push_tail(state->mailboxQueue, (void*)requestId);
			g_queue_push_tail(state->mailboxQueue, (void*)packageCode);
			return;
		case PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE:
			g_queue_push_tail(state->mailboxQueue, (void*)requestId);
			g_queue_push_tail(state->mailboxQueue, (void*)packageCode);
			g_queue_push_tail(state->mailboxQueue, (void*)data);
			return;
		case PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE:
		case PACKAGE_CSP_CHANNEL_READ_RESPONSE:
		case PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE:
			g_queue_push_tail(state->mailboxQueue, (void*)requestId);
			g_queue_push_tail(state->mailboxQueue, (void*)packageCode);
			g_queue_push_tail(state->mailboxQueue, (void*)data);
			g_queue_push_tail(state->mailboxQueue, (void*)size);
			return;
		default:
			REPORT_ERROR2("Unexpected response code %d", packageCode);
			break;
	}
#endif
}

//This function releases all resources reserved for the object, and sends out invalidate responses, if required
void dsmcbe_spu_DisposeObject(struct dsmcbe_spu_state* state, struct dsmcbe_spu_dataObject* obj)
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
		dsmcbe_rc_EnqueInvalidateResponse(obj->id, obj->invalidateId);
		obj->invalidateId = UINT_MAX;
	}

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Disposing item %d\n", WHEREARG, obj->id);
#endif

	g_hash_table_remove(state->itemsById, (void*)obj->id);
	g_hash_table_remove(state->itemsByPointer, (void*)obj->LS);
	g_queue_remove(state->agedObjectMap, (void*)obj->id);
	dsmcbe_spu_memory_free(state->map, obj->LS);
	
	if (state->terminated != UINT_MAX && g_hash_table_size(state->itemsById) == 0)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Signaling termination to SPU %d\n", WHEREARG, obj->id);
#endif
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_TERMINATE_RESPONSE, state->terminated, PACKAGE_TERMINATE_RESPONSE, 0);
	}

	FREE(obj);
	obj = NULL;
}

//This function creates and forwards a message to the request coordinator
void dsmcbe_spu_SendRequestCoordinatorMessage(struct dsmcbe_spu_state* state, void* req)
{
#ifdef USE_EVENTS
	QueueableItem qi = dsmcbe_rc_new_QueueableItem(&state->inMutex, &state->inCond, &state->inQueue, req, NULL);
#else
	QueueableItem qi = dsmcbe_rc_new_QueueableItem(&state->inMutex, NULL, &state->inQueue, req, NULL);
#endif
	
	dsmcbe_rc_EnqueItem(qi);
}


//This function removes all objects from the state and shuts down the SPU
void dsmcbe_spu_DisposeAllObject(struct dsmcbe_spu_state* state)
{
	if (state->terminated != UINT_MAX && g_queue_is_empty(state->agedObjectMap) && g_hash_table_size(state->itemsById) == 0)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Signaling termination to SPU %d\n", WHEREARG, state->terminated);
#endif
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_TERMINATE_RESPONSE, state->terminated, 0, 0);

		state->shutdown = TRUE;
		return;
	}
	
	while(!g_queue_is_empty(state->agedObjectMap))
	{
		GUID id = (GUID)g_queue_pop_head(state->agedObjectMap);
		
		struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
		if (obj == NULL) {
			REPORT_ERROR("An item was in the age map, but did not exist!");
		} else
			dsmcbe_spu_DisposeObject(state, obj);
	}
	
	if (g_hash_table_size(state->itemsById) != 0)
	{
		//This can happen, because the release call is async
		//REPORT_ERROR("DisposeAll was called but some objects were not released!");
		
		/*GHashTableIter it;

		void* key;
		struct dsmcbe_spu_dataObject* obj;
		
		g_hash_table_iter_init(&it, state->itemsById);
		while(g_hash_table_iter_next(&it, &key, (void*)&obj))
			printf(WHERESTR "Found item: %d with count: %d", WHEREARG, obj->id, obj->count); 
	
		while (g_hash_table_size(state->itemsById) != 0)
		{
			g_hash_table_iter_init(&it, state->itemsById);
			if (g_hash_table_iter_next(&it, &key, (void*)&obj))
			{
				obj->count = 0;
				spuhandler_DisposeObject(state, (struct dsmcbe_spu_dataObject*)obj);
			}
			
			if (g_hash_table_remove(state->itemsById, key))
				REPORT_ERROR("An object in the table was attempted disposed, but failed");
		}*/
	}
}

//This function allocates space for an object.
//If there is not enough space, unused objects are removed until there is enough space.
void* dsmcbe_spu_AllocateSpaceForObject(struct dsmcbe_spu_state* state, unsigned long size)
{
	void* temp = NULL;
	size_t i = 0;

	struct dsmcbe_spu_dataObject* obj;
	unsigned int id;	
	size = ALIGNED_SIZE(size);
	
	//If the requested size is larger than the total available space, don't discard objects
	if (size > state->map->totalmem)
		return NULL;
	
	//If there is no way the object can fit, skip the initial try
	if (state->map->free_mem >= size)
		temp = dsmcbe_spu_memory_malloc(state->map, size);

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
					dsmcbe_spu_DisposeObject(state, obj);
					temp = dsmcbe_spu_memory_malloc(state->map, size);
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
				dsmcbe_spu_DisposeObject(state, obj);
				if (state->map->free_mem >= size)
					temp = dsmcbe_spu_memory_malloc(state->map, size);
			}
		}
	}

#ifdef DEBUG_FRAGMENTATION	
	if (temp == NULL)
	{
		GHashTableIter iter;
		g_hash_table_iter_init(&iter, state->itemsById);
		GUID key;
		struct dsmcbe_spu_dataObject* value;
		
		while(g_hash_table_iter_next(&iter, (void*)&key, (void*)&value))
		{
			//printf(WHERESTR "Found item with id: %d and count %d\n", WHEREARG, key, value->count);
			if (value->count == 0)
				g_queue_push_tail(state->agedObjectMap, (void*)key);
		}
				
		if (!g_queue_is_empty(state->agedObjectMap))
		{
			REPORT_ERROR("Extra unused objects were found outside the agedObjectMap");			
			return dsmcbe_spu_AllocateSpaceForObject(state, size);
		}
		else
			return NULL;
	}
#endif
	
	return temp;
}
 
//This function handles incoming acquire requests from an SPU
void dsmcbe_spu_HandleAcquireRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, unsigned int packageCode)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

	struct dsmcbe_spu_dataObject* obj = NULL;

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
			dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, requestId, (unsigned int)obj->LS, obj->size);

			if (obj->count == 1)
				g_queue_remove(state->agedObjectMap, (void*)obj->id);
			return;
		}
	}
	
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_new_PendingRequest(state, requestId, packageCode, id, obj, 0, FALSE);

	if (obj != NULL)
		obj->preq = preq;

	struct dsmcbe_acquireRequest* req = dsmcbe_new_acquireRequest(id, requestId, packageCode == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE);
	
	req->packageCode = packageCode;

	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}

//This function initiates a DMA transfer to or from the SPU
void dsmcbe_spu_InitiateDMATransfer(struct dsmcbe_spu_state* state, unsigned int toSPU, unsigned int EA, unsigned int LS, unsigned int size, unsigned int groupId)
{
	if (size > SPE_DMA_MIN_TRANSFERSIZE)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Initiating DMA transfer on PPU (%s), EA: %d, LS: %d, size: %d, tag: %d\n", WHEREARG, toSPU ? "EA->LS" : "LS->EA", EA, LS, size, groupId);
#endif

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
		
		dsmcbe_spu_HandleDMATransferCompleted(state, groupId);
			
	}
}


//This function handles incoming create requests from an SPU
void dsmcbe_spu_HandleCreateRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, unsigned long size)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling create request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

	if (g_hash_table_lookup(state->itemsById, (void*)id) != NULL)
	{
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, requestId, 0, 0);
		return;
	}
	
	//Register the pending request
	dsmcbe_spu_new_PendingRequest(state, requestId, PACKAGE_CREATE_REQUEST, id, NULL, 0, FALSE);

	struct dsmcbe_createRequest* req = dsmcbe_new_createRequest(id, requestId, size);

	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}

//This function handles incoming barrier requests from an SPU
void dsmcbe_spu_HandleBarrierRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling barrier request for %d, with requestId: %d\n", WHEREARG, id, requestId);
#endif

	//Register the pending request
	dsmcbe_spu_new_PendingRequest(state, requestId, PACKAGE_ACQUIRE_BARRIER_REQUEST, id, NULL, 0, FALSE);

	struct dsmcbe_acquireBarrierRequest* req = dsmcbe_new_acquireBarrierRequest(id, requestId);

	dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
}

//This function transfers an entire object to or from the SPU
void dsmcbe_spu_TransferObject(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq, struct dsmcbe_spu_dataObject* obj)
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
		while(TRUE) //TODO: Could create inf. loop if there are no empty slots
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
		dsmcbe_spu_InitiateDMATransfer(state, obj->isDMAToSPU, (unsigned int)obj->EA + sizeDone, (unsigned int)obj->LS + sizeDone, MIN(sizeRemain, SPE_DMA_MAX_TRANSFERSIZE), obj->DMAId);
		sizeRemain -= SPE_DMA_MAX_TRANSFERSIZE; 
		sizeDone += SPE_DMA_MAX_TRANSFERSIZE;
	}
}

//This function handles an incoming release request from an SPU
void dsmcbe_spu_HandleReleaseRequest(struct dsmcbe_spu_state* state, void* data)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Releasing object @: %d\n", WHEREARG, (unsigned int)data);
#endif

	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->itemsByPointer, data);

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
		dsmcbe_spu_HandleObjectRelease(state, obj);
	}
	else /*if (obj->mode == ACQUIRE_MODE_WRITE)*/
	{
		//Get a group id, and register the active transfer
		struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_new_PendingRequest(state, NEXT_SEQ_NO(state->releaseSeqNo, MAX_PENDING_RELEASE_REQUESTS) + RELEASE_NUMBER_BASE, PACKAGE_RELEASE_REQUEST, obj->id, NULL, (MAX(ALIGNED_SIZE(obj->size), SPE_DMA_MAX_TRANSFERSIZE) + (SPE_DMA_MAX_TRANSFERSIZE - 1)) / SPE_DMA_MAX_TRANSFERSIZE, FALSE);
		
		obj->isDMAToSPU = FALSE;

		//Initiate the DMA transfer if the buffer is ready
		if (obj->writebuffer_ready)
		{
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "WriteBuffer was ready, performing transfer without delay: %d\n", WHEREARG, obj->id);
#endif
			dsmcbe_spu_TransferObject(state, preq, obj);
		}
		else
		{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "WriteBuffer was NOT ready, registering a dummy object: %d\n", WHEREARG, obj->id);
#endif
			//Otherwise just register it as active to avoid disposing
			while(TRUE)
			{
				obj->DMAId = NEXT_SEQ_NO(DMA_SEQ_NO, MAX_DMA_GROUPID);
				if (state->activeDMATransfers[obj->DMAId] != NULL)
					continue;

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
void dsmcbe_spu_HandleWriteBufferReady(struct dsmcbe_spu_state* state, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling WriteBufferReady for itemId: %d\n", WHEREARG, id);
#endif
	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);
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
		struct dsmcbe_spu_pendingRequest* preq = state->activeDMATransfers[obj->DMAId];
		if (preq == NULL) {
			REPORT_ERROR2("Delayed DMA write for %d was missing control object!", obj->DMAId);
			exit(-5);
		}
		else
		{
			state->activeDMATransfers[obj->DMAId] = NULL;
		}
		
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "WriteBufferReady triggered a DMA transfer: %d\n", WHEREARG, id);
#endif
		
		dsmcbe_spu_TransferObject(state, preq, obj);
	}
}

//This function calculates the amount of space that is in the process of being released,
// and thus will be available for use eventually
unsigned int dsmcbe_spu_EstimatePendingReleaseSize(struct dsmcbe_spu_state* state)
{
	void* key;
	void* value;
	unsigned int pendingSize = 0;
	struct dsmcbe_spu_dataObject* obj;
	size_t i;

	for (i = 0; i < MAX_DMA_GROUPID; i++)
	{
		struct dsmcbe_spu_pendingRequest* v = state->pendingRequestsPointer[i];
		if (v->requestId != UINT_MAX && v->operation == PACKAGE_RELEASE_REQUEST)
		{
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
		struct dsmcbe_spu_pendingRequest* v = (struct dsmcbe_spu_pendingRequest*)value;
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
int dsmcbe_spu_HandleAcquireResponse(struct dsmcbe_spu_state* state, struct dsmcbe_acquireResponse* data)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling acquire response for id: %d, requestId: %d\n", WHEREARG, data->dataItem, data->requestID);
#endif
	
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, data->requestID);
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
	struct dsmcbe_spu_dataObject* obj = preq->dataObj;

	if (obj == NULL)
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Allocating space on SPU\n", WHEREARG);
#endif
		void* ls = dsmcbe_spu_AllocateSpaceForObject(state, data->dataSize);

		if (ls == NULL)
		{
			//We have no space on the SPU, so wait until we get some
			if (dsmcbe_spu_EstimatePendingReleaseSize(state) == 0)
			{
				REPORT_ERROR2("No more space, denying acquire request for %d", data->dataItem);

				dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, preq->requestId, 0, 0);

				dsmcbe_spu_free_PendingRequest(preq);

				return TRUE;
				
			}
		} 
			

#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling acquire response for id: %d, requestId: %d, object did not exist, creating\n", WHEREARG, preq->objId, data->requestID);
#endif

		obj = dsmcbe_spu_new_dataObject(
				preq->objId, //id
				FALSE, //isCSP
				preq->operation == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE, //mode
				1, //count
				-1, //dmaid
				data->data, //EA
				ls, //LS
				UINT_MAX, //invalidateId
				data->dataSize, //size
				(preq->operation == PACKAGE_CREATE_REQUEST || data->writeBufferReady), //writebuffer_ready
				TRUE, //isDMAToSPU
				NULL //preq
		);

		g_hash_table_insert(state->itemsById, (void*)obj->id, obj);

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
			dsmcbe_spu_TransferObject(state, preq, obj);
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
		
		dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, preq->requestId, (unsigned int)obj->LS, obj->size);

		//if (preq->operation != PACKAGE_ACQUIRE_REQUEST_WRITE)
		{
			dsmcbe_spu_free_PendingRequest(preq);
			obj->preq = NULL;
			preq = NULL;
		}	
	}
	
	if (obj->count == 1)
		g_queue_remove(state->agedObjectMap, (void*)obj->id);

	return TRUE;

}

//This function deals with acquire requests that are waiting for a release response
void dsmcbe_spu_ManageDelayedAcquireResponses(struct dsmcbe_spu_state* state)
{
	while (!g_queue_is_empty(state->releaseWaiters))
	{
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Attempting to process a new delayed acquire\n", WHEREARG);	
#endif
		struct dsmcbe_spu_pendingRequest* preq = g_queue_peek_head(state->releaseWaiters);
		
		if (preq == NULL)
			REPORT_ERROR("Pending request was missing?");
	
		struct dsmcbe_spu_dataObject* obj;

		if (preq->isCSP)
			obj = preq->dataObj;
		else
			obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);

		if (obj == NULL)
			REPORT_ERROR("Object was missing in delayed acquire");		
		
		if (obj->LS != NULL)
			REPORT_ERROR("Waiter was allocated?");

		obj->LS = dsmcbe_spu_AllocateSpaceForObject(state, obj->size);

		if (obj->LS == NULL)
		{
			if (dsmcbe_spu_EstimatePendingReleaseSize(state) == 0)
			{
				REPORT_ERROR("Out of memory on SPU");
				exit(-3);
			}
			return;
		}
		else
		{
			g_queue_pop_head(state->releaseWaiters);
			if (preq->isCSP)
				g_hash_table_insert(state->csp_items, obj->LS, obj);
			else
				g_hash_table_insert(state->itemsByPointer, obj->LS, obj);

			dsmcbe_spu_TransferObject(state, preq, obj);
		}
	}
}

//This function deals with cleaning up and possibly freeing objects
void dsmcbe_spu_HandleObjectRelease(struct dsmcbe_spu_state* state, struct dsmcbe_spu_dataObject* obj)
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
				
				dsmcbe_spu_DisposeObject(state, obj);
				dsmcbe_spu_ManageDelayedAcquireResponses(state);
			}
			else
				g_queue_push_tail(state->agedObjectMap, (void*)obj->id);
		}
}

//This function handles completed DMA transfers
void dsmcbe_spu_HandleDMATransferCompleted(struct dsmcbe_spu_state* state, unsigned int groupID)
{
	//Get the corresponding request
	struct dsmcbe_spu_pendingRequest* preq = state->activeDMATransfers[groupID];

	if (preq == NULL)
	{
		REPORT_ERROR("DMA completed, but was not initiated");
		return;
	}

	state->activeDMATransfers[groupID] = NULL;

	//Get the corresponding data object
	struct dsmcbe_spu_dataObject* obj = preq->dataObj;
	if (obj == NULL)
	{
		dsmcbe_spu_free_PendingRequest(preq);
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
	if (preq->operation != PACKAGE_RELEASE_REQUEST && preq->operation != PACKAGE_CSP_CHANNEL_WRITE_REQUEST && preq->operation != PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST)
	{
		if (preq->DMAcount != 0)
			return;
			
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, id: %d, notifying SPU\n", WHEREARG, groupID, preq->objId);
#endif
		
		if (preq->isCSP)
		{
			if (preq->operation == PACKAGE_CSP_CHANNEL_READ_REQUEST)
			{
				dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_CSP_CHANNEL_READ_RESPONSE, preq->requestId, (unsigned int)obj->LS, obj->size);
			}
			else if (preq->operation == PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST)
			{
				if (preq->channelPointer != NULL)
					*((unsigned int*)preq->channelPointer) = preq->objId;
				dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE, preq->requestId, (unsigned int)obj->LS, obj->size);
			}
			else
			{
				REPORT_ERROR2("Unexpected csp package code %d", preq->operation);
			}
		}
		else
		{
			dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_ACQUIRE_RESPONSE, preq->requestId, (unsigned int)obj->LS, obj->size);
		}

		obj->preq = NULL;
		dsmcbe_spu_free_PendingRequest(preq);
		preq = NULL;

	}
	else if(preq->DMAcount == 0)
	{
		//Data is transfered from SPU LS to EA, now notify the request coordinator
#ifdef DEBUG_COMMUNICATION	
		printf(WHERESTR "Handling completed DMA transfer, dmaId: %d, id: %d, notifying RC\n", WHEREARG, groupID, preq->objId);
#endif
		//__asm__ __volatile__("lwsync" : : : "memory");
		
		if (preq->isCSP)
		{
			if (preq->operation == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST)
			{
				//We already have the required struct created, so just forward it
				dsmcbe_spu_SendRequestCoordinatorMessage(state, preq->channelPointer);
				preq->channelPointer = NULL;
			}
			else if (preq->operation == PACKAGE_CSP_CHANNEL_WRITE_REQUEST)
			{
				//Create the request and forward it
				struct dsmcbe_cspChannelWriteRequest* req;
				if (dsmcbe_new_cspChannelWriteRequest_single(&req, preq->objId, preq->requestId, obj->EA, obj->size, TRUE) != CSP_CALL_SUCCESS)
					exit(-1);

				dsmcbe_spu_SendRequestCoordinatorMessage(state, req);
			}
			else
			{
				REPORT_ERROR2("Unexpected csp package code %d", preq->operation);
			}
		}
		else
		{
			struct dsmcbe_releaseRequest* req = dsmcbe_new_releaseRequest(obj->id, preq->requestId, ACQUIRE_MODE_WRITE, obj->size, 0, obj->EA);

			dsmcbe_spu_SendRequestCoordinatorMessage(state, req);

			obj->preq = NULL;
			dsmcbe_spu_free_PendingRequest(preq);
			if (obj == NULL || obj->count == 0)
			{
				REPORT_ERROR("Release was completed, but the object was not acquired?");
				return;
			}
			dsmcbe_spu_HandleObjectRelease(state, obj);
		}
	}
}

//This function handles incoming barrier responses from the request coordinator
void dsmcbe_spu_HandleBarrierResponse(struct dsmcbe_spu_state* state, unsigned int requestId)
{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Handling barrier response for requestId: %d\n", WHEREARG, requestId);
#endif	
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);
	if (preq == NULL)
	{
		REPORT_ERROR("Get release response for non initiated request");
		return;
	}

	dsmcbe_spu_free_PendingRequest(preq);
	preq = NULL;

	dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_ACQUIRE_BARRIER_RESPONSE, requestId, 0, 0);
}


//This function handles incoming invalidate requests from the request coordinator
void dsmcbe_spu_HandleInvalidateRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id)
{
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Handling invalidate request for id: %d, request id: %d\n", WHEREARG, id, requestId);
#endif

	struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)id);

	if (obj == NULL) 
	{
		dsmcbe_rc_EnqueInvalidateResponse(id, requestId);
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
				dsmcbe_spu_DisposeObject(state, obj);
		}
		else
			REPORT_ERROR2("The Invalidate was for %d in NON read mode or obj->count was NOT 0", obj->id);
	}
}

void dsmcbe_spu_PrintDebugStatus(struct dsmcbe_spu_state* state, unsigned int requestId)
{
	struct dsmcbe_spu_pendingRequest* preq = dsmcbe_spu_FindPendingRequest(state, requestId);
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
		
		struct dsmcbe_spu_dataObject* obj = g_hash_table_lookup(state->itemsById, (void*)preq->objId);
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

void dsmcbe_spu_ForwardInternalMbox(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
{
	pthread_mutex_lock(&state->inMutex);
	g_queue_push_tail(state->inQueue, args);

#ifdef USE_EVENTS
	pthread_cond_signal(&state->inCond);
#endif

	pthread_mutex_unlock(&state->inMutex);
}

//Reads an processes any incoming mailbox messages
void dsmcbe_spu_MailboxHandler(struct dsmcbe_spu_state* state, struct dsmcbe_spu_internalMboxArgs* args)
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
				dsmcbe_spu_DisposeAllObject(state);
				break;
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				dsmcbe_spu_HandleAcquireRequest(state, args->requestId, args->id, args->packageCode);
				break;
			case PACKAGE_CREATE_REQUEST:
				dsmcbe_spu_HandleCreateRequest(state, args->requestId, args->id, args->size);
				break;
			case PACKAGE_RELEASE_REQUEST:
				dsmcbe_spu_HandleReleaseRequest(state, (void*)args->requestId);
				break;
			case PACKAGE_SPU_MEMORY_FREE:
				dsmcbe_spu_memory_free(state->map, (void*)args->requestId);
				break;
			case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
				dsmcbe_spu_SendMessagesToSPU(state, PACKAGE_SPU_MEMORY_MALLOC_RESPONSE, args->requestId, (unsigned int)dsmcbe_spu_AllocateSpaceForObject(state, args->size), 0);
				break;
			case PACKAGE_ACQUIRE_BARRIER_REQUEST:
				dsmcbe_spu_HandleBarrierRequest(state, args->requestId, args->id);
				break;
			case PACKAGE_DEBUG_PRINT_STATUS:
				dsmcbe_spu_PrintDebugStatus(state, args->requestId);
				break;

			case PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST:
				dsmcbe_spu_csp_HandleItemCreateRequest(state, args->requestId, args->size);
				break;
			case PACKAGE_SPU_CSP_ITEM_FREE_REQUEST:
				dsmcbe_spu_csp_HandleItemFreeRequest(state, args->requestId, (void*)args->data);
				break;
			case PACKAGE_CSP_CHANNEL_WRITE_REQUEST:
				dsmcbe_spu_csp_HandleChannelWriteRequest(state, args->requestId, args->id, (void*)args->data);
				break;
			case PACKAGE_CSP_CHANNEL_READ_REQUEST:
				dsmcbe_spu_csp_HandleChannelReadRequest(state, args->requestId, args->id);
				break;
			case PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST:
				dsmcbe_spu_csp_HandleChannelReadRequestAlt(state, args->requestId, (void*)args->channels, args->size, (void*)args->channelId, args->mode);
				break;
			case PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST:
				dsmcbe_spu_csp_HandleChannelWriteRequestAlt(state, args->requestId, (void*)args->data, (void*)args->channels, args->size, args->mode);
				break;
			case PACKAGE_CSP_CHANNEL_POISON_REQUEST:
				dsmcbe_spu_csp_HandleChannelPoisonRequest(state, args->requestId, args->id);
				break;
			case PACKAGE_CSP_CHANNEL_CREATE_REQUEST:
				dsmcbe_spu_csp_HandleChannelCreateRequest(state, args->requestId, args->id, args->size, args->mode);
				break;

			default:
				REPORT_ERROR2("Unknown package code received: %d", args->packageCode);
				break;
		}

		FREE(args);
		args = NULL;
	}
}

void dsmcbe_spu_SPUMailboxReader(struct dsmcbe_spu_state* state)
{
#ifndef USE_EVENTS
	if (spe_out_intr_mbox_status(state->context) <= 0)
		return;
#endif

	//TODO: It is not smart to malloc this all the time
	struct dsmcbe_spu_internalMboxArgs* args = MALLOC(sizeof(struct dsmcbe_spu_internalMboxArgs));
	spe_out_intr_mbox_read(state->context, &args->packageCode, 1, SPE_MBOX_ALL_BLOCKING);

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
				state->map = dsmcbe_spu_memory_create((unsigned int)args->requestId, args->size);
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

		case PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_SPU_CSP_ITEM_FREE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->data, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_CSP_CHANNEL_WRITE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->data, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_CSP_CHANNEL_READ_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST: //TODO: Maybe its faster to just memcpy these?
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->mode, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->channels, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->channelId, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST: //TODO: Maybe its faster to just memcpy these?
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->mode, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->channels, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->data, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_CSP_CHANNEL_POISON_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			break;
		case PACKAGE_CSP_CHANNEL_CREATE_REQUEST:
			spe_out_intr_mbox_read(state->context, &args->requestId, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->id, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->size, 1, SPE_MBOX_ALL_BLOCKING);
			spe_out_intr_mbox_read(state->context, &args->mode, 1, SPE_MBOX_ALL_BLOCKING);
			break;

		default:
			REPORT_ERROR2("Unknown package code recieved: %d", args->packageCode);
			break;
	}

	if (args != NULL)
	{
#ifdef USE_EVENTS
		dsmcbe_spu_ForwardInternalMbox(state, args);
#else
		dsmcbe_spu_MailboxHandler(state, args);
#endif
	}
}

//This function writes pending data to the spu mailbox while there is room
int dsmcbe_spu_SPUMailboxWriter(struct dsmcbe_spu_state* state)
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
void* dsmcbe_spu_thread_SPUMailboxWriter(void* outdata)
{
	struct dsmcbe_spu_state* state = (struct dsmcbe_spu_state*)outdata;
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
void dsmcbe_spu_HandleDMAEvent(struct dsmcbe_spu_state* state)
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
			dsmcbe_spu_HandleDMATransferCompleted(state, ((unsigned int)i));
}


//This function handles completion of a DMA event
void* dsmcbe_spu_thread_HandleEvents()
{
	unsigned int mask = 0;
	size_t i = 0;

	spe_event_unit_t* eventArgsReg = MALLOC(sizeof(spe_event_unit_t) * dsmcbe_spu_thread_count);
	spe_event_unit_t* eventPend = MALLOC(sizeof(spe_event_unit_t) * dsmcbe_spu_thread_count);
	unsigned int* SPU_IsReg = MALLOC(sizeof(unsigned int) * dsmcbe_spu_thread_count);

	spe_event_handler_ptr_t eventhandler = spe_event_handler_create();

	//printf(WHERESTR "SPU count is %d\n", WHEREARG, dsmcbe_spu_thread_count);

	for(i = 0; i < dsmcbe_spu_thread_count; i++)
	{
		eventArgsReg[i].events = (SPE_EVENT_ALL_EVENTS) & (~SPE_EVENT_IN_MBOX);
		eventArgsReg[i].spe = dsmcbe_spu_states[i].context;
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

			for(i = 0; i < dsmcbe_spu_thread_count; i++)
				if (!dsmcbe_spu_states[i].shutdown)
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

		if ((eventCount = spe_event_wait(eventhandler, eventPend, dsmcbe_spu_thread_count, 5000)) < 1)
		{
			REPORT_ERROR("Event Wait Failed");
			//eventArgsIn.events = SPE_EVENT_SPE_STOPPED;
		}

#ifdef DEBUG_EVENT
		printf(WHERESTR "Recieved event\n", WHEREARG);
#endif

		for(j = 0; j < eventCount; j++)
		{
			struct dsmcbe_spu_state* state = &dsmcbe_spu_states[eventPend[j].data.u32];

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

				struct dsmcbe_spu_internalMboxArgs* args = (struct dsmcbe_spu_internalMboxArgs*)MALLOC(sizeof(struct dsmcbe_spu_internalMboxArgs));

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

				dsmcbe_spu_SPUMailboxReader(state);
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
void dsmcbe_spu_HandleMessagesFromQueue(struct dsmcbe_spu_state* state, void* package)
{
	size_t i;

#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Handling response message: %s (%d)\n", WHEREARG, PACKAGE_NAME(((struct dsmcbe_acquireResponse*)package)->packageCode), ((struct dsmcbe_acquireResponse*)package)->packageCode);
#endif
	switch(((struct dsmcbe_createRequest*)package)->packageCode)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			if (dsmcbe_spu_HandleAcquireResponse(state, (struct dsmcbe_acquireResponse*)package) == FALSE)
				package = NULL;
			break;
		case PACKAGE_INVALIDATE_REQUEST:
			dsmcbe_spu_HandleInvalidateRequest(state, ((struct dsmcbe_invalidateRequest*)package)->requestID, ((struct dsmcbe_invalidateRequest*)package)->dataItem);
			break;
		case PACKAGE_WRITEBUFFER_READY:
			dsmcbe_spu_HandleWriteBufferReady(state, ((struct dsmcbe_writebufferReady*)package)->dataItem);
			break;
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			dsmcbe_spu_HandleBarrierResponse(state, ((struct dsmcbe_acquireBarrierResponse*)package)->requestID);
			break;
		case PACKAGE_DMA_COMPLETE:
			//See if the any of the completed transfers are in our wait list
			for(i = 0; i < MAX_DMA_GROUPID; i++)
				if (state->activeDMATransfers[i] != NULL && ((((struct dsmcbe_spu_internalMboxArgs*)package)->requestId) & (1 << ((unsigned int)i))) != 0)
					dsmcbe_spu_HandleDMATransferCompleted(state, ((unsigned int)i));
			break;

		case PACKAGE_CSP_CHANNEL_CREATE_RESPONSE:
			dsmcbe_spu_csp_HandleChannelCreateResponse(state, ((struct dsmcbe_cspChannelCreateResponse*)package)->packageCode, ((struct dsmcbe_cspChannelCreateResponse*)package)->requestID);
			break;
		case PACKAGE_CSP_CHANNEL_POISON_RESPONSE:
			dsmcbe_spu_csp_HandleChannelPoisonResponse(state, ((struct dsmcbe_cspChannelCreateResponse*)package)->packageCode, ((struct dsmcbe_cspChannelCreateResponse*)package)->requestID);
			break;
		case PACKAGE_CSP_CHANNEL_READ_RESPONSE:
			dsmcbe_spu_csp_HandleChannelReadResponse(state, package);
			break;
		case PACKAGE_CSP_CHANNEL_WRITE_RESPONSE:
			dsmcbe_spu_csp_HandleChannelWriteResponse(state, package);
			break;
		case PACKAGE_CSP_CHANNEL_POISONED_RESPONSE:
		case PACKAGE_CSP_CHANNEL_SKIP_RESPONSE:
		case PACKAGE_NACK:
			dsmcbe_spu_csp_HandleChannelPoisonNACKorSkipResponse(state, package);
			break;
		default:

			//KS: WTF? The structs are not even comparable....
			//dsmcbe_spu_MailboxHandler(state, (struct dsmcbe_spu_internalMboxArgs*)package);
			//package = NULL; //Handled in the mbox reader

			fprintf(stderr, WHERESTR "Error: Unsupported response message: %s (%d)\n", WHEREARG, PACKAGE_NAME(((struct dsmcbe_acquireResponse*)package)->packageCode), ((struct dsmcbe_acquireResponse*)package)->packageCode);
			break;
	}

	if (package != NULL)
		FREE(package);

	package = NULL;
}

//This function repeatedly checks for events relating to the SPU's
void* dsmcbe_spu_mainthreadSpinning(void* threadranges)
{
	size_t i;
	unsigned int pending_out_data;
	unsigned int spu_thread_min = ((unsigned int*)threadranges)[0];
	unsigned int spu_thread_max = ((unsigned int*)threadranges)[1];
	struct dsmcbe_spu_state* state = NULL;
	struct acquireResponse* resp = NULL;
	FREE(threadranges);

	printf(WHERESTR "This run is using SPINNING\n", WHEREARG);

	//Event base, keeps the mutex locked, until we wait for events
	while(!dsmcbe_spu_do_terminate)
	{

		pending_out_data = 0;

		for(i = spu_thread_min; i < spu_thread_max; i++)
		{
			resp = NULL;
			state = &dsmcbe_spu_states[i];

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

				dsmcbe_spu_HandleMessagesFromQueue(state, resp);
			}
		}

		for(i = spu_thread_min; i < spu_thread_max; i++)
		{
			state = &dsmcbe_spu_states[i];

			//For each SPU, just repeat this
			dsmcbe_spu_SPUMailboxReader(state);
			dsmcbe_spu_HandleDMAEvent(&dsmcbe_spu_states[i]);
			pending_out_data |= dsmcbe_spu_SPUMailboxWriter(&dsmcbe_spu_states[i]);
		}
	}

	printf(WHERESTR "Main thread stopping\n", WHEREARG);

	return NULL;
}

//This function repeatedly checks for events relating to the SPU's
void* dsmcbe_spu_mainthreadEvents(void* input)
{
	struct dsmcbe_spu_state* state = (struct dsmcbe_spu_state*)input;
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

		dsmcbe_spu_HandleMessagesFromQueue(state, req);
	}

	printf(WHERESTR "WARNING: HandleRequest messages stopping\n", WHEREARG);

	return NULL;
}

//This function sets up the SPU event handler
void dsmcbe_spu_initialize(spe_context_ptr_t* threads, unsigned int thread_count)
{
	pthread_attr_t attr;
	size_t i,j;
	
	dsmcbe_spu_thread_count = thread_count;
	dsmcbe_spu_do_terminate = TRUE;
	
	if (thread_count == 0)
		return;

	dsmcbe_spu_do_terminate = FALSE;


	dsmcbe_spu_states = MALLOC(sizeof(struct dsmcbe_spu_state) * thread_count);
	//if (pthread_mutex_init(&spu_rq_mutex, NULL) != 0) REPORT_ERROR("Mutex initialization failed");

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	//DMA_SEQ_NO = 0;
	
	for(i = 0; i < thread_count; i++)
	{
		dsmcbe_spu_states[i].shutdown = FALSE;
		dsmcbe_spu_states[i].agedObjectMap = g_queue_new();
		dsmcbe_spu_states[i].context = threads[i];
		dsmcbe_spu_states[i].dmaSeqNo = 0;
		dsmcbe_spu_states[i].itemsById = g_hash_table_new(NULL, NULL);
		dsmcbe_spu_states[i].itemsByPointer = g_hash_table_new(NULL, NULL);
		dsmcbe_spu_states[i].mailboxQueue = g_queue_new();
		dsmcbe_spu_states[i].map = NULL;
		dsmcbe_spu_states[i].releaseWaiters = g_queue_new();
		dsmcbe_spu_states[i].inQueue = g_queue_new();
		dsmcbe_spu_states[i].terminated = UINT_MAX;
		dsmcbe_spu_states[i].releaseSeqNo = 0;
		dsmcbe_spu_states[i].streamItems = g_queue_new();
		dsmcbe_spu_states[i].csp_items = g_hash_table_new(NULL, NULL);

		pthread_mutex_init(&dsmcbe_spu_states[i].inMutex, NULL);

#ifdef USE_EVENTS
		pthread_cond_init(&dsmcbe_spu_states[i].inCond, NULL);
		pthread_mutex_init(&dsmcbe_spu_states[i].writerMutex, NULL);
		pthread_cond_init(&dsmcbe_spu_states[i].writerCond, NULL);
#endif

		dsmcbe_spu_states[i].writerDirtyReadFlag = 0;
		dsmcbe_spu_states[i].writerQueue = g_queue_new();

#ifdef USE_EVENTS
		dsmcbe_rc_RegisterInvalidateSubscriber(&dsmcbe_spu_states[i].inMutex, &dsmcbe_spu_states[i].inCond, &dsmcbe_spu_states[i].inQueue, -1);
#else
		dsmcbe_rc_RegisterInvalidateSubscriber(&dsmcbe_spu_states[i].inMutex, NULL, &dsmcbe_spu_states[i].inQueue, -1);
#endif

		for(j = 0; j < MAX_DMA_GROUPID; j++)
		{
			dsmcbe_spu_states[i].pendingRequestsPointer[j] = MALLOC(sizeof(struct dsmcbe_spu_pendingRequest));
			dsmcbe_spu_free_PendingRequest(dsmcbe_spu_states[i].pendingRequestsPointer[j]);
			dsmcbe_spu_states[i].activeDMATransfers[j] = NULL;
		}

		dsmcbe_spu_states[i].currentPendingRequest = 0;
	}

#ifdef USE_EVENTS
	//Start SPU mailbox readers
	for(i = 0; i < thread_count; i++)
	{
		if (pthread_create(&dsmcbe_spu_states[i].main, &attr, dsmcbe_spu_mainthreadEvents, &dsmcbe_spu_states[i]) != 0)
			REPORT_ERROR("Failed to start an SPU service thread")

		if (pthread_create(&dsmcbe_spu_states[i].inboxWriter, &attr, spuhandler_thread_SPUMailboxWriter, &dsmcbe_spu_states[i]) != 0)
			REPORT_ERROR("Failed to start an SPU service thread")
	}

	if (pthread_create(&dsmcbe_spu_states[0].eventHandler, &attr, spuhandler_thread_HandleEvents, NULL) != 0)
		REPORT_ERROR("Failed to start an SPU service thread")
#else
		unsigned int cur_thread = 0;

		if (dsmcbe_spu_thread_count < dsmcbe_spu_ppu_threadcount)
			dsmcbe_spu_ppu_threadcount = dsmcbe_spu_thread_count;

		dsmcbe_spu_mainthread = MALLOC(sizeof(pthread_t) * dsmcbe_spu_ppu_threadcount);

		unsigned int* tmp = MALLOC(sizeof(unsigned int) * dsmcbe_spu_ppu_threadcount);
		memset(tmp, 0, sizeof(unsigned int) * dsmcbe_spu_ppu_threadcount);

		unsigned int remaining_spu_threads = dsmcbe_spu_thread_count;

		i = 0;
		while(remaining_spu_threads > 0)
		{
			tmp[i]++;
			i = (i+1) % dsmcbe_spu_ppu_threadcount;
			remaining_spu_threads--;
		}

		for(i = 0; i < dsmcbe_spu_ppu_threadcount; i++)
		{
			//printf(WHERESTR "Starting thread %d with SPU %d to %d\n", WHEREARG, i, cur_thread, cur_thread + tmp[i]);
			//sleep(5);
			unsigned int* ranges = MALLOC(sizeof(unsigned int) * 2);
			ranges[0] = cur_thread;
			cur_thread += tmp[i];
			ranges[1] = cur_thread;
			pthread_create(&dsmcbe_spu_mainthread[i], &attr, dsmcbe_spu_mainthreadSpinning, ranges);
		}

		FREE(tmp);
#endif
}

//This function cleans up used resources 
void dsmcbe_spu_terminate(int force)
{
	printf("\n\n\n\n\n\n\n\n\n TERMINATING!!! \n\n\n\n\n\n\n\n\n");
	sleep(5);

	size_t i,j;
	
	//Remove warning
	dsmcbe_spu_do_terminate = force | TRUE;
	
	for(i = 0; i < dsmcbe_spu_ppu_threadcount; i++)
		pthread_join(dsmcbe_spu_mainthread[i], NULL);
	
	FREE(dsmcbe_spu_mainthread);
	
	for(i = 0; i < dsmcbe_spu_thread_count; i++)
	{
		dsmcbe_rc_UnregisterInvalidateSubscriber(&dsmcbe_spu_states[i].inQueue);

		//g_hash_table_destroy(dsmcbe_spu_states[i].activeDMATransfers);
		FREE(dsmcbe_spu_states[i].activeDMATransfers);
		g_queue_free(dsmcbe_spu_states[i].agedObjectMap);
		dsmcbe_spu_states[i].agedObjectMap = NULL;
		g_hash_table_destroy(dsmcbe_spu_states[i].itemsById);
		g_hash_table_destroy(dsmcbe_spu_states[i].itemsByPointer);
		g_queue_free(dsmcbe_spu_states[i].mailboxQueue);
		dsmcbe_spu_states[i].mailboxQueue = NULL;
		g_queue_free(dsmcbe_spu_states[i].releaseWaiters);
		dsmcbe_spu_states[i].releaseWaiters = NULL;
		g_queue_free(dsmcbe_spu_states[i].inQueue);
		dsmcbe_spu_states[i].inQueue = NULL;
		g_queue_free(dsmcbe_spu_states[i].streamItems);
		dsmcbe_spu_states[i].streamItems = NULL;
		dsmcbe_spu_memory_destroy(dsmcbe_spu_states[i].map);
		dsmcbe_spu_states[i].map = NULL;
		g_hash_table_destroy(dsmcbe_spu_states[i].csp_items);
		dsmcbe_spu_states[i].csp_items = NULL;

		for(j = 0; j < MAX_DMA_GROUPID; j++)
			FREE(dsmcbe_spu_states[i].pendingRequestsPointer[j]);

		pthread_mutex_destroy(&dsmcbe_spu_states[i].inMutex);

#ifdef USE_EVENTS
		pthread_cond_destroy(&dsmcbe_spu_states[i].inCond);
		pthread_mutex_destroy(&dsmcbe_spu_states[i].writerMutex);
		pthread_cond_destroy(&dsmcbe_spu_states[i].writerCond);
#endif
	}
	
	FREE(dsmcbe_spu_states);
	dsmcbe_spu_states = NULL;
}

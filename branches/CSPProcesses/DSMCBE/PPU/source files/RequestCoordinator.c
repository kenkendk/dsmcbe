/*
 * This module contains implementation code for
 * Coordinating requests from various sources
 *
 */
 
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <libspe2.h>
#include <RequestCoordinator.h>
#include <NetworkHandler.h>
#include <dsmcbe_ppu.h>
#include <debug.h>
#include <RequestCoordinator_CSP.h>
#include <dsmcbe_initializers.h>
#include <stdlib.h>

//#define DEBUG_PACKAGES
//#define DEBUG_COMMUNICATION

//#define ENABLE_MIGRATION

//The number of write requests to record
#define MAX_RECORDED_WRITE_REQUESTS 5
//The number of request required from a single machine 
// (out of MAX_RECORDED_WRITE_REQUESTS) for migration to activate 
#define MIGRATION_THRESHOLD 4


volatile int dsmcbe_rc_do_terminate;

pthread_mutex_t dsmcbe_rc_invalidate_queue_mutex;
pthread_mutex_t dsmcbe_rc_queue_mutex;
pthread_cond_t dsmcbe_rc_queue_ready;
GQueue* dsmcbe_rc_GbagOfTasks = NULL;
pthread_t dsmcbe_rc_workthread;

#define CAST_TO_PACKAGE(x) ((struct dsmcbe_createRequest*)(x)->dataRequest)

#define MAX_SEQUENCE_NR 1000000
unsigned int dsmcbe_rc_sequence_nr;

//This is the table of all pending creates
GHashTable* dsmcbe_rc_GpendingCreates;
//This is the table of all allocated active items
GHashTable* dsmcbe_rc_GallocatedItems;
//This is a table that keeps track of un-answered invalidates
GHashTable* dsmcbe_rc_GpendingSequenceNr;
//This is a table of items that await object creation
GHashTable* dsmcbe_rc_Gwaiters;
//This is a table with acquireRequests that are sent over the network, but not yet responded to
GHashTable* dsmcbe_rc_GpendingRequests;
//This is a table with priority requests, usually invalidate responses
GQueue* dsmcbe_rc_GpriorityResponses;

typedef struct dsmcbe_rc_dataObjectStruct *dataObject;

//This buffer is used in TestForMigration, but created here to avoid the overhead of creating it multiple times
unsigned int* dsmcbe_rc_request_count_buffer;


//This structure contains information about the registered objects
struct dsmcbe_rc_dataObjectStruct{
	
	//The objects GUID
	GUID id;
	//The object data in main memory
	void* EA;
	//The size of the object
	unsigned long size;
	//The list of pending requests
	GQueue* Gwaitqueue;
	//A list of ID's of write requesters, used for migration decisions
	GQueue* GrequestCount;
	//A table with invalidateSubscribers that have received the data
	GHashTable* GleaseTable;
	//This is a QueuableItem that should be notified when all invalidates are in
	QueueableItem writebufferReady;
	//Count indicating how many invalidates are unaccounted-for
	//UINT_MAX indicating NULL!
	unsigned int unaccountedInvalidates;
};

typedef struct dsmcbe_invalidateSubscriber* invalidateSubscriber;

//This structure is used to keep track of invalidate subscribers
struct dsmcbe_invalidateSubscriber
{
	pthread_mutex_t* mutex;
	pthread_cond_t* event;
	GQueue** Gqueue;
	int network;
};

//This list contains all current invalidate subscribers;
GHashTable* dsmcbe_rc_GinvalidateSubscribers;

//All package handlers have the same signature
typedef void (*packagehandler_function)(QueueableItem item);
packagehandler_function dsmcbe_rc_packagehandlers[MAX_PACKAGE_ID] ;

//This is the method the thread runs
void* dsmcbe_rc_ProccessWork(void* data);

//Callback functions
void dsmcbe_rc_HandleCreateRequest(QueueableItem item);
void dsmcbe_rc_HandleAcquireRequest(QueueableItem item);
void dsmcbe_rc_HandleReleaseRequest(QueueableItem item);
void dsmcbe_rc_HandleInvalidateRequest(QueueableItem item);
void dsmcbe_rc_HandleUpdateRequest(QueueableItem item);
void dsmcbe_rc_HandleInvalidateResponse(QueueableItem item);
void dsmcbe_rc_HandleAcquireResponse(QueueableItem item);
void dsmcbe_rc_DoAcquireBarrier(QueueableItem item);
void dsmcbe_rc_HandleAcquireBarrierResponse(QueueableItem item);
void dsmcbe_rc_HandleMigrationResponse(QueueableItem item);


QueueableItem dsmcbe_rc_new_QueueableItem(pthread_mutex_t* mutex, pthread_cond_t* cond, GQueue** queue, void* dataRequest, dsmcbe_rc_callback callback)
{
	QueueableItem res = (QueueableItem)MALLOC(sizeof(struct dsmcbe_QueueableItemStruct));
	res->mutex = mutex;
	res->event = cond;
	res->Gqueue = queue;
	res->dataRequest = dataRequest;
	res->callback = callback;

	return res;
}

dataObject dsmcbe_rc_new_dataObject(GUID id, void* ea, unsigned long size, GQueue* waitQueue, GQueue* requestCount, GHashTable* leaseTable)
{
	dataObject res = (dataObject)MALLOC(sizeof(struct dsmcbe_rc_dataObjectStruct));
	res->id = id;
	res->EA = ea;
	res->size = size;
	res->Gwaitqueue = waitQueue;
	res->GrequestCount = requestCount;
	res->GleaseTable = leaseTable;
	res->writebufferReady = NULL;
	res->unaccountedInvalidates = UINT_MAX;

	return res;
}

//Add another subscriber to the list
void dsmcbe_rc_RegisterInvalidateSubscriber(pthread_mutex_t* mutex, pthread_cond_t* event, GQueue** q, int network)
{
	invalidateSubscriber sub;
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&dsmcbe_rc_invalidate_queue_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	
	sub = MALLOC(sizeof(struct dsmcbe_invalidateSubscriber));
	
	sub->mutex = mutex;
	sub->event = event;
	sub->Gqueue = q;
	sub->network = network;
		
	g_hash_table_insert(dsmcbe_rc_GinvalidateSubscribers, q, sub);
	pthread_mutex_unlock(&dsmcbe_rc_invalidate_queue_mutex);
}

//Remove a subscriber from the list
void dsmcbe_rc_UnregisterInvalidateSubscriber(GQueue** q)
{
	//TODO: It is now possible to unsubscribe while the entry
	//is in the lease table of an object.
	
	//The fix could be to iterate all objects, and
	//remove the entries from all the objects
	
	pthread_mutex_lock(&dsmcbe_rc_invalidate_queue_mutex);
	
	void* ptr = g_hash_table_lookup(dsmcbe_rc_GinvalidateSubscribers, q);
	g_hash_table_remove(dsmcbe_rc_GinvalidateSubscribers, q);
	FREE(ptr);
	ptr = NULL;
	
	pthread_mutex_unlock(&dsmcbe_rc_invalidate_queue_mutex);
}

//Stops the coordination thread and releases all resources
void dsmcbe_rc_terminate(int force)
{
	
	int queueEmpty;
	
	if (force)
		dsmcbe_rc_do_terminate = 1;
		
	queueEmpty = 0;
	while(!queueEmpty)
	{
	 	pthread_mutex_lock(&dsmcbe_rc_queue_mutex);
	 	
	 	queueEmpty = g_queue_is_empty(dsmcbe_rc_GbagOfTasks);
	 	if (queueEmpty)
	 	{
	 		dsmcbe_rc_do_terminate = 1;
	 		pthread_cond_signal(&dsmcbe_rc_queue_ready);
	 	}

	 	pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
	}
		
	g_queue_free(dsmcbe_rc_GbagOfTasks);
	dsmcbe_rc_GbagOfTasks = NULL;
	
	g_queue_free(dsmcbe_rc_GpriorityResponses);
	dsmcbe_rc_GpriorityResponses = NULL;

	g_hash_table_destroy(dsmcbe_rc_GpendingCreates);
	dsmcbe_rc_GpendingCreates = NULL;
	g_hash_table_destroy(dsmcbe_rc_GallocatedItems);
	dsmcbe_rc_GallocatedItems = NULL;
	g_hash_table_destroy(dsmcbe_rc_Gwaiters);
	dsmcbe_rc_Gwaiters = NULL;
	g_hash_table_destroy(dsmcbe_rc_GpendingRequests);
	dsmcbe_rc_GpendingRequests = NULL;
	
	g_hash_table_destroy(dsmcbe_rc_cspChannels);
	dsmcbe_rc_cspChannels = NULL;
	g_hash_table_destroy(dsmcbe_rc_cspMultiWaiters);
	dsmcbe_rc_cspMultiWaiters = NULL;

	pthread_join(dsmcbe_rc_workthread, NULL);
	
	if (dsmcbe_rc_request_count_buffer != NULL)
		free(dsmcbe_rc_request_count_buffer);

	pthread_mutex_destroy(&dsmcbe_rc_queue_mutex);
	pthread_cond_destroy(&dsmcbe_rc_queue_ready);
}

//This method initializes all items related to the coordinator and starts the handler thread
void dsmcbe_rc_initialize()
{
	pthread_attr_t attr;
	dataObject obj;
	size_t i;

	if (dsmcbe_rc_GbagOfTasks == NULL)
	{
		dsmcbe_rc_GbagOfTasks = g_queue_new();
		if (dsmcbe_MachineCount() > 1) {
			dsmcbe_rc_request_count_buffer = MALLOC(sizeof(unsigned int) * dsmcbe_MachineCount());
		} else {
			dsmcbe_rc_request_count_buffer = NULL;
		}
			
		dsmcbe_rc_do_terminate = 0;
	
		//Setup all package handlers, clear array first
		memset(dsmcbe_rc_packagehandlers, 0, sizeof(void*) * MAX_PACKAGE_ID);

		dsmcbe_rc_packagehandlers[PACKAGE_CREATE_REQUEST] = &dsmcbe_rc_HandleCreateRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_ACQUIRE_REQUEST_READ] = &dsmcbe_rc_HandleAcquireRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_ACQUIRE_REQUEST_WRITE] = &dsmcbe_rc_HandleAcquireRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_RELEASE_REQUEST] = &dsmcbe_rc_HandleReleaseRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_INVALIDATE_REQUEST] = &dsmcbe_rc_HandleInvalidateRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_UPDATE] = &dsmcbe_rc_HandleUpdateRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_INVALIDATE_RESPONSE] = &dsmcbe_rc_HandleInvalidateResponse;
		dsmcbe_rc_packagehandlers[PACKAGE_ACQUIRE_RESPONSE] = &dsmcbe_rc_HandleAcquireResponse;
		dsmcbe_rc_packagehandlers[PACKAGE_ACQUIRE_BARRIER_REQUEST] = &dsmcbe_rc_DoAcquireBarrier;
		dsmcbe_rc_packagehandlers[PACKAGE_ACQUIRE_BARRIER_RESPONSE] = &dsmcbe_rc_HandleAcquireBarrierResponse;
		dsmcbe_rc_packagehandlers[PACKAGE_MIGRATION_RESPONSE] = &dsmcbe_rc_HandleMigrationResponse;
		dsmcbe_rc_packagehandlers[PACKAGE_CSP_CHANNEL_CREATE_REQUEST] = &dsmcbe_rc_csp_ProcessChannelCreateRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_CSP_CHANNEL_POISON_REQUEST] = &dsmcbe_rc_csp_ProcessChannelPoisonRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_CSP_CHANNEL_READ_REQUEST] = &dsmcbe_rc_csp_ProcessChannelReadRequest;
		dsmcbe_rc_packagehandlers[PACKAGE_CSP_CHANNEL_WRITE_REQUEST] = &dsmcbe_rc_csp_ProcessChannelWriteRequest;

		/* Initialize mutex and condition variable objects */
		pthread_mutex_init(&dsmcbe_rc_queue_mutex, NULL);
		pthread_cond_init (&dsmcbe_rc_queue_ready, NULL);

		/* For portability, explicitly create threads in a joinable state */
		dsmcbe_rc_GallocatedItems = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_GpendingSequenceNr = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_Gwaiters = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_GpendingRequests = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_GinvalidateSubscribers = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_GpendingCreates = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_GpriorityResponses = g_queue_new();
		
		dsmcbe_rc_cspChannels = g_hash_table_new(NULL, NULL);
		dsmcbe_rc_cspMultiWaiters = g_hash_table_new(NULL, NULL);

		//if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
		//{
			obj = dsmcbe_rc_new_dataObject(OBJECT_TABLE_ID, MALLOC_ALIGN(sizeof(OBJECT_TABLE_ENTRY_TYPE) * OBJECT_TABLE_SIZE, 7), sizeof(OBJECT_TABLE_ENTRY_TYPE) * OBJECT_TABLE_SIZE, g_queue_new(), g_queue_new(), g_hash_table_new(NULL, NULL));
			
			for(i = 0; i < OBJECT_TABLE_SIZE; i++)
				((OBJECT_TABLE_ENTRY_TYPE*)obj->EA)[i] = OBJECT_TABLE_RESERVED;

			((OBJECT_TABLE_ENTRY_TYPE*)obj->EA)[OBJECT_TABLE_ID] = OBJECT_TABLE_OWNER;
			if(g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)obj->id) == NULL)
				g_hash_table_insert(dsmcbe_rc_GallocatedItems, (void*)obj->id, obj);
			else
				REPORT_ERROR("Could not insert into acllocatedItems");
		//}

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_create(&dsmcbe_rc_workthread, &attr, dsmcbe_rc_ProccessWork, NULL);
		pthread_attr_destroy(&attr);
	
	}
}

void dsmcbe_rc_ProcessWaiters(OBJECT_TABLE_ENTRY_TYPE* objectTable)
{
	pthread_mutex_lock(&dsmcbe_rc_queue_mutex);
	
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, dsmcbe_rc_Gwaiters);
	while (g_hash_table_iter_next (&iter, &key, &value)) 
	{
		//printf(WHERESTR "Trying item %d\n", WHEREARG, (int)key);
		if (objectTable[(GUID)key] != OBJECT_TABLE_RESERVED)
		{
			//printf(WHERESTR "Matched, emptying queue item %d\n", WHEREARG, (GUID)ht_iter_get_key(it));
			GQueue* dq = g_hash_table_lookup(dsmcbe_rc_Gwaiters, key);
			while(!g_queue_is_empty(dq))
			{
				//printf(WHERESTR "processed a waiter for %d (%d)\n", WHEREARG, (GUID)key, (unsigned int)g_queue_peek_head(dq));
				g_queue_push_tail(dsmcbe_rc_GbagOfTasks, g_queue_pop_head(dq));
			}
			
			g_hash_table_iter_steal(&iter);							 
			g_queue_free(dq);
			dq = NULL;
		}
	}

	pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
}


//This method can be called from outside the module to set up a request
void dsmcbe_rc_EnqueItem(QueueableItem item)
{
 	pthread_mutex_lock(&dsmcbe_rc_queue_mutex);
 	
 	g_queue_push_tail(dsmcbe_rc_GbagOfTasks, (void*)item);
 	
 	pthread_cond_signal(&dsmcbe_rc_queue_ready);
 	pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
}

//This method enques a response for an invalidate
void dsmcbe_rc_EnqueInvalidateResponse(GUID id, unsigned int requestNumber)
{
	struct dsmcbe_invalidateResponse* resp = dsmcbe_new_invalidateResponse(id, requestNumber);
	
 	pthread_mutex_lock(&dsmcbe_rc_queue_mutex);
 	
 	g_queue_push_tail(dsmcbe_rc_GpriorityResponses, resp);
 	
 	pthread_cond_signal(&dsmcbe_rc_queue_ready);
 	pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
}

//Sends the message in data to the queue supplied
void dsmcbe_rc_SendMessage(QueueableItem item, void* data)
{
	if (item->mutex != NULL)
		pthread_mutex_lock(item->mutex);

	if (item->Gqueue != NULL)
		g_queue_push_tail(*(item->Gqueue), data);

	if (item->callback != NULL)
		(*(item->callback))(item, data);

	if (item->event != NULL)
		pthread_cond_signal(item->event);

	if (item->mutex != NULL)
		pthread_mutex_unlock(item->mutex);

	if (item->dataRequest != NULL)
	{
		FREE(item->dataRequest);
		item->dataRequest = NULL;
	}

	FREE(item);
	item = NULL;

}

//Helper method with common code for responding
//It sets the requestID on the response, and frees the data structures
void dsmcbe_rc_RespondAny(QueueableItem item, void* resp)
{
	unsigned int originator = UINT_MAX;
	unsigned int originalRecipient = UINT_MAX;
	unsigned int originalRequestID = UINT_MAX;

	if (CAST_TO_PACKAGE(item)->packageCode != PACKAGE_UPDATE)
	{
		originator = ((struct dsmcbe_createRequest*)item->dataRequest)->originator;
		originalRecipient = ((struct dsmcbe_createRequest*)item->dataRequest)->originalRecipient;
		originalRequestID = ((struct dsmcbe_createRequest*)item->dataRequest)->originalRequestID;
	}

	if (originator != UINT_MAX && originalRecipient != UINT_MAX && originalRequestID != UINT_MAX)
	{
		((struct dsmcbe_acquireResponse*)resp)->originator = originator;
		((struct dsmcbe_acquireResponse*)resp)->originalRecipient = originalRecipient;
		((struct dsmcbe_acquireResponse*)resp)->originalRequestID = originalRequestID;
	}

	//The actual type is not important, since the first two fields are 
	// layed out the same way for all packages
	((struct dsmcbe_acquireResponse*)resp)->requestID = CAST_TO_PACKAGE(item)->requestID;

#ifdef DEBUG_PACKAGES
	printf(WHERESTR "Responding with package %s (%d), reqId: %d, possible id: %d\n", WHEREARG, PACKAGE_NAME(((struct dsmcbe_acquireResponse*)resp)->packageCode), ((struct dsmcbe_acquireResponse*)resp)->packageCode, ((struct dsmcbe_acquireResponse*)resp)->requestID,  ((struct dsmcbe_acquireResponse*)resp)->dataItem);
#endif

	dsmcbe_rc_SendMessage(item, resp);
}

//Responds with NACK to a request
void dsmcbe_rc_RespondNACK(QueueableItem item)
{
	dsmcbe_rc_RespondAny(item, dsmcbe_new_NACK(CAST_TO_PACKAGE(item)->dataItem, CAST_TO_PACKAGE(item)->requestID));
}

//Responds to a barrier request
void dsmcbe_rc_RespondAcquireBarrier(QueueableItem item)
{
	dsmcbe_rc_RespondAny(item, dsmcbe_new_acquireBarrierResponse(CAST_TO_PACKAGE(item)->dataItem, CAST_TO_PACKAGE(item)->requestID));
}

unsigned int IsOnlyOwner(QueueableItem item, dataObject obj)
{
	unsigned int ht_count = g_hash_table_size(obj->GleaseTable);
	if (ht_count == 0 || (ht_count == 1 && g_hash_table_lookup(obj->GleaseTable, item->Gqueue) != NULL))
		return TRUE;
	else
		return FALSE;
}

//Responds to an acquire request
void dsmcbe_rc_RespondAcquire(QueueableItem item, dataObject obj, unsigned int onlyOwner)
{
	int mode = -1;
	if (CAST_TO_PACKAGE(item)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
		mode = ACQUIRE_MODE_WRITE;
	else if (CAST_TO_PACKAGE(item)->packageCode == PACKAGE_ACQUIRE_REQUEST_READ)
		mode = ACQUIRE_MODE_READ;
	else if (CAST_TO_PACKAGE(item)->packageCode == PACKAGE_CREATE_REQUEST)
		mode = ACQUIRE_MODE_CREATE;
	else
		REPORT_ERROR("Responding to unknown acquire type"); 

	dsmcbe_rc_RespondAny(item, dsmcbe_new_acquireResponse(obj->id, CAST_TO_PACKAGE(item)->requestID, mode, onlyOwner, obj->size, obj->EA));
}

OBJECT_TABLE_ENTRY_TYPE* dsmcbe_rc_GetObjectTable()
{
	dataObject otObj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)OBJECT_TABLE_ID);
	if (otObj == NULL)
		REPORT_ERROR("Object table was missing");
	if (otObj->EA == NULL)
		REPORT_ERROR("Object table was broken");
	return (OBJECT_TABLE_ENTRY_TYPE*)otObj->EA; 	
}

//Performs all actions related to a create request
void dsmcbe_rc_DoCreate(QueueableItem item, struct dsmcbe_createRequest* request)
{
	unsigned long size;
	unsigned int transfersize;
	void* data;
	dataObject object;
	
	//Check that the item is not already created
	if (g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)request->dataItem) != NULL)
	{
		REPORT_ERROR("Create request for already existing item");
		dsmcbe_rc_RespondNACK(item);
		return;
	}
	
	if (request->dataItem > OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR("GUID was larger than object table size");
		dsmcbe_rc_RespondNACK(item);
		return;
	}

	OBJECT_TABLE_ENTRY_TYPE* objTable = dsmcbe_rc_GetObjectTable();

	if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
	{
		if (objTable[request->dataItem] != OBJECT_TABLE_RESERVED)
		{
			REPORT_ERROR("Create request for already existing item");
			dsmcbe_rc_RespondNACK(item);
			return;
		}
		else
		{
			objTable[request->dataItem] = request->originator;
		}
	}

	if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
	{
		dsmcbe_net_Update(OBJECT_TABLE_ID, sizeof(OBJECT_TABLE_ENTRY_TYPE) * request->dataItem, sizeof(OBJECT_TABLE_ENTRY_TYPE), &(objTable[request->dataItem]));
	}

	if (request->originator == dsmcbe_host_number)
	{	
		size = request->dataSize;
		transfersize = ALIGNED_SIZE(size);
			
		data = MALLOC_ALIGN(transfersize, 7);

		if (data == NULL)
		{
			REPORT_ERROR("Failed to allocate buffer for create");
			dsmcbe_rc_RespondNACK(item);
			return;
		}

		GQueue* waitQueue = g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)object->id);
		
		//If there are pending acquires, add them to the list
		if (waitQueue != NULL)
			g_hash_table_remove(dsmcbe_rc_Gwaiters, (void*)object->id);
		else
			waitQueue = g_queue_new();
		
		// Make datastructures for later use
		object = dsmcbe_rc_new_dataObject(request->dataItem, data, size, waitQueue, g_queue_new(), g_hash_table_new(NULL, NULL));
		
		//Acquire the item for the creator
		g_queue_push_head(object->Gwaitqueue, NULL);
		
		//Register this item as created
		if (g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)object->id) == NULL)
			g_hash_table_insert(dsmcbe_rc_GallocatedItems, (void*)object->id, object);
		else 
			REPORT_ERROR("Could not insert into allocatedItems");
		
		//Notify the requester
		dsmcbe_rc_RespondAcquire(item, object, TRUE);
	}
	else if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
	{
		GQueue* dq = g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)request->dataItem);
		while(dq != NULL && !g_queue_is_empty(dq))
		{
			//printf(WHERESTR "processed a waiter for %d (%d)\n", WHEREARG, object->id, (unsigned int)g_queue_peek_head(dq));
			g_queue_push_tail(dsmcbe_rc_GbagOfTasks, g_queue_pop_head(dq));
		}
	}
}

//Perform all actions related to an invalidate
//If onlySubscribers is set, the network handler and local cache is not purged
void dsmcbe_rc_DoInvalidate(GUID dataItem, unsigned int removeLocalCopy)
{
	dataObject obj;
	invalidateSubscriber sub;

	if (dataItem == OBJECT_TABLE_ID && dsmcbe_host_number == OBJECT_TABLE_OWNER)
		return;
	
	if ((obj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)dataItem)) == NULL)
	{
		printf(WHERESTR "Id: %d, known objects: %d\n", WHEREARG, dataItem, g_hash_table_size(dsmcbe_rc_GallocatedItems));
		REPORT_ERROR("Attempted to invalidate an item that was not registered");
		return;
	}

	pthread_mutex_lock(&dsmcbe_rc_invalidate_queue_mutex);
	
	unsigned int leaseCount = g_hash_table_size(obj->GleaseTable);

	if (dsmcbe_host_number != dsmcbe_rc_GetMachineID(dataItem) && removeLocalCopy)
	{
		if(leaseCount != 0) {
			// Mark memory as dirty
			g_hash_table_remove(dsmcbe_rc_GallocatedItems, (void*)dataItem);
			if(obj->unaccountedInvalidates == UINT_MAX)
				obj->unaccountedInvalidates = 0;
			else
				REPORT_ERROR("Could not insert into allocatedItemsDirty");
		} else {
			FREE_ALIGN(obj->EA);
			obj->EA = NULL;
			g_hash_table_remove(dsmcbe_rc_GallocatedItems, (void*)dataItem);
			if (obj->Gwaitqueue != NULL)
				g_queue_free(obj->Gwaitqueue);
			if (obj->Gwaitqueue != NULL)
				g_queue_free(obj->GrequestCount);
			if (obj->GleaseTable != NULL)
				g_hash_table_destroy(obj->GleaseTable);
			FREE(obj);
			obj = NULL;
			pthread_mutex_unlock(&dsmcbe_rc_invalidate_queue_mutex);
			return;
		}
	}
	else
	{
		if (leaseCount == 0)
		{
			pthread_mutex_unlock(&dsmcbe_rc_invalidate_queue_mutex);
			return;
		}

		if(obj->unaccountedInvalidates == UINT_MAX)
			obj->unaccountedInvalidates = 0;
		else
			REPORT_ERROR("Could not insert into allocatedItemsDirty");
	}	

	GHashTableIter iter;
	g_hash_table_iter_init(&iter, obj->GleaseTable);
	
	void* key;
	void* value;
	while(g_hash_table_iter_next(&iter, &key, &value))
	{
		struct dsmcbe_invalidateRequest* requ = dsmcbe_new_invalidateRequest(dataItem, 0);
		
		requ->requestID = NEXT_SEQ_NO(dsmcbe_rc_sequence_nr, MAX_SEQUENCE_NR);
		
		if (g_hash_table_size(dsmcbe_rc_GpendingSequenceNr) > (MAX_SEQUENCE_NR / 2))
			REPORT_ERROR("Likely problem with indvalidate requests...");

		while (g_hash_table_lookup(dsmcbe_rc_GpendingSequenceNr, (void*)requ->requestID) != NULL && g_hash_table_size(dsmcbe_rc_GpendingSequenceNr) < MAX_SEQUENCE_NR)
			requ->requestID = NEXT_SEQ_NO(dsmcbe_rc_sequence_nr, MAX_SEQUENCE_NR);

		sub = g_hash_table_lookup(dsmcbe_rc_GinvalidateSubscribers, key);
		if (sub == NULL)
		{
			//This happens if the requester is unsubscribed
			REPORT_ERROR("Failed to locate subscriber");
			continue;
		}
		
		if (g_hash_table_lookup(dsmcbe_rc_GpendingSequenceNr, (void*)requ->requestID) == NULL)
			g_hash_table_insert(dsmcbe_rc_GpendingSequenceNr, (void*)requ->requestID, obj);
		else
		{
			printf(WHERESTR "Seqnr is :%d, table count is: %d\n", WHEREARG, requ->requestID, g_hash_table_size(dsmcbe_rc_GpendingSequenceNr));
			REPORT_ERROR("Could not insert into pendingSequenceNr");
		}

		obj->unaccountedInvalidates++;

		pthread_mutex_lock(sub->mutex);
		
		g_queue_push_tail(*sub->Gqueue, requ);
		if (sub->event != NULL)
			pthread_cond_signal(sub->event);
		pthread_mutex_unlock(sub->mutex); 
	}
	pthread_mutex_unlock(&dsmcbe_rc_invalidate_queue_mutex);
	
	g_hash_table_remove_all(obj->GleaseTable);
}

QueueableItem dsmcbe_rc_RecordBufferRequest(QueueableItem item, dataObject obj)
{
	QueueableItem temp = MALLOC(sizeof(struct dsmcbe_QueueableItemStruct));
	memcpy(temp, item, sizeof(struct dsmcbe_QueueableItemStruct));

	temp->dataRequest = MALLOC(sizeof(struct dsmcbe_acquireRequest));
	memcpy(temp->dataRequest, item->dataRequest, sizeof(struct dsmcbe_acquireRequest));

	if (CAST_TO_PACKAGE(item)->packageCode != PACKAGE_ACQUIRE_REQUEST_WRITE)
		REPORT_ERROR("Recording buffer entry for non acquire or non write");

	//printf(WHERESTR "Inserting into writebuffer table: %d, %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem, (int)obj);
	if(obj->writebufferReady == NULL)
	{
		//printf(WHERESTR "Inserted item into writebuffer table: %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem);
		obj->writebufferReady = temp;
	}
	else
	{
		printf(WHERESTR "*EXT*\n", WHEREARG);
		REPORT_ERROR("Could not insert into writebufferReady, element exists");
	}
	
	return temp;
}

//Performs all actions related to an acquire request
void dsmcbe_rc_DoAcquireBarrier(QueueableItem item)
{
	struct dsmcbe_acquireBarrierRequest* request;
	GQueue* q;
	dataObject obj;

	request = (struct dsmcbe_acquireBarrierRequest*)(item->dataRequest);

	OBJECT_TABLE_ENTRY_TYPE machineId = dsmcbe_rc_GetMachineID(request->dataItem);
	
	if (machineId == OBJECT_TABLE_RESERVED)
	{
		if (g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)request->dataItem) == NULL)
			g_hash_table_insert(dsmcbe_rc_Gwaiters, (void*)request->dataItem, g_queue_new());
		g_queue_push_tail(g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)request->dataItem), item);
	}
	else if (machineId != dsmcbe_host_number)
	{
		dsmcbe_net_Request(item, machineId);
	}
	else
	{
		//Check that the item exists
		if ((obj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)request->dataItem)) != NULL)
		{
			if (obj->size < (sizeof(unsigned int) * 2))
				REPORT_ERROR("Invalid size for barrier!");
			
			//We keep a count, because g_queue_get_length iterates the queue
			q = obj->Gwaitqueue;
			((unsigned int*)obj->EA)[1]++;
			
			//We have the last acquire in, free them all!
			if (((unsigned int*)obj->EA)[1] == ((unsigned int*)obj->EA)[0])
			{
#ifdef DEBUG_COMMUNICATION
				printf(WHERESTR "Releasing barrier on id %i\n", WHEREARG, request->dataItem);
#endif	
				
				((unsigned int*)obj->EA)[1] = 0;
				while(!g_queue_is_empty(q))
					dsmcbe_rc_RespondAcquireBarrier(g_queue_pop_head(q));
				
				//Also respond to the last one in, but we did not put it in the queue
				dsmcbe_rc_RespondAcquireBarrier(item);
			}
			else
				g_queue_push_tail(q, item);
			
		}
	}
}

int dsmcbe_rc_TestForMigration()
{
#ifdef ENABLE_MIGRATION
	unsigned int machine_count = DSMCBE_MachineCount();
	if (machine_count > 1)
	{
		if (obj->GrequestCount == NULL)
			REPORT_ERROR("object had no requestCount queue");
		
		if (next->dataRequest == NULL)
			REPORT_ERROR("No request on item");
		if (((struct dsmcbe_acquireRequest*)next->dataRequest)->packageCode != PACKAGE_ACQUIRE_REQUEST_WRITE)
			REPORT_ERROR("TestForMigration called with unwanted package");
		
		unsigned int* m = &((struct dsmcbe_acquireRequest*)next->dataRequest)->originator;
		
		if ((*m) == UINT_MAX)
		{
			//This should not happen
			REPORT_ERROR("Setting originator on package"); 
			(*m) = dsmcbe_host_number;
		}
			
		if (*m >= machine_count) {
			REPORT_ERROR2("Machine was %d", *m);
		} else {
			//printf(WHERESTR "Queued %d\n", WHEREARG, *m);
			g_queue_push_tail(obj->GrequestCount, (void*)((struct dsmcbe_acquireRequest*)next->dataRequest)->originator);
		}
		
		if (g_queue_get_length(obj->GrequestCount) > MAX_RECORDED_WRITE_REQUESTS)
		{
			g_queue_pop_head(obj->GrequestCount);
			size_t i;
			
			if (dsmcbe_rc_request_count_buffer == NULL)
				REPORT_ERROR("Broken buffer");
				
			memset(dsmcbe_rc_request_count_buffer, 0, sizeof(unsigned int) * machine_count);

			//TODO: It should be faster to avoid g_queue_peek_nth and use g_queue_foreach instead
			 
			//Step 1 count number of occurrences
			for(i = 0; i < g_queue_get_length(obj->GrequestCount); i++)
			{
				unsigned int machine = (unsigned int)g_queue_peek_nth(obj->GrequestCount, i);
				//printf(WHERESTR "Queue i: %d, machine: %d, list: %d\n", WHEREARG, i, machine, (unsigned int)dsmcbe_rc_request_count_buffer);
				if (machine >= machine_count) {
					REPORT_ERROR2("Machine id was too large, id: %d", machine);
				} else
					dsmcbe_rc_request_count_buffer[machine]++;
			}
			
			//Step 2, examine hits
			for(i = 0; i < machine_count; i++)
				if (i != dsmcbe_host_number && dsmcbe_rc_request_count_buffer[i] > MIGRATION_THRESHOLD)
				{
#ifdef DEBUG_COMMUNICATION
					printf(WHERESTR "Migration threshold exceeded for object %d by machine %d, initiating migration\n", WHEREARG, obj->id, i);
#endif
					dsmcbe_rc_PerformMigration(next, (struct acquireRequest*)next->dataRequest, obj, i);
					dsmcbe_rc_DoInvalidate(obj->id, TRUE);
					return TRUE;
				}
		}
	}
#endif	
	return FALSE;
}

void dsmcbe_rc_SendWriteBufferReady(dataObject object)
{
	QueueableItem reciever = object->writebufferReady;
	object->writebufferReady = NULL;
			
	dsmcbe_rc_RespondAny(reciever, dsmcbe_new_writeBufferReady(object->id, CAST_TO_PACKAGE(reciever)->requestID));
}

//Performs all actions related to an acquire request
void dsmcbe_rc_DoAcquire(QueueableItem item, struct dsmcbe_acquireRequest* request)
{
	GQueue* q;
	dataObject obj;

	//printf(WHERESTR "Start acquire on id %i\n", WHEREARG, request->dataItem);

	//Check that the item exists
	if ((obj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)request->dataItem)) != NULL)
	{
		q = obj->Gwaitqueue;
						
		//If the object is not locked, register as locked and respond
		if (g_queue_is_empty(q))
		{
			//printf(WHERESTR "Object not locked\n", WHEREARG);
			if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
				//printf(WHERESTR "Acquiring READ on not locked object\n", WHEREARG);
				g_hash_table_insert(obj->GleaseTable, item->Gqueue, g_hash_table_lookup(dsmcbe_rc_GinvalidateSubscribers, item->Gqueue));
				dsmcbe_rc_RespondAcquire(item, obj, FALSE);
			} else if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE) {			
				//printf(WHERESTR "Acquiring WRITE on not locked object\n", WHEREARG);
				//if (machineId == 0 && request->dataItem == 0 && request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
					//printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, requestID %i\n", WHEREARG, request->dataItem, machineId, dsmcbe_host_number, request->requestID);

				if (dsmcbe_rc_TestForMigration(item, obj))
					return;
				
				void* requestor = item->Gqueue;
				unsigned int onlyOwner = IsOnlyOwner(item, obj);
				
				g_queue_push_head(q, NULL);
				
				if (obj->id != OBJECT_TABLE_ID && !onlyOwner)
					dsmcbe_rc_RecordBufferRequest(item, obj);
				
				dsmcbe_rc_RespondAcquire(item, obj, onlyOwner);
					
				if (obj->id != OBJECT_TABLE_ID)
				{
					if (!onlyOwner)
					{
						//When onlyOwner is false, size of GleaseTable cannot be 0!
						if (g_hash_table_size(obj->GleaseTable) == 0)
						{
							printf(WHERESTR "This should not happen!!\n", WHEREARG);
							dsmcbe_rc_SendWriteBufferReady(obj);
						}
						else
							g_hash_table_remove(obj->GleaseTable, requestor);
						
						dsmcbe_rc_DoInvalidate(obj->id, FALSE);
					}
				}
				else
				{
					//printf(WHERESTR "Sending NetUpdate with ID %u, Offset %u, Size %u, Data %u\n", WHEREARG, obj->id, 0, OBJECT_TABLE_SIZE, obj->EA);					
					dsmcbe_net_Update(obj->id, 0, OBJECT_TABLE_SIZE, obj->EA);
				}
				
				g_hash_table_insert(obj->GleaseTable, requestor, (void*)1);

				//if (g_hash_table_lookup(dsmcbe_rc_GinvalidateSubscribers, requester) == NULL)
				//	REPORT_ERROR("Bad insert");
					
			}

		}
		else {
			//Otherwise add the request to the wait list			
			//if (machineId == 0 && request->dataItem == 0 && request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
				//printf(WHERESTR "Object locked\n", WHEREARG);

			//printf(WHERESTR "Object locked, count %d, id: %d\n", WHEREARG, g_queue_get_length(q), request->dataItem);
				
			g_queue_push_tail(q, item);
		}
	}
	else
	{
		//Create a list if none exists

		//if (machineId == 0 && request->dataItem == 0 && request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
			//printf(WHERESTR "acquire requested for id %d, waiting for create\n", WHEREARG, request->dataItem);

		//Append the request to the waiters, for use when the object gets created
		q = g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)request->dataItem);

		if (q == NULL)
		{
			q = g_queue_new();
			g_hash_table_insert(dsmcbe_rc_Gwaiters, (void*)request->dataItem, (void*)q);
		}

		g_queue_push_tail(q, item);		
	}
}

//Performs all actions related to a release
void dsmcbe_rc_DoRelease(QueueableItem item, struct dsmcbe_releaseRequest* request, dataObject obj)
{
	
	GQueue* q;
	QueueableItem next;
	
	//OBJECT_TABLE_ENTRY_TYPE machineId = GetMachineID(request->dataItem);
	//if (machineId == 0 && request->dataItem == 0)
		//printf(WHERESTR "Release for item %d, machineid: %d, machine id: %d, requestID %i\n", WHEREARG, request->dataItem, machineId, dsmcbe_host_number, request->requestID);
	
	//printf(WHERESTR "Performing release for %d\n", WHEREARG, request->dataItem);
	if (request->mode == ACQUIRE_MODE_READ)
	{
		//TODO: Possibly remove requester from queue
		//printf(WHERESTR "Performing read-release for %d\n", WHEREARG, request->dataItem);
		return;
	}
	
	//Ensure that the item exists
	if (obj != NULL)
	{
		q = obj->Gwaitqueue;
		
		//printf(WHERESTR "%d queue pointer: %d\n", WHEREARG, request->dataItem, (int)q);
		
		//Ensure that the item was actually locked
		if (g_queue_is_empty(q))
		{
			REPORT_ERROR("Bad release, item was not locked!");
			dsmcbe_rc_RespondNACK(item);
		}
		else
		{
			//Get the next pending request
			next = g_queue_pop_head(q);
			if (next != NULL)
			{
				REPORT_ERROR("Bad queue, the top entry was not a locker!");
				g_queue_push_head(q, next);
				dsmcbe_rc_RespondNACK(item);
			}
			else
			{
				//if(g_queue_is_empty(q))
					//printf(WHERESTR "Queue is empty\n", WHEREARG);
					
				if (obj->id == OBJECT_TABLE_ID && dsmcbe_host_number == OBJECT_TABLE_OWNER)
				{	
					//ProcessWaiters(obj->EA == NULL ? g_hash_table_lookup(dsmcbe_rc_GallocatedItems, OBJECT_TABLE_ID) : obj->EA);
					//printf(WHERESTR "Sending NetUpdate with ID %u, Offset %u, Size %u, Data %u\n", WHEREARG, obj->id, 0, OBJECT_TABLE_SIZE, obj->EA);
					dsmcbe_net_Update(OBJECT_TABLE_ID, 0, request->dataSize, obj->EA);
				}
				
				FREE(item);
				FREE(request);
				
				if (!g_queue_is_empty(q) && CAST_TO_PACKAGE((QueueableItem)g_queue_peek_head(q))->packageCode != PACKAGE_ACQUIRE_BARRIER_REQUEST)
			    {
					while (!g_queue_is_empty(q))
					{
						//Acquire for the next in the queue
						next = g_queue_pop_head(q);
						if (CAST_TO_PACKAGE(next)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE){
	
							if (dsmcbe_rc_TestForMigration(next, obj))
								return;
														
							void* requestor = next->Gqueue; 
							unsigned int onlyOwner = IsOnlyOwner(next, obj);
							
							g_queue_push_head(q, NULL);
	
							GUID id = obj->id;
							if (id != OBJECT_TABLE_ID && !onlyOwner)
								dsmcbe_rc_RecordBufferRequest(next, obj);

							dsmcbe_rc_RespondAcquire(next, obj, onlyOwner);

							if (id != OBJECT_TABLE_ID)
							{
								if (!onlyOwner)
								{
									if (g_hash_table_size(obj->GleaseTable) == 0)
										dsmcbe_rc_SendWriteBufferReady(obj);
									else
										g_hash_table_remove(obj->GleaseTable, requestor);
	
									dsmcbe_rc_DoInvalidate(id, FALSE);
								}
							}
				
							if (g_hash_table_lookup(obj->GleaseTable, requestor) == NULL)
								g_hash_table_insert(obj->GleaseTable, requestor, (void*)1);


							if (g_hash_table_lookup(dsmcbe_rc_GinvalidateSubscribers, requestor) == NULL)
								REPORT_ERROR("Bad insert");
							
							break; //Done
						} else if (CAST_TO_PACKAGE(next)->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
							g_hash_table_insert(obj->GleaseTable, next->Gqueue, (void*)1);
							dsmcbe_rc_RespondAcquire(next, obj, FALSE);
						} else {
							REPORT_ERROR2("packageCode was neither WRITE nor READ, real ID %d", obj->id);
							REPORT_ERROR2("packageCode was neither WRITE nor READ, ID %d", CAST_TO_PACKAGE(next)->dataItem);
							REPORT_ERROR2("packageCode was neither WRITE nor READ, but %d", CAST_TO_PACKAGE(next)->packageCode);
						}						 
					}
			    }
			    else if (!g_queue_is_empty(q)) //We have a list of barrierRequest's
			    {
					//printf(WHERESTR "Barrier had waiting requests: id %i, waiting: %i\n", WHEREARG, request->dataItem, g_queue_get_length(q));

					((unsigned int*)obj->EA)[1] = g_queue_get_length(q);
					
			    	//Are there enough to signal the barrier?
			    	if (((unsigned int*)obj->EA)[0] == ((unsigned int*)obj->EA)[1])
			    	{
						//printf(WHERESTR "Signaling barrier: id %i, waiting: %i\n", WHEREARG, request->dataItem, g_queue_get_length(q));
			    		
			    		//Simulate that it just arrived
			    		QueueableItem x = g_queue_pop_tail(q);
			    		((unsigned int*)obj->EA)[1]--;
			    		
			    		dsmcbe_rc_DoAcquireBarrier(x);
			    	}
			    }
			}		
		}
	}
	else
	{
		REPORT_ERROR("Tried to release a non-existing item");
		dsmcbe_rc_RespondNACK(item);
	}

	//printf(WHERESTR "Release done\n", WHEREARG);
}

OBJECT_TABLE_ENTRY_TYPE dsmcbe_rc_GetMachineID(GUID id)
{
	return dsmcbe_rc_GetObjectTable()[id];
}


void dsmcbe_rc_HandleCreateRequest(QueueableItem item)
{
	struct dsmcbe_createRequest* req = item->dataRequest;

	//printf(WHERESTR "processing create event\n", WHEREARG);
	if (OBJECT_TABLE_OWNER != dsmcbe_host_number) {
		
		//Check if we can decline the request early on
		if (dsmcbe_rc_GetObjectTable()[req->dataItem] != OBJECT_TABLE_RESERVED)
		{
			REPORT_ERROR("Attempted to create existing item");
			dsmcbe_rc_RespondNACK(item);
			return;
		}
		
		dsmcbe_net_Request(item, OBJECT_TABLE_OWNER);
		if (g_hash_table_lookup(dsmcbe_rc_GpendingCreates, (void*)req->dataItem) != NULL)
		{
			REPORT_ERROR("Attempted to create item which already has a pending create");
			dsmcbe_rc_RespondNACK(item);
		}
		else
			g_hash_table_insert(dsmcbe_rc_GpendingCreates, (void*)req->dataItem, item);
	} else { 
		dsmcbe_rc_DoCreate(item, req);
	}
}

void dsmcbe_rc_HandleAcquireRequest(QueueableItem item)
{
	
	struct dsmcbe_acquireRequest* req = item->dataRequest;
	OBJECT_TABLE_ENTRY_TYPE machineId = dsmcbe_rc_GetMachineID(req->dataItem);
	void* obj;
	
	if (machineId != dsmcbe_host_number && machineId != OBJECT_TABLE_RESERVED)
	{
		if ((obj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)req->dataItem)) != NULL && req->packageCode == PACKAGE_ACQUIRE_REQUEST_READ)
		{
			dsmcbe_rc_RespondAcquire(item, obj, FALSE);
		}
		else
		{
			GQueue* queue = NULL;
			if ((queue = g_hash_table_lookup(dsmcbe_rc_GpendingRequests, (void*)req->dataItem)) == NULL)
			{
				queue = g_queue_new();
				g_hash_table_insert(dsmcbe_rc_GpendingRequests, (void*)req->dataItem, queue);
			}

			dsmcbe_net_Request(item, machineId);
			g_queue_push_tail(queue, item);
		}
	}
	else if (machineId == OBJECT_TABLE_RESERVED)
	{
		GQueue* queue = NULL;
		if ((queue = g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)req->dataItem)) == NULL)
		{
			queue = g_queue_new();
			g_hash_table_insert(dsmcbe_rc_Gwaiters, (void*)req->dataItem, queue);
		}
		g_queue_push_tail(queue, item);
	}
	else 
	{
		dsmcbe_rc_DoAcquire(item, req);
	}	
}

void dsmcbe_rc_HandleReleaseRequest(QueueableItem item)
{
	//printf(WHERESTR "processing release event\n", WHEREARG);	
	struct dsmcbe_releaseRequest* req = item->dataRequest;
	OBJECT_TABLE_ENTRY_TYPE machineId = dsmcbe_rc_GetMachineID(req->dataItem);

	if (machineId != dsmcbe_host_number)
	{
		dsmcbe_net_Request(item, machineId);
		FREE(item->dataRequest);
	}
	else
	{
		dataObject obj = NULL;

		if (req->mode == ACQUIRE_MODE_WRITE)
		{
			obj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)req->dataItem);
			if (obj->writebufferReady != NULL || obj->unaccountedInvalidates != UINT_MAX)
			{
				//printf(WHERESTR "processing release event, object %d is in use, re-registering\n", WHEREARG, obj->id);
				//The object is still in use, re-register, the last invalidate response will free it
				//The temp stuff is used to preserve the queues on the new object
				
				GQueue* tmp = obj->Gwaitqueue;
				GQueue* tmp2 = obj->GrequestCount;
				GHashTable* tmp3 = obj->GleaseTable;
				obj->Gwaitqueue = NULL;
				obj->GrequestCount = NULL;
				obj->GleaseTable = NULL;
				
				if (req->data == NULL)
					req->data = obj->EA;
					
				obj = dsmcbe_rc_new_dataObject(req->dataItem, req->data, req->dataSize, tmp, tmp2, tmp3);

				g_hash_table_insert(dsmcbe_rc_GallocatedItems, (void*)obj->id, obj);
			}
			else
			{
				//The object is not in use, just copy in the new version
				if (obj->EA != req->data && req->data != NULL)
				{
					memcpy(obj->EA, req->data, obj->size);
					FREE_ALIGN(req->data);
					req->data = NULL;				
				}
			}
		}
		
		dsmcbe_rc_DoRelease(item, req, obj);
	}
}

void dsmcbe_rc_HandleInvalidateRequest(QueueableItem item)
{
	struct dsmcbe_invalidateRequest* req = item->dataRequest;
	
	dsmcbe_rc_DoInvalidate(req->dataItem, TRUE);
	
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item = NULL;
}

void dsmcbe_rc_HandleUpdateRequest(QueueableItem item)
{
	struct dsmcbe_updateRequest* req = item->dataRequest;
	
	if (dsmcbe_host_number != OBJECT_TABLE_OWNER && req->dataItem == OBJECT_TABLE_ID)	
	{
		memcpy(((void*)dsmcbe_rc_GetObjectTable()) + req->offset, req->data, req->dataSize);

		if (req->dataSize == sizeof(OBJECT_TABLE_ENTRY_TYPE))
		{
			unsigned int id = req->offset / sizeof(OBJECT_TABLE_ENTRY_TYPE);

			QueueableItem item = g_hash_table_lookup(dsmcbe_rc_GpendingCreates, (void*)id);

			if (item != NULL)
			{
				g_hash_table_remove(dsmcbe_rc_GpendingCreates, (void*)req->dataItem);
				dsmcbe_rc_DoCreate(item, item->dataRequest);
			}

			GQueue* dq = g_hash_table_lookup(dsmcbe_rc_Gwaiters, (void*)id);
			while(dq != NULL && !g_queue_is_empty(dq))
			{
				g_queue_push_tail(dsmcbe_rc_GbagOfTasks, g_queue_pop_head(dq));
			}
		}
		else
		{
			REPORT_ERROR("Grouped updates are not tested!");
			
			GHashTableIter iter;
			gpointer key, value;
			OBJECT_TABLE_ENTRY_TYPE* objectTable = dsmcbe_rc_GetObjectTable();
	
			g_hash_table_iter_init (&iter, dsmcbe_rc_GpendingCreates);
			while (g_hash_table_iter_next (&iter, &key, &value))
				if (objectTable[(GUID)key] != OBJECT_TABLE_RESERVED)
				{
					g_hash_table_steal(dsmcbe_rc_GpendingCreates, key);
					dsmcbe_rc_DoCreate((QueueableItem)value, ((QueueableItem)value)->dataRequest);
				} 

			
			dsmcbe_rc_ProcessWaiters(dsmcbe_rc_GetObjectTable());
		}
	}
	else
	{
		REPORT_ERROR("Bad update detected!");
	}
	
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item = NULL;	
}

void dsmcbe_rc_HandleInvalidateResponse(QueueableItem item)
{
	
	dataObject object;
	struct dsmcbe_invalidateResponse* req = item->dataRequest;
	
	if ((object = g_hash_table_lookup(dsmcbe_rc_GpendingSequenceNr, (void*)req->requestID)) == NULL)
	{
		FREE(item->dataRequest);
		item->dataRequest = NULL;
		FREE(item);
		item  = NULL;

		REPORT_ERROR("Incoming invalidate response did not match a pending request");
		return;
	}

	if (object->unaccountedInvalidates == UINT_MAX)
	{
		g_hash_table_remove(dsmcbe_rc_GpendingSequenceNr, (void*)req->requestID);
		FREE(item->dataRequest);
		item->dataRequest = NULL;
		FREE(item);
		item  = NULL;
				
		REPORT_ERROR("Incoming invalidate response was found, but there was no object for it");
		return;
	}
	
	object->unaccountedInvalidates--;

	if (object->unaccountedInvalidates <= 0)
	{
		object->unaccountedInvalidates = UINT_MAX;
		QueueableItem reciever = NULL;
		
		if((reciever = object->writebufferReady) != NULL)
		{
			//Notify waiter that the buffer is now ready

			object->writebufferReady = NULL;
			dsmcbe_rc_RespondAny(reciever, dsmcbe_new_writeBufferReady(object->id, CAST_TO_PACKAGE(reciever)->requestID));
		}
		else
		{
			//This happens when the invalidate comes from the network, or the acquireResponse has new data
		}

		dataObject temp = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)object->id);
		
		if (temp == NULL || temp != object)
		{  		
			printf(WHERESTR "Special case: Why is object not in allocatedItemsDirty or allocedItems?\n", WHEREARG);
			
			//Make sure we are not still using the actual memory
			if (temp == NULL || temp->EA != object->EA)
				FREE_ALIGN(object->EA);
			object->EA = NULL;
			//Release all control structures on the object
			if (object->Gwaitqueue != NULL)
				g_queue_free(object->Gwaitqueue);
			if (object->Gwaitqueue != NULL)
				g_queue_free(object->GrequestCount);
			if (object->GleaseTable != NULL)
				g_hash_table_destroy(object->GleaseTable);
			FREE(object);
			object = NULL;
		}
	}
	else
	{
		//Still pending invalidateResponse packages
	}

	//Clean up this response
	g_hash_table_remove(dsmcbe_rc_GpendingSequenceNr, (void*)req->requestID);
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item  = NULL;
}

void dsmcbe_rc_SingleInvalidate(QueueableItem item, GUID id)
{
	struct dsmcbe_invalidateRequest* req = dsmcbe_new_invalidateRequest(id, 0);
	req->requestID = 0;
	
	if (item->mutex != NULL)
		pthread_mutex_lock(item->mutex);

	g_queue_push_tail(*(item->Gqueue), req);
	
	if (item->event != NULL)
		pthread_cond_signal(item->event);
		
	if (item->mutex != NULL)
		pthread_mutex_unlock(item->mutex);
}

void dsmcbe_rc_HandleAcquireResponse(QueueableItem item)
{
	
	struct dsmcbe_acquireResponse* req = item->dataRequest;
	dataObject object;

	if (req->dataSize == 0 || (dsmcbe_host_number == OBJECT_TABLE_OWNER && req->dataItem == OBJECT_TABLE_ID))
	{
		if ((object = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)req->dataItem)) == NULL)
			REPORT_ERROR("Requester had sent response without data for non-existing local object");
			
		//Copy back the object table on the object table owner
		if (req->dataSize != 0 && req->data != NULL && object->EA != NULL && req->data != object->EA)
			memcpy(object->EA, req->data, req->dataSize);
			
		req->dataSize = object->size;
		req->data = object->EA;
		
		//If data is not changed, there is no need to invalidate locally
	}
	else
	{
		//Registering item locally

		if (req->data == NULL)
			REPORT_ERROR("Acquire response had empty data");	

		object = dsmcbe_rc_new_dataObject(req->dataItem, req->data, req->dataSize, NULL, g_queue_new(), g_hash_table_new(NULL, NULL));

		if(g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)object->id) == NULL)
			g_hash_table_insert(dsmcbe_rc_GallocatedItems, (void*)object->id, object);
		else
		{
			dsmcbe_rc_DoInvalidate(object->id, TRUE);
			if (g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)object->id) == NULL)
				g_hash_table_insert(dsmcbe_rc_GallocatedItems, (void*)object->id, object);
			else
				REPORT_ERROR("Could not insert into allocatedItems");
		}
	}

	//If the response is a objecttable acquire, check if items have been created, that we are awaiting 
	if (object->id == OBJECT_TABLE_ID)
	{
		dsmcbe_rc_ProcessWaiters(object->EA);
	}

	GQueue* dq = NULL;
	//If this is an acquire for an object we requested, release the waiters
	char isLocked = 1;
	if ((dq = g_hash_table_lookup(dsmcbe_rc_GpendingRequests, (void*)object->id)) != NULL)
	{
		pthread_mutex_lock(&dsmcbe_rc_queue_mutex);
		
		while(!g_queue_is_empty(dq))
		{
			QueueableItem q = (QueueableItem)g_queue_pop_head(dq);
			if (CAST_TO_PACKAGE(q)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
			{
				if (req->mode != ACQUIRE_MODE_READ)
				{
					pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
					isLocked = 0;
					unsigned int onlyOwner = IsOnlyOwner(q, object);
					void* requestor = q->Gqueue;
					
					if (!onlyOwner)
						dsmcbe_rc_RecordBufferRequest(q, object);
						
					dsmcbe_rc_RespondAcquire(q, g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)object->id), onlyOwner);
					
					if (!onlyOwner)
					{
						if (g_hash_table_size(object->GleaseTable) == 0)
							dsmcbe_rc_SendWriteBufferReady(object);
						
						dsmcbe_rc_DoInvalidate(object->id, TRUE);
					}
					
					g_hash_table_insert(object->GleaseTable, requestor, (void*)1);
				}
				else
				{
					//Oops, put it back in!
					g_queue_push_head(dq, q);
				}
				break;
			}
			else
			{
				//Re-insert into request queue
				g_queue_push_tail(dsmcbe_rc_GbagOfTasks, q);
			}
		}
		if (isLocked)
		{
			pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
		}

		if (g_queue_is_empty(dq))
		{
			g_hash_table_remove(dsmcbe_rc_GpendingRequests, (void*)object->id);
			g_queue_free(dq);
			dq = NULL;
		}
		
	}

	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);				
	item = NULL;
}

void dsmcbe_rc_HandleAcquireBarrierResponse(QueueableItem item)
{
	//If we receive one from the network, simply forward it
	struct dsmcbe_acquireBarrierResponse* resp = dsmcbe_new_acquireBarrierResponse(CAST_TO_PACKAGE(item)->dataItem, CAST_TO_PACKAGE(item)->requestID);
	resp->originator = CAST_TO_PACKAGE(item)->originator;
	resp->originalRequestID = CAST_TO_PACKAGE(item)->originalRequestID;
	resp->originalRecipient = CAST_TO_PACKAGE(item)->originalRecipient;

	dsmcbe_rc_RespondAny(item, resp);
}

//Handles an incoming migrationResponse
void dsmcbe_rc_HandleMigrationResponse(QueueableItem item)
{
	struct dsmcbe_migrationResponse* resp = (struct dsmcbe_migrationResponse*)item->dataRequest;

	//Clear any local copies first
	dsmcbe_rc_DoInvalidate(resp->dataItem, TRUE);

	//Make sure we are the new owner (the update is delayed)
	dsmcbe_rc_GetObjectTable()[resp->dataItem] = dsmcbe_host_number;

	//Pretend the package was an acquire response
	struct dsmcbe_acquireResponse* aresp = dsmcbe_new_acquireResponse(resp->dataItem, resp->requestID, resp->mode, TRUE, resp->dataSize, resp->data);
	
	//Copy all fields into the new structure	
	aresp->originalRecipient = resp->originalRecipient;
	aresp->originalRequestID = resp->originalRequestID;
	aresp->originator = resp->originator;
	item->dataRequest = aresp;

	dsmcbe_rc_HandleAcquireResponse(item);

	//HandleAcquireResponse assumes that the object is owned by another machine
	dataObject obj = g_hash_table_lookup(dsmcbe_rc_GallocatedItems, (void*)resp->dataItem);
	if (obj == NULL)
		REPORT_ERROR("Received item, but it did not exist?")
	
	if (obj->Gwaitqueue == NULL)
		obj->Gwaitqueue = g_queue_new();
	if (obj->GrequestCount == NULL)
		obj->GrequestCount = g_queue_new();
	if (obj->GleaseTable == NULL)
		obj->GleaseTable = g_hash_table_new(NULL, NULL);
		
	//Since the request is not registered, the object does not appear locked but it is
	g_queue_push_head(obj->Gwaitqueue, NULL); 
	free(resp);

}

void dsmcbe_rc_PerformMigration(QueueableItem item, struct dsmcbe_acquireRequest* req, dataObject obj, OBJECT_TABLE_ENTRY_TYPE owner)
{
	//Update local table
	dsmcbe_rc_GetObjectTable()[obj->id] = owner;
	
	int mode = req->packageCode == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
	struct dsmcbe_migrationResponse* resp = dsmcbe_new_migrationResponse(obj->id, 0, mode, obj->size, obj->EA);
	
	dsmcbe_rc_RespondAny(item, resp);
	
	//Forward all requests to the new owner
	while(!g_queue_is_empty(obj->Gwaitqueue))
	{
		g_queue_pop_head(obj->Gwaitqueue);
		dsmcbe_net_Request(item, owner);
		free(item->dataRequest);
		free(item);
	}
	
	//Notify others about the new owner, 
	//any package in-transit will be forwarded by this machine to the new owner
	dsmcbe_net_Update(OBJECT_TABLE_ID, sizeof(OBJECT_TABLE_ENTRY_TYPE) * obj->id, sizeof(OBJECT_TABLE_ENTRY_TYPE), &(dsmcbe_rc_GetObjectTable()[obj->id]));
}

//This is the main thread function
void* dsmcbe_rc_ProccessWork(void* data)
{
	QueueableItem item;
	unsigned int datatype;
	struct timespec ts;
	packagehandler_function handler;
	unsigned int consecutiveTimeouts;
	unsigned int timedWaitReturnValue;

	while(!dsmcbe_rc_do_terminate)
	{
		//Get the next item, or sleep until it arrives	
		pthread_mutex_lock(&dsmcbe_rc_queue_mutex);

		consecutiveTimeouts = 0;

		while (g_queue_is_empty(dsmcbe_rc_GbagOfTasks) && g_queue_is_empty(dsmcbe_rc_GpriorityResponses) && !dsmcbe_rc_do_terminate)
		{
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 10;

			timedWaitReturnValue = pthread_cond_timedwait(&dsmcbe_rc_queue_ready, &dsmcbe_rc_queue_mutex, &ts);

			if (g_queue_is_empty(dsmcbe_rc_GpriorityResponses) && g_queue_is_empty(dsmcbe_rc_GbagOfTasks))
			{
				consecutiveTimeouts++;

#ifdef DEBUG
				printf("RequestCoordinator got timeout %d, return value: %d\n", consecutiveTimeouts, timedWaitReturnValue);
#endif

#ifdef DEBUG
				//One minute in DEBUG mode
				if (consecutiveTimeouts > 6)
#else
				//Five minutes in RELEASE mode
				if (consecutiveTimeouts > 30)
#endif
				{
					REPORT_ERROR("Terminating because RequestCoordinator got 10 consecutive timeouts (60 seconds of waittime)");
					exit(-1);
				}
				continue;
			}
#ifdef DEBUG
			else if (timedWaitReturnValue != 0)
			{
				REPORT_ERROR2("pthread_cond_timedwait returned an error: %d, but there was data. This indicates an error in DSMCBE's use of locks", timedWaitReturnValue);
			}
#endif
		}
		
		if (dsmcbe_rc_do_terminate)
		{
			pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
			break;
		}
		
		//We prioritize object table responses
		if (!g_queue_is_empty(dsmcbe_rc_GpriorityResponses))
		{
			item = dsmcbe_rc_new_QueueableItem(&dsmcbe_rc_queue_mutex, &dsmcbe_rc_queue_ready, &dsmcbe_rc_GpriorityResponses, g_queue_pop_head(dsmcbe_rc_GpriorityResponses), NULL);
		}
		else
		{
			item = (QueueableItem)g_queue_pop_head(dsmcbe_rc_GbagOfTasks);
			if (item == NULL)
			{
				pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
				REPORT_ERROR("Empty entry in request queue");
				continue;
			}
			if (item->dataRequest == NULL)
			{
				pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);
				REPORT_ERROR2("Empty request in queued item (%d)", (unsigned int)item)
				free(item);
				continue;
			}
		}
			
		pthread_mutex_unlock(&dsmcbe_rc_queue_mutex);

		//Get the type of the package and perform the corresponding action
		datatype = CAST_TO_PACKAGE(item)->packageCode;

#ifdef DEBUG_PACKAGES
		printf(WHERESTR "processing type %s (%d), reqId: %d, possible id: %d\n", WHEREARG, PACKAGE_NAME(datatype), datatype, CAST_TO_PACKAGE(item)->requestID, CAST_TO_PACKAGE(item)->dataItem);
#endif		

		handler = datatype >= MAX_PACKAGE_ID ? NULL : dsmcbe_rc_packagehandlers[datatype];

		if (handler == NULL)
		{
			REPORT_ERROR2("Unknown package recieved with code %i recieved", datatype);
			handler = dsmcbe_rc_RespondNACK;
		}

		//Invoke the corresponding handler

		//All responses ensure that the QueueableItem and request structures are free'd
		//It is the obligation of the requester to free the response
		handler(item);
	}
	
	//Returning the unused argument removes a warning
	return data;
}


void* __malloc_w_check(unsigned int size, char* file, int line)
{
	void* tmp = malloc(size);
	if (tmp == NULL || size == 0)
		fprintf(stderr, "* ERROR * [file %s, line %d]: Out of memory: %s, (%i)\n", file, line, strerror(errno), errno);
		
	return tmp;
 }

void* __malloc_align_w_check(unsigned int size, unsigned int power, char* file, int line)
{
	void* tmp = _malloc_align(size, power);
	if (tmp == NULL || size == 0)
		fprintf(stderr, "* ERROR * [file %s, line %d]: Out of memory: %s, (%i)\n", file, line, strerror(errno), errno);
		
	return tmp;
}  

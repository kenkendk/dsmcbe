/*
 *
 * This module contains implementation code for
 * Coordinating requests from various sources
 *
 */
 
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <libspe2.h>
#include "../header files/RequestCoordinator.h"
#include "../header files/NetworkHandler.h"

#include "../../common/debug.h"

//#define DEBUG_PACKAGES
//#define DEBUG_COMMUNICATION

//#define ENABLE_MIGRATION

//The number of write requests to record
#define MAX_RECORDED_WRITE_REQUESTS 5
//The number of request required from a single machine 
// (out of MAX_RECORDED_WRITE_REQUESTS) for migration to activate 
#define MIGRATION_THRESHOLD 4


volatile int terminate;

pthread_mutex_t rc_invalidate_queue_mutex;
pthread_mutex_t rc_queue_mutex;
pthread_cond_t rc_queue_ready;
GQueue* rc_GbagOfTasks = NULL;
pthread_t rc_workthread;

#define MAX_SEQUENCE_NR 1000000
unsigned int rc_sequence_nr;

#define OPTIMISTIC_CREATE
#ifndef OPTIMISTIC_CREATE 
//This is the table of all pending creates
GHashTable* rc_GpendingCreates;
#endif
//This is the table of all allocated active items
GHashTable* rc_GallocatedItems;
//This is a temporary table with objects that are slated for deletion
GHashTable* rc_GallocatedItemsDirty;
//This is a table that keeps track of un-answered invalidates
GHashTable* rc_GpendingSequenceNr;
//This is a table of items that await object creation
GHashTable* rc_Gwaiters;
//This is a table with QueuableItems that should be notified when all invalidates are in
GHashTable* rc_GwritebufferReady;
//This is a table with acquireRequests that are sent over the network, but not yet responded to
GHashTable* rc_GpendingRequests;

GQueue* rc_GpriorityResponses;

typedef struct dataObjectStruct *dataObject;

//This buffer is used in TestForMigration, but created here to avoid the overhead of creating it multiple times
unsigned int* rc_request_count_buffer;

//This structure contains information about the registered objects
struct dataObjectStruct{
	
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
};

typedef struct invalidateSubscriber* invalidateSubscriber;

//This structure is used to keep track of invalidate subscribers
struct invalidateSubscriber
{
	pthread_mutex_t* mutex;
	pthread_cond_t* event;
	GQueue** Gqueue;
};

//This list contains all current invalidate subscribers;
GHashTable* rc_GinvalidateSubscribers;

//This is the method the thread runs
void* rc_ProccessWork(void* data);

//Add another subscriber to the list
void RegisterInvalidateSubscriber(pthread_mutex_t* mutex, pthread_cond_t* event, GQueue** q)
{
	invalidateSubscriber sub;
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&rc_invalidate_queue_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	
	if ((sub = MALLOC(sizeof(struct invalidateSubscriber))) == NULL)
		REPORT_ERROR("malloc error");	
	
	sub->mutex = mutex;
	sub->event = event;
	sub->Gqueue = q; 
		
	g_hash_table_insert(rc_GinvalidateSubscribers, q, sub);
	pthread_mutex_unlock(&rc_invalidate_queue_mutex);
}

//Remove a subscriber from the list
void UnregisterInvalidateSubscriber(GQueue** q)
{
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&rc_invalidate_queue_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	
	void* ptr = g_hash_table_lookup(rc_GinvalidateSubscribers, q);
	g_hash_table_remove(rc_GinvalidateSubscribers, q); 
	FREE(ptr);
	ptr = NULL;
	
	pthread_mutex_unlock(&rc_invalidate_queue_mutex);
}

//Stops the coordination thread and releases all resources
void TerminateCoordinator(int force)
{
	
	int queueEmpty;
	
	if (force)
		terminate = 1;
		
	queueEmpty = 0;
	while(!queueEmpty)
	{
	 	//printf(WHERESTR "locking mutex\n", WHEREARG);
	 	pthread_mutex_lock(&rc_queue_mutex);
	 	//printf(WHERESTR "locked mutex\n", WHEREARG);
	 	
	 	queueEmpty = g_queue_is_empty(rc_GbagOfTasks);
	 	if (queueEmpty)
	 	{
	 		terminate = 1;
	 		pthread_cond_signal(&rc_queue_ready);
	 	}
	 	//printf(WHERESTR "unlocking mutex\n", WHEREARG);
	 	pthread_mutex_unlock(&rc_queue_mutex);
	 	//printf(WHERESTR "unlocked mutex\n", WHEREARG);
	}
		
	g_queue_free(rc_GbagOfTasks);
	rc_GbagOfTasks = NULL;
	
	g_queue_free(rc_GpriorityResponses);
	rc_GpriorityResponses = NULL;

#ifndef OPTIMISTIC_CREATE	
	g_hash_table_destroy(rc_GpendingCreates);
	rc_GpendingCreates = NULL;
#endif	
	g_hash_table_destroy(rc_GallocatedItems);
	rc_GallocatedItems = NULL;
	g_hash_table_destroy(rc_GallocatedItemsDirty);
	rc_GallocatedItemsDirty = NULL;
	g_hash_table_destroy(rc_Gwaiters);
	rc_Gwaiters = NULL;
	g_hash_table_destroy(rc_GpendingRequests);
	rc_GpendingRequests = NULL;
	
	pthread_join(rc_workthread, NULL);
	
	if (rc_request_count_buffer != NULL)
		free(rc_request_count_buffer);
		
	pthread_mutex_destroy(&rc_queue_mutex);
	pthread_cond_destroy(&rc_queue_ready);
}

//This method initializes all items related to the coordinator and starts the handler thread
void InitializeCoordinator()
{
	
	pthread_attr_t attr;
	dataObject obj;
	size_t i;

	if (rc_GbagOfTasks == NULL)
	{
		rc_GbagOfTasks = g_queue_new();
		if (DSMCBE_MachineCount() > 1) {
			if ((rc_request_count_buffer = malloc(sizeof(unsigned int) * DSMCBE_MachineCount())) == NULL)
				REPORT_ERROR("malloc error");
		} else {
			rc_request_count_buffer = NULL;
		}
			
		terminate = 0;
	
		/* Initialize mutex and condition variable objects */
		pthread_mutex_init(&rc_queue_mutex, NULL);
		pthread_cond_init (&rc_queue_ready, NULL);

		/* For portability, explicitly create threads in a joinable state */
		rc_GallocatedItems = g_hash_table_new(NULL, NULL);
		rc_GallocatedItemsDirty = g_hash_table_new(NULL, NULL);
		rc_GpendingSequenceNr = g_hash_table_new(NULL, NULL);
		rc_Gwaiters = g_hash_table_new(NULL, NULL);
		rc_GwritebufferReady = g_hash_table_new(NULL, NULL);
		rc_GpendingRequests = g_hash_table_new(NULL, NULL);
		rc_GinvalidateSubscribers = g_hash_table_new(NULL, NULL);
#ifndef OPTIMISTIC_CREATE
		rc_GpendingCreates = g_hash_table_new(NULL, NULL);
#endif		
		rc_GpriorityResponses = g_queue_new();
		
		//if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
		//{
			if ((obj = MALLOC(sizeof(struct dataObjectStruct))) == NULL)
				REPORT_ERROR("MALLOC error");
				
			obj->size = sizeof(OBJECT_TABLE_ENTRY_TYPE) * OBJECT_TABLE_SIZE;
			obj->EA = MALLOC_ALIGN(obj->size, 7);
			obj->id = OBJECT_TABLE_ID;
			obj->Gwaitqueue = g_queue_new();
			obj->GrequestCount = g_queue_new();
			
			for(i = 0; i < OBJECT_TABLE_SIZE; i++)
				((OBJECT_TABLE_ENTRY_TYPE*)obj->EA)[i] = OBJECT_TABLE_RESERVED;

			((OBJECT_TABLE_ENTRY_TYPE*)obj->EA)[OBJECT_TABLE_ID] = OBJECT_TABLE_OWNER;
			if(g_hash_table_lookup(rc_GallocatedItems, (void*)obj->id) == NULL) 
				g_hash_table_insert(rc_GallocatedItems, (void*)obj->id, obj);
			else
				REPORT_ERROR("Could not insert into acllocatedItems");
		//}

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_create(&rc_workthread, &attr, rc_ProccessWork, NULL);
		pthread_attr_destroy(&attr);
	
	}
}

void ProcessWaiters(OBJECT_TABLE_ENTRY_TYPE* objectTable)
{
	//printf(WHERESTR "Releasing local waiters\n", WHEREARG);
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&rc_queue_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	//printf(WHERESTR "Releasing local waiters\n", WHEREARG);
	
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, rc_Gwaiters);
	while (g_hash_table_iter_next (&iter, &key, &value)) 
	{
		//printf(WHERESTR "Trying item %d\n", WHEREARG, (int)key);
		if (objectTable[(GUID)key] != OBJECT_TABLE_RESERVED)
		{
			//printf(WHERESTR "Matched, emptying queue item %d\n", WHEREARG, (GUID)ht_iter_get_key(it));
			GQueue* dq = g_hash_table_lookup(rc_Gwaiters, key);
			while(!g_queue_is_empty(dq))
			{
				//printf(WHERESTR "processed a waiter for %d (%d)\n", WHEREARG, (GUID)key, (unsigned int)g_queue_peek_head(dq));
				g_queue_push_tail(rc_GbagOfTasks, g_queue_pop_head(dq));
			}
			
			g_hash_table_iter_steal(&iter);							 
			g_queue_free(dq);
			dq = NULL;
		}
	}

	//printf(WHERESTR "unlocking mutex\n", WHEREARG);
	pthread_mutex_unlock(&rc_queue_mutex);
	//printf(WHERESTR "unlocked mutex\n", WHEREARG);
	//printf(WHERESTR "Released local waiters\n", WHEREARG);
}


//This method can be called from outside the module to set up a request
void EnqueItem(QueueableItem item)
{
	
	//printf(WHERESTR "adding item to queue: %i\n", WHEREARG, (int)item);
 	//printf(WHERESTR "locking mutex\n", WHEREARG);
 	pthread_mutex_lock(&rc_queue_mutex);
 	//printf(WHERESTR "locked mutex\n", WHEREARG);
 	
 	g_queue_push_tail(rc_GbagOfTasks, (void*)item);
	//printf(WHERESTR "setting event\n", WHEREARG);
 	
 	pthread_cond_signal(&rc_queue_ready);
 	//printf(WHERESTR "unlocking mutex\n", WHEREARG);
 	pthread_mutex_unlock(&rc_queue_mutex);
 	//printf(WHERESTR "unlocked mutex\n", WHEREARG);

	//printf(WHERESTR "item added to queue\n", WHEREARG);
}

//This method enques a response for an invalidate
void EnqueInvalidateResponse(unsigned int requestNumber)
{
	
	struct invalidateResponse* resp;
	
	if((resp = MALLOC(sizeof(struct invalidateResponse))) == NULL)
		REPORT_ERROR("malloc error");
	
	resp->packageCode = PACKAGE_INVALIDATE_RESPONSE;
	resp->requestID = requestNumber;
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
 	pthread_mutex_lock(&rc_queue_mutex);
 	//printf(WHERESTR "locked mutex\n", WHEREARG);
 	
 	g_queue_push_tail(rc_GpriorityResponses, resp);
	//printf(WHERESTR "setting event\n", WHEREARG);
 	
 	pthread_cond_signal(&rc_queue_ready);
	//printf(WHERESTR "unlocking mutext\n", WHEREARG);
 	pthread_mutex_unlock(&rc_queue_mutex);
 	//printf(WHERESTR "unlocked mutex\n", WHEREARG);	
}

//Helper method with common code for responding
//It sets the requestID on the response, and frees the data structures
void RespondAny(QueueableItem item, void* resp)
{
	unsigned int originator = UINT_MAX;
	unsigned int originalRecipient = UINT_MAX;
	unsigned int originalRequestID = UINT_MAX;

	switch(((struct createRequest*)item->dataRequest)->packageCode)
	{
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			originator = ((struct acquireBarrierRequest*)item->dataRequest)->originator;
			originalRecipient = ((struct acquireBarrierRequest*)item->dataRequest)->originalRecipient;
			originalRequestID = ((struct acquireBarrierRequest*)item->dataRequest)->originalRequestID;
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			originator = ((struct acquireRequest*)item->dataRequest)->originator;
			originalRecipient = ((struct acquireRequest*)item->dataRequest)->originalRecipient;
			originalRequestID = ((struct acquireRequest*)item->dataRequest)->originalRequestID;
			break;
		case PACKAGE_CREATE_REQUEST:
			originator = ((struct createRequest*)item->dataRequest)->originator;
			originalRecipient = ((struct createRequest*)item->dataRequest)->originalRecipient;
			originalRequestID = ((struct createRequest*)item->dataRequest)->originalRequestID;
			break;
		case PACKAGE_MIGRATION_REQUEST:
			originator = ((struct migrationRequest*)item->dataRequest)->originator;
			originalRecipient = ((struct migrationRequest*)item->dataRequest)->originalRecipient;
			originalRequestID = ((struct migrationRequest*)item->dataRequest)->originalRequestID;
			break;
		/*case PACKAGE_RELEASE_REQUEST:
			originator = ((struct releaseRequest*)item->dataRequest)->originator;
			originalRecipient = ((struct releaseRequest*)item->dataRequest)->originalRecipient;
			originalRequestID = ((struct releaseRequest*)item->dataRequest)->originalRequestID;
			break;*/
	}


	if (originator != UINT_MAX && originalRecipient != UINT_MAX && originalRequestID != UINT_MAX)
		switch(((struct createRequest*)resp)->packageCode)
		{
			case PACKAGE_ACQUIRE_RESPONSE:
				((struct acquireResponse*)resp)->originator = originator;
				((struct acquireResponse*)resp)->originalRecipient = originalRecipient;
				((struct acquireResponse*)resp)->originalRequestID = originalRequestID;
				break;
			case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
				((struct acquireBarrierResponse*)resp)->originator = originator;
				((struct acquireBarrierResponse*)resp)->originalRecipient = originalRecipient;
				((struct acquireBarrierResponse*)resp)->originalRequestID = originalRequestID;
				break;
			case PACKAGE_MIGRATION_RESPONSE:
				((struct migrationResponse*)resp)->originator = originator;
				((struct migrationResponse*)resp)->originalRecipient = originalRecipient;
				((struct migrationResponse*)resp)->originalRequestID = originalRequestID;
				break;
			case PACKAGE_NACK:
				((struct NACK*)resp)->originator = originator;
				((struct NACK*)resp)->originalRecipient = originalRecipient;
				((struct NACK*)resp)->originalRequestID = originalRequestID;
				break;
			/*case PACKAGE_RELEASE_RESPONSE:
				break;*/
		}
	
	//printf(WHERESTR "responding to %i\n", WHEREARG, (int)item);
	//The actual type is not important, since the first two fields are 
	// layed out the same way for all packages
	((struct acquireResponse*)resp)->requestID = ((struct acquireRequest*)item->dataRequest)->requestID;

	//printf(WHERESTR "responding, locking %i\n", WHEREARG, (int)item->mutex);
	
	if (item->mutex != NULL)
	{
		//printf(WHERESTR "locking item->mutex\n", WHEREARG);
		pthread_mutex_lock(item->mutex);
		//printf(WHERESTR "locked item->mutex\n", WHEREARG);
	}
	
	//printf(WHERESTR "responding, locking %i, packagetype: %d\n", WHEREARG, (int)item->mutex, ((struct acquireRequest*)resp)->packageCode);
	//printf(WHERESTR "responding, locked %i\n", WHEREARG, (int)item->queue);
	
	if (item->Gqueue != NULL)
		g_queue_push_tail(*(item->Gqueue), resp);
		
	if (item->callback != NULL)
	{
		//printf(WHERESTR "responding, callback %i\n", WHEREARG, (int)item->callback);
		(*(item->callback))(item, resp);
		//printf(WHERESTR "responding, callback %i\n", WHEREARG, (int)item->callback);
	}
		
	if (item->event != NULL)
		pthread_cond_signal(item->event);
	
	//printf(WHERESTR "responding, signalled %i\n", WHEREARG, (int)item->event);
	
	//printf(WHERESTR "responded, unlocking %i\n", WHEREARG, (int)item->mutex);
	if (item->mutex != NULL)
		pthread_mutex_unlock(item->mutex);
	
	//printf(WHERESTR "responding, done\n", WHEREARG);
	
	
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item = NULL;
}

//Responds with NACK to a request
void RespondNACK(QueueableItem item)
{
	
	struct NACK* resp;
	if ((resp = (struct NACK*)MALLOC(sizeof(struct NACK))) == NULL)
		REPORT_ERROR("MALLOC error");
			
	resp->packageCode = PACKAGE_NACK;
	resp->hint = 0;
		
	RespondAny(item, resp);
}

//Responds to a barrier request
void RespondAcquireBarrier(QueueableItem item)
{
	//printf(WHERESTR "Responding to acquire barrier\n", WHEREARG);

	struct acquireBarrierResponse* resp;
	if ((resp = (struct acquireBarrierResponse*)MALLOC(sizeof(struct acquireBarrierResponse))) == NULL)
		REPORT_ERROR("MALLOC error");
			
	resp->packageCode = PACKAGE_ACQUIRE_BARRIER_RESPONSE;
	
	RespondAny(item, resp);
}

//Responds to an acquire request
void RespondAcquire(QueueableItem item, dataObject obj)
{
	struct acquireResponse* resp;
	if ((resp = (struct acquireResponse*)MALLOC(sizeof(struct acquireResponse))) == NULL)
		REPORT_ERROR("MALLOC error");

	//printf(WHERESTR "Responding to acquire for item for %d\n", WHEREARG, obj->id);

	resp->requestID = ((struct acquireRequest*)(item->dataRequest))->requestID;
	resp->packageCode = PACKAGE_ACQUIRE_RESPONSE;
	resp->dataSize = obj->size;
	resp->data = obj->EA;
	resp->dataItem = obj->id;
	
	if (((struct acquireRequest*)item->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
		resp->mode = ACQUIRE_MODE_WRITE;
	else if (((struct acquireRequest*)item->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_READ)
		resp->mode = ACQUIRE_MODE_READ;
	else if (((struct acquireRequest*)item->dataRequest)->packageCode == PACKAGE_CREATE_REQUEST)
		resp->mode = ACQUIRE_MODE_CREATE;
	else
		REPORT_ERROR("Responding to unknown acquire type"); 

	//printf(WHERESTR "Responding to acquire for item for %d, mode: %d, isNotOwner: %d\n", WHEREARG, obj->id, resp->mode, dsmcbe_host_number != GetMachineID(obj->id));

	RespondAny(item, resp);	
}

//Responds to a release request
void RespondRelease(QueueableItem item)
{
	
	struct releaseResponse* resp;
	
	//TODO: This occasionally leaks memory
	if ((resp = (struct releaseResponse*)MALLOC(sizeof(struct releaseResponse))) == NULL)
		REPORT_ERROR("MALLOC error");
	
	resp->packageCode = PACKAGE_RELEASE_RESPONSE;

	RespondAny(item, resp);	
}

OBJECT_TABLE_ENTRY_TYPE* GetObjectTable()
{
	dataObject otObj = g_hash_table_lookup(rc_GallocatedItems, (void*)OBJECT_TABLE_ID);
	if (otObj == NULL)
		REPORT_ERROR("Object table was missing");
	if (otObj->EA == NULL)
		REPORT_ERROR("Object table was broken");
	return (OBJECT_TABLE_ENTRY_TYPE*)otObj->EA; 	
}

//Performs all actions releated to a create request
void DoCreate(QueueableItem item, struct createRequest* request)
{
	
	unsigned long size;
	unsigned int transfersize;
	void* data;
	dataObject object;
	
	//printf(WHERESTR "Create request for %d\n", WHEREARG, request->dataItem);
	
	//Check that the item is not already created
	if (g_hash_table_lookup(rc_GallocatedItems, (void*)request->dataItem) != NULL)
	{
		REPORT_ERROR("Create request for already existing item");
		RespondNACK(item);
		return;
	}
	
	if (request->dataItem > OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR("GUID was larger than object table size");
		RespondNACK(item);
		return;
	}

	OBJECT_TABLE_ENTRY_TYPE* objTable = GetObjectTable();
#ifdef OPTIMISTIC_CREATE
	
	if (objTable[request->dataItem] != OBJECT_TABLE_RESERVED)
	{
		REPORT_ERROR("Create request for already existing item");
		RespondNACK(item);
		return;
	}
	else
	{
		objTable[request->dataItem] = request->originator;
		//printf(WHERESTR "Inserting creator %u for ID %u\n", WHEREARG, request->originator, request->dataItem);
		
		GQueue* dq = g_hash_table_lookup(rc_Gwaiters, (void*)request->dataItem);
		while(dq != NULL && !g_queue_is_empty(dq))
		{
			//printf(WHERESTR "processed a waiter for %d, (%d)\n", WHEREARG, request->dataItem, (unsigned int)g_queue_peek_head(dq));
			g_queue_push_tail(rc_GbagOfTasks, g_queue_pop_head(dq));
		}		
	}
	
#else
	if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
	{
		if (objTable[request->dataItem] != OBJECT_TABLE_RESERVED)
		{
			REPORT_ERROR("Create request for already existing item");
			RespondNACK(item);
			return;
		}
		else
		{
			objTable[request->dataItem] = request->originator;
		}
	}
#endif

	if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
	{
		NetUpdate(OBJECT_TABLE_ID, sizeof(OBJECT_TABLE_ENTRY_TYPE) * request->dataItem, sizeof(OBJECT_TABLE_ENTRY_TYPE), &(objTable[request->dataItem]));
	}

	if (request->originator == dsmcbe_host_number)
	{	
		size = request->dataSize;
		transfersize = ALIGNED_SIZE(size);
			
		data = MALLOC_ALIGN(transfersize, 7);
		if (data == NULL)
		{
			REPORT_ERROR("Failed to allocate buffer for create");
			RespondNACK(item);
			return;
		}
		
		//printf(WHERESTR "Create request for %d, size: %d, actual size: %d\n", WHEREARG, request->dataItem, (int)request->dataSize, (int)transfersize);
		
		// Make datastructures for later use
		if ((object = (dataObject)MALLOC(sizeof(struct dataObjectStruct))) == NULL)
			REPORT_ERROR("MALLOC error");
		
		object->id = request->dataItem;
		object->EA = data;
		object->size = size;
		object->GrequestCount = g_queue_new();
	
		//If there are pending acquires, add them to the list
		if ((object->Gwaitqueue = g_hash_table_lookup(rc_Gwaiters, (void*)object->id)) != NULL)
		{
			//printf(WHERESTR "Create request for %d, waitqueue was not empty\n", WHEREARG, request->dataItem);
			g_hash_table_remove(rc_Gwaiters, (void*)object->id);
		}
		else
		{
			//printf(WHERESTR "Create request for %d, waitqueue was empty\n", WHEREARG, request->dataItem);
			object->Gwaitqueue = g_queue_new();
		}
			
		//Acquire the item for the creator
		g_queue_push_head(object->Gwaitqueue, NULL);
		
		//Register this item as created
		if (g_hash_table_lookup(rc_GallocatedItems, (void*)object->id) == NULL)
			g_hash_table_insert(rc_GallocatedItems, (void*)object->id, object);
		else 
			REPORT_ERROR("Could not insert into allocatedItems");
		
		//Notify the requestor 
		RespondAcquire(item, object);	
	}
	else if (dsmcbe_host_number == OBJECT_TABLE_OWNER)
	{
		GQueue* dq = g_hash_table_lookup(rc_Gwaiters, (void*)request->dataItem);
		while(dq != NULL && !g_queue_is_empty(dq))
		{
			//printf(WHERESTR "processed a waiter for %d (%d)\n", WHEREARG, object->id, (unsigned int)g_queue_peek_head(dq));
			g_queue_push_tail(rc_GbagOfTasks, g_queue_pop_head(dq));
		}
	}
}

//Perform all actions related to an invalidate
//If onlySubscribers is set, the network handler and local cache is not purged
void DoInvalidate(GUID dataItem, unsigned int onlySubscribers)
{
	
	GList* kl;
	GList* toplist;
	dataObject obj;
	invalidateSubscriber sub;

	//printf(WHERESTR "Invalidating...\n", WHEREARG);
	
	if (dataItem == OBJECT_TABLE_ID && dsmcbe_host_number == OBJECT_TABLE_OWNER)
		return;
	
	if ((obj = g_hash_table_lookup(rc_GallocatedItems, (void*)dataItem)) == NULL)
	{
		printf(WHERESTR "Id: %d, known objects: %d\n", WHEREARG, dataItem, g_hash_table_size(rc_GallocatedItems));
		REPORT_ERROR("Attempted to invalidate an item that was not registered");
		return;
	}

	//printf(WHERESTR "Invalidating id: %d, known objects: %d\n", WHEREARG, dataItem, allocatedItems->fill);

	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&rc_invalidate_queue_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	//kl = g_hash_table_get_keys(rc_GinvalidateSubscribers);
	toplist = kl = g_hash_table_get_values(rc_GinvalidateSubscribers);

	if (dsmcbe_host_number != GetMachineID(dataItem) && !onlySubscribers)
	{
		if(kl != NULL) {
			// Mark memory as dirty
			g_hash_table_remove(rc_GallocatedItems, (void*)dataItem);
			unsigned int* count = (unsigned int*)MALLOC(sizeof(unsigned int));
			*count = 0;
			if(g_hash_table_lookup(rc_GallocatedItemsDirty, obj) == NULL)
			{
				g_hash_table_insert(rc_GallocatedItemsDirty, obj, count);
			}
			else
				REPORT_ERROR("Could not insert into allocatedItemsDirty");
		} else {
			FREE_ALIGN(obj->EA);
			obj->EA = NULL;
			g_hash_table_remove(rc_GallocatedItems, (void*)dataItem);
			if (obj->Gwaitqueue != NULL)
				g_queue_free(obj->Gwaitqueue);
			if (obj->Gwaitqueue != NULL)
				g_queue_free(obj->GrequestCount);
			FREE(obj);
			obj = NULL;
			g_list_free(toplist);
			pthread_mutex_unlock(&rc_invalidate_queue_mutex);
			return;
		}
	}
	else
	{
		unsigned int* count = (unsigned int*)MALLOC(sizeof(unsigned int));
		*count = 0;
		if(g_hash_table_lookup(rc_GallocatedItemsDirty, obj) == NULL)
		{
			g_hash_table_insert(rc_GallocatedItemsDirty, obj, count);
		}
		else
			REPORT_ERROR("Could not insert into allocatedItemsDirty");
	}	
		
	while(kl != NULL)
	{
		struct invalidateRequest* requ;
		if ((requ = (struct invalidateRequest*)MALLOC(sizeof(struct invalidateRequest))) == NULL)
			REPORT_ERROR("MALLOC error");
		
		requ->packageCode =  PACKAGE_INVALIDATE_REQUEST;
		requ->requestID = NEXT_SEQ_NO(rc_sequence_nr, MAX_SEQUENCE_NR);
		
		if (g_hash_table_size(rc_GpendingSequenceNr) > (MAX_SEQUENCE_NR / 2))
			REPORT_ERROR("Likely problem with indvalidate requests...");

		while (g_hash_table_lookup(rc_GpendingSequenceNr, (void*)requ->requestID) != NULL && g_hash_table_size(rc_GpendingSequenceNr) < MAX_SEQUENCE_NR)
			requ->requestID = NEXT_SEQ_NO(rc_sequence_nr, MAX_SEQUENCE_NR);

		
		requ->dataItem = dataItem;
		
		sub = kl->data;
		
		
		if (g_hash_table_lookup(rc_GpendingSequenceNr, (void*)requ->requestID) == NULL)		
			g_hash_table_insert(rc_GpendingSequenceNr, (void*)requ->requestID, obj);
		else
		{
			printf(WHERESTR "Seqnr is :%d, table count is: %d\n", WHEREARG, requ->requestID, g_hash_table_size(rc_GpendingSequenceNr));
			REPORT_ERROR("Could not insert into pendingSequenceNr");
		}
		unsigned int* count = g_hash_table_lookup(rc_GallocatedItemsDirty, obj);
		(*count)++;

		//printf(WHERESTR "locking mutex\n", WHEREARG);
		pthread_mutex_lock(sub->mutex);
		//printf(WHERESTR "locked mutex\n", WHEREARG);
		
		g_queue_push_tail(*sub->Gqueue, requ);
		if (sub->event != NULL)
			pthread_cond_signal(sub->event);
		pthread_mutex_unlock(sub->mutex); 
		
		//printf(WHERESTR "Sent invalidate request sent\n", WHEREARG);
		
		kl = kl->next;
	}
	pthread_mutex_unlock(&rc_invalidate_queue_mutex);

	g_list_free(toplist);
	kl = NULL;

	//Invalidate the network?
	if (DSMCBE_MachineCount() > 1 && !onlySubscribers)
	{
		struct invalidateRequest* requ;
		if ((requ = (struct invalidateRequest*)MALLOC(sizeof(struct invalidateRequest))) == NULL)
			REPORT_ERROR("MALLOC error");
		
		requ->packageCode =  PACKAGE_INVALIDATE_REQUEST;
		requ->requestID = NEXT_SEQ_NO(rc_sequence_nr, MAX_SEQUENCE_NR);
		
		if (g_hash_table_size(rc_GpendingSequenceNr) > (MAX_SEQUENCE_NR / 2))
			REPORT_ERROR("Likely problem with indvalidate requests...");

		while (g_hash_table_lookup(rc_GpendingSequenceNr, (void*)requ->requestID) != NULL && g_hash_table_size(rc_GpendingSequenceNr) < MAX_SEQUENCE_NR)
			requ->requestID = NEXT_SEQ_NO(rc_sequence_nr, MAX_SEQUENCE_NR);

		requ->dataItem = dataItem;
		
		if (g_hash_table_lookup(rc_GpendingSequenceNr, (void*)requ->requestID) == NULL)		
			g_hash_table_insert(rc_GpendingSequenceNr, (void*)requ->requestID, obj);
		else
			REPORT_ERROR("Could not insert into pendingSequenceNr");
		unsigned int* count = g_hash_table_lookup(rc_GallocatedItemsDirty, obj);
		(*count)++;		
		
		struct QueueableItemStruct* ui;
		if ((ui = (struct QueueableItemStruct*)MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
			REPORT_ERROR("MALLOC error");
		
		ui->mutex = &rc_queue_mutex;
		ui->event = &rc_queue_ready;
		ui->Gqueue = &rc_GbagOfTasks;
		ui->callback = NULL;
		ui->dataRequest = requ;
		
		NetRequest(ui, OBJECT_TABLE_RESERVED);
	}
	//printf(WHERESTR "Invalidate request sent\n", WHEREARG);
}

void RecordBufferRequest(QueueableItem item, dataObject obj)
{
	
	QueueableItem temp;
	if ((temp = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("malloc error");
	memcpy(temp, item, sizeof(struct QueueableItemStruct));				

	if ((temp->dataRequest = MALLOC(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");
	memcpy(temp->dataRequest, item->dataRequest, sizeof(struct acquireRequest));

	if (((struct acquireRequest*)item->dataRequest)->packageCode != PACKAGE_ACQUIRE_REQUEST_WRITE)
		REPORT_ERROR("Recording buffer entry for non acquire or non write");

	//printf(WHERESTR "Inserting into writebuffer table: %d, %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem, (int)obj);
	if(g_hash_table_lookup(rc_GwritebufferReady, obj) == NULL)
	{
		//printf(WHERESTR "Inserted item into writebuffer table: %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem);
		g_hash_table_insert(rc_GwritebufferReady, obj, temp);
	}
	else
	{
		printf(WHERESTR "*EXT*\n", WHEREARG);
		REPORT_ERROR("Could not insert into writebufferReady, element exists");
	}
}

//Performs all actions releated to an acquire request
void DoAcquireBarrier(QueueableItem item, struct acquireBarrierRequest* request)
{
	GQueue* q;
	dataObject obj;

#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Acquire barrier on id %i\n", WHEREARG, request->dataItem);
#endif	
	OBJECT_TABLE_ENTRY_TYPE machineId = GetMachineID(request->dataItem);		
	
	if (machineId == OBJECT_TABLE_RESERVED)
	{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Acquire barrier on id %i is waiting\n", WHEREARG, request->dataItem);
#endif	
		if (g_hash_table_lookup(rc_Gwaiters, (void*)request->dataItem) == NULL)
			g_hash_table_insert(rc_Gwaiters, (void*)request->dataItem, g_queue_new());
		g_queue_push_tail(g_hash_table_lookup(rc_Gwaiters, (void*)request->dataItem), item);
	}
	else if (machineId != dsmcbe_host_number)
	{
		NetRequest(item, machineId);
	}
	else
	{ // && machineId != OBJECT_TABLE_RESERVED)
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Acquire barrier is local id %i\n", WHEREARG, request->dataItem);
#endif	
		
		//Check that the item exists
		if ((obj = g_hash_table_lookup(rc_GallocatedItems, (void*)request->dataItem)) != NULL)
		{
			if (obj->size < (sizeof(unsigned int) * 2))
				REPORT_ERROR("Invalid size for barrier!");
			
			//We keep a count, because g_queue_get_length itterates the queue
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
					RespondAcquireBarrier(g_queue_pop_head(q));
				
				//Also respond to the last one in, but we did not put it in the queue
				RespondAcquireBarrier(item);
			}
			else
				g_queue_push_tail(q, item);
			
		}
	}
}

int TestForMigration(QueueableItem next, dataObject obj)
{
#ifdef ENABLE_MIGRATION
	unsigned int machine_count = DSMCBE_MachineCount();
	if (machine_count > 1)
	{
		if (obj->GrequestCount == NULL)
			REPORT_ERROR("object had no requestCount queue");
		
		if (next->dataRequest == NULL)
			REPORT_ERROR("No request on item");
		if (((struct acquireRequest*)next->dataRequest)->packageCode != PACKAGE_ACQUIRE_REQUEST_WRITE)
			REPORT_ERROR("TestForMigration called with unwanted package");
		
		unsigned int* m = &((struct acquireRequest*)next->dataRequest)->originator;
		
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
			g_queue_push_tail(obj->GrequestCount, (void*)((struct acquireRequest*)next->dataRequest)->originator);
		}
		
		if (g_queue_get_length(obj->GrequestCount) > MAX_RECORDED_WRITE_REQUESTS)
		{
			g_queue_pop_head(obj->GrequestCount);
			size_t i;
			
			if (rc_request_count_buffer == NULL)
				REPORT_ERROR("Broken buffer");
				
			memset(rc_request_count_buffer, 0, sizeof(unsigned int) * machine_count);

			//TODO: It should be faster to avoid g_queue_peek_nth and use g_queue_foreach instead
			 
			//Step 1 count number of occurences
			for(i = 0; i < g_queue_get_length(obj->GrequestCount); i++)
			{
				unsigned int machine = (unsigned int)g_queue_peek_nth(obj->GrequestCount, i);
				//printf(WHERESTR "Queue i: %d, machine: %d, list: %d\n", WHEREARG, i, machine, (unsigned int)rc_request_count_buffer);
				if (machine >= machine_count) {
					REPORT_ERROR2("Machine id was too large, id: %d", machine);
				} else
					rc_request_count_buffer[machine]++;
			}
			
			//Step 2, examine hits
			for(i = 0; i < machine_count; i++)
				if (i != dsmcbe_host_number && rc_request_count_buffer[i] > MIGRATION_THRESHOLD)
				{
#ifdef DEBUG_COMMUNICATION
					printf(WHERESTR "Migration threshold exeeced for object %d by machine %d, initiating migration\n", WHEREARG, obj->id, i);
#endif
					rc_PerformMigration(next, (struct acquireRequest*)next->dataRequest, obj, i);
					DoInvalidate(obj->id, FALSE);
					return TRUE;
				}
		}
	}
#endif	
	return FALSE;
}

//Performs all actions releated to an acquire request
void DoAcquire(QueueableItem item, struct acquireRequest* request)
{
	
	GQueue* q;
	dataObject obj;

	//printf(WHERESTR "Start acquire on id %i\n", WHEREARG, request->dataItem);

	//OBJECT_TABLE_ENTRY_TYPE machineId = GetMachineID(request->dataItem);
			
	
	//Check that the item exists
	if ((obj = g_hash_table_lookup(rc_GallocatedItems, (void*)request->dataItem)) != NULL)
	{
		q = obj->Gwaitqueue;
						
		//If the object is not locked, register as locked and respond
		if (g_queue_is_empty(q))
		{
			//printf(WHERESTR "Object not locked\n", WHEREARG);
			if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
				//printf(WHERESTR "Acquiring READ on not locked object\n", WHEREARG);
				RespondAcquire(item, obj);
			} else if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE) {			
				//printf(WHERESTR "Acquiring WRITE on not locked object\n", WHEREARG);
				//if (machineId == 0 && request->dataItem == 0 && request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
					//printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, requestID %i\n", WHEREARG, request->dataItem, machineId, dsmcbe_host_number, request->requestID);


				if (TestForMigration(item, obj))
					return;
							
				g_queue_push_head(q, NULL);
				
				if (obj->id != OBJECT_TABLE_ID)
					RecordBufferRequest(item, obj);
					
				RespondAcquire(item, obj);
					
				if (obj->id != OBJECT_TABLE_ID)
				{
					DoInvalidate(obj->id, FALSE);
					//printf(WHERESTR "Sending NET invalidate for id: %d\n", WHEREARG, obj->id);
					NetInvalidate(obj->id);
				}
				else
				{
					//printf(WHERESTR "Sending NetUpdate with ID %u, Offset %u, Size %u, Data %u\n", WHEREARG, obj->id, 0, OBJECT_TABLE_SIZE, obj->EA);					
					NetUpdate(obj->id, 0, OBJECT_TABLE_SIZE, obj->EA);
				}
					
			}

		}
		else {
			//Otherwise add the request to the wait list			
			//if (machineId == 0 && request->dataItem == 0 && request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
				//printf(WHERESTR "Object locked\n", WHEREARG);
				
			g_queue_push_tail(q, item);
		}
	}
	else
	{
		//Create a list if none exists

		//if (machineId == 0 && request->dataItem == 0 && request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
			//printf(WHERESTR "acquire requested for id %d, waiting for create\n", WHEREARG, request->dataItem);


		if (g_hash_table_lookup(rc_Gwaiters, (void*)request->dataItem) == NULL)
			g_hash_table_insert(rc_Gwaiters, (void*)request->dataItem, (void*)g_queue_new());
		
		//Append the request to the waiters, for use when the object gets created
		q = g_hash_table_lookup(rc_Gwaiters, (void*)request->dataItem);
		g_queue_push_tail(q, item);		
	}
	
}

//Performs all actions releated to a release
void DoRelease(QueueableItem item, struct releaseRequest* request)
{
	
	GQueue* q;
	dataObject obj;
	QueueableItem next;
	
	//OBJECT_TABLE_ENTRY_TYPE machineId = GetMachineID(request->dataItem);
	//if (machineId == 0 && request->dataItem == 0)
		//printf(WHERESTR "Release for item %d, machineid: %d, machine id: %d, requestID %i\n", WHEREARG, request->dataItem, machineId, dsmcbe_host_number, request->requestID);
	
	//printf(WHERESTR "Performing release for %d\n", WHEREARG, request->dataItem);
	if (request->mode == ACQUIRE_MODE_READ)
	{
		//printf(WHERESTR "Performing read-release for %d\n", WHEREARG, request->dataItem);
		return;
	}
	
	//Ensure that the item exists
	if ((obj =g_hash_table_lookup(rc_GallocatedItems, (void*)request->dataItem)) != NULL)
	{
		q = obj->Gwaitqueue;
		
		//printf(WHERESTR "%d queue pointer: %d\n", WHEREARG, request->dataItem, (int)q);
		
		//Ensure that the item was actually locked
		if (g_queue_is_empty(q))
		{
			REPORT_ERROR("Bad release, item was not locked!");
			RespondNACK(item);
		}
		else
		{
			//Get the next pending request
			next = g_queue_pop_head(q);
			if (next != NULL)
			{
				REPORT_ERROR("Bad queue, the top entry was not a locker!");
				g_queue_push_head(q, next);
				RespondNACK(item);
			}
			else
			{
				//Respond to the releaser
				//printf(WHERESTR "Respond to the releaser for %d\n", WHEREARG, request->dataItem);
				RespondRelease(item);
				
				//if(g_queue_is_empty(q))
					//printf(WHERESTR "Queue is empty\n", WHEREARG);
					
				if (obj->id == OBJECT_TABLE_ID && dsmcbe_host_number == OBJECT_TABLE_OWNER)
				{	
					//ProcessWaiters(obj->EA == NULL ? g_hash_table_lookup(rc_GallocatedItems, OBJECT_TABLE_ID) : obj->EA);
					//printf(WHERESTR "Sending NetUpdate with ID %u, Offset %u, Size %u, Data %u\n", WHEREARG, obj->id, 0, OBJECT_TABLE_SIZE, obj->EA);
					NetUpdate(OBJECT_TABLE_ID, 0, request->dataSize, obj->EA);
				}
				
			
				while (!g_queue_is_empty(q))
				{
					//Acquire for the next in the queue
					//printf(WHERESTR "Acquire for the next in the queue for %d\n", WHEREARG, request->dataItem);
					next = g_queue_pop_head(q);
					if (((struct acquireRequest*)next->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE){
						//if (machineId == 0 && ((struct acquireRequest*)next->dataRequest)->dataItem == 0)
							//printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, requestID %i\n", WHEREARG, request->dataItem, machineId, dsmcbe_host_number, request->requestID);

						if (TestForMigration(next, obj))
							return;
													
						g_queue_push_head(q, NULL);

						GUID id = obj->id;
						if (id != OBJECT_TABLE_ID)
							RecordBufferRequest(next, obj);
						RespondAcquire(next, obj);

						//printf(WHERESTR "Sending NET invalidate for id: %d\n", WHEREARG, id);
						if (id != OBJECT_TABLE_ID)
						{
							DoInvalidate(id, FALSE);
							//printf(WHERESTR "Sending NET invalidate for id: %d\n", WHEREARG, obj->id);
							NetInvalidate(id);
						}
						
						break; //Done
					} else if (((struct acquireRequest*)next->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
						RespondAcquire(next, obj);						
					}
					else
						REPORT_ERROR("packageCode was neither WRITE or READ");						 
				}
			}		
		}
	}
	else
	{
		REPORT_ERROR("Tried to release a non-existing item");
		RespondNACK(item);		
	}
}

OBJECT_TABLE_ENTRY_TYPE GetMachineID(GUID id)
{
	return GetObjectTable()[id];
}

void HandleCreateRequest(QueueableItem item)
{
	struct createRequest* req = item->dataRequest;

	//printf(WHERESTR "processing create event\n", WHEREARG);
	if (OBJECT_TABLE_OWNER != dsmcbe_host_number) {
		
		//Check if we can decline the request early on
		if (GetObjectTable()[req->dataItem] != OBJECT_TABLE_RESERVED)
		{
			RespondNACK(item);
			return;
		}
		
		NetRequest(item, OBJECT_TABLE_OWNER);
#ifdef OPTIMISTIC_CREATE
		DoCreate(item, req);
#else
		if (g_hash_table_lookup(rc_GpendingCreates, (void*)req->dataItem) != NULL)
			RespondNACK(item);
		else
			g_hash_table_insert(rc_GpendingCreates, (void*)req->dataItem, item);
#endif
	} else { 
		DoCreate(item, req);
	}
}

void HandleAcquireRequest(QueueableItem item)
{
	
	struct acquireRequest* req = item->dataRequest;
	OBJECT_TABLE_ENTRY_TYPE machineId = GetMachineID(req->dataItem);
	void* obj;
	//printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, requestID %i\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number, req->requestID);
	
	if (machineId != dsmcbe_host_number && machineId != OBJECT_TABLE_RESERVED)
	{
		//printf(WHERESTR "Acquire for item %d must be handled remotely, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
		if ((obj = g_hash_table_lookup(rc_GallocatedItems, (void*)req->dataItem)) != NULL && req->packageCode == PACKAGE_ACQUIRE_REQUEST_READ)
		{
			//printf(WHERESTR "Read acquire for item %d, machineid: %d, machine id: %d, returned from local cache\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
			RespondAcquire(item, obj);
		}
		else
		{
			//printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, registering\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
			if (g_hash_table_lookup(rc_GpendingRequests, (void*)((struct acquireRequest*)item->dataRequest)->dataItem) == NULL)
			{
				//printf(WHERESTR "Creating queue for %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem);
				g_hash_table_insert(rc_GpendingRequests, (void*)((struct acquireRequest*)item->dataRequest)->dataItem, g_queue_new());
			}
			//GQueue* q = g_hash_table_lookup(rc_GpendingRequests, (void*)req->dataItem);
			//printf(WHERESTR "Queue->head: %d, Queue->tail: %d, Queue->length: %d, Queue->length 2: %d\n", WHEREARG, q->head, q->tail, q->length, g_queue_get_length(q));

			//printf(WHERESTR "Test x: %d, y: %d, z: %d, q: %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem, req->dataItem, (int)rc_GpendingRequests, (int)g_hash_table_lookup(rc_GpendingRequests, (void*)req->dataItem));
			//printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, sending remote request\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
			NetRequest(item, machineId);
			//printf(WHERESTR "Test x: %d, y: %d, z: %d, q: %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem, req->dataItem, (int)rc_GpendingRequests, (int)g_hash_table_lookup(rc_GpendingRequests, (void*)req->dataItem));
			g_queue_push_tail(g_hash_table_lookup(rc_GpendingRequests, (void*)req->dataItem), item);
		}
	}
	else if (machineId == OBJECT_TABLE_RESERVED)
	{
		//printf(WHERESTR "Acquire for non-existing item %d, registering request locally, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
		if (g_hash_table_lookup(rc_Gwaiters, (void*)req->dataItem) == NULL)
			g_hash_table_insert(rc_Gwaiters, (void*)req->dataItem, g_queue_new());
		g_queue_push_tail(g_hash_table_lookup(rc_Gwaiters, (void*)req->dataItem), item);
	}
	else 
	{
		//printf(WHERESTR "Processing acquire locally for %d, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
		DoAcquire(item, (struct acquireRequest*)item->dataRequest);
	}	
}

void HandleReleaseRequest(QueueableItem item)
{
	//printf(WHERESTR "processing release event\n", WHEREARG);	
	struct releaseRequest* req = item->dataRequest;
	OBJECT_TABLE_ENTRY_TYPE machineId = GetMachineID(req->dataItem);

	if (machineId != dsmcbe_host_number)
	{
		//printf(WHERESTR "processing release event, not owner\n", WHEREARG);
		//Keep the local copy, as it is the updated version
		/*if (req->mode == ACQUIRE_MODE_WRITE)
			DoInvalidate(req->dataItem);*/

		NetRequest(item, machineId);
		free(item->dataRequest);
	}
	else
	{
		//printf(WHERESTR "processing release event, owner\n", WHEREARG);
		if (req->mode == ACQUIRE_MODE_WRITE)
		{
			dataObject obj = g_hash_table_lookup(rc_GallocatedItems, (void*)req->dataItem);
			if (g_hash_table_lookup(rc_GallocatedItemsDirty, obj) != NULL || g_hash_table_lookup(rc_GwritebufferReady, obj) != NULL)
			{
				//printf(WHERESTR "processing release event, object %d is in use, re-registering\n", WHEREARG, obj->id);
				//The object is still in use, re-register, the last invalidate response will free it
				
				GQueue* tmp = obj->Gwaitqueue;
				GQueue* tmp2 = obj->GrequestCount;
				obj->Gwaitqueue = NULL;
				obj->GrequestCount = NULL;
				
				if (req->data == NULL)
					req->data = obj->EA;
					
				//TODO: Create and use ht_update
				g_hash_table_remove(rc_GallocatedItems, (void*)req->dataItem);
				if ((obj = MALLOC(sizeof(struct dataObjectStruct))) == NULL)
					REPORT_ERROR("malloc error");
				
				obj->EA = req->data;
				obj->id = req->dataItem;
				obj->size = req->dataSize;
				obj->Gwaitqueue = tmp;
				obj->GrequestCount = tmp2;
				
				if (g_hash_table_lookup(rc_GallocatedItems, (void*)obj->id) == NULL)
					g_hash_table_insert(rc_GallocatedItems, (void*)obj->id, obj);
				else
					REPORT_ERROR("Could not insert into allocatedItems");
			}
			else
			{
				//printf(WHERESTR "processing release event, object is not in use, updating\n", WHEREARG);
				//The object is not in use, just copy in the new version
				if (obj->EA != req->data && req->data != NULL)
				{
					//printf(WHERESTR "Req: %i, Size(req) %i, Size(obj): %i, Data(req) %i, Data(obj) %i, id: %d\n", WHEREARG, (int)req, (int)req->dataSize, (int)obj->size, (int)req->data, (int)obj->EA, obj->id);
					memcpy(obj->EA, req->data, obj->size);
					FREE_ALIGN(req->data);
					req->data = NULL;				
				}
			}
		}
		
		//printf(WHERESTR "processing release event, owner\n", WHEREARG);
		DoRelease(item, (struct releaseRequest*)item->dataRequest);
	}
	//printf(WHERESTR "processed release event\n", WHEREARG);
	
}

void HandleInvalidateRequest(QueueableItem item)
{
	
	struct invalidateRequest* req = item->dataRequest;
	
	//printf(WHERESTR "processing network invalidate request for: %d\n", WHEREARG, req->dataItem);
	DoInvalidate(req->dataItem, FALSE);
	
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item = NULL;
}

void HandleUpdateRequest(QueueableItem item)
{
	struct updateRequest* req = item->dataRequest;
	//printf(WHERESTR "Processing update\n", WHEREARG);
	
	if (dsmcbe_host_number != OBJECT_TABLE_OWNER && req->dataItem == OBJECT_TABLE_ID)	
	{
		//printf(WHERESTR "Getting ObjectTable\n", WHEREARG);
		memcpy(((void*)GetObjectTable()) + req->offset, req->data, req->dataSize);
		unsigned int id = req->offset / sizeof(OBJECT_TABLE_ENTRY_TYPE);
#ifndef OPTIMISTIC_CREATE
		if (req->dataSize == sizeof(OBJECT_TABLE_ENTRY_TYPE))
		{
			unsigned int id = req->offset / sizeof(OBJECT_TABLE_ENTRY_TYPE);
			//printf(WHERESTR "Object %d was created\n", WHEREARG, id);
			QueueableItem item = g_hash_table_lookup(rc_GpendingCreates, (void*)id);
			if (item != NULL)
			{
				g_hash_table_remove(rc_GpendingCreates, (void*)req->dataItem);
				DoCreate(item, item->dataRequest);
			}

			GQueue* dq = g_hash_table_lookup(rc_Gwaiters, (void*)id);
			while(dq != NULL && !g_queue_is_empty(dq))
			{
				//printf(WHERESTR "processed a waiter for %d (%d)\n", WHEREARG, object->id, (unsigned int)g_queue_peek_head(dq));
				g_queue_push_tail(rc_GbagOfTasks, g_queue_pop_head(dq));
			}
			
		}
		else
		{
			REPORT_ERROR("Grouped updates are not tested!");
			
			GHashTableIter iter;
			gpointer key, value;
			OBJECT_TABLE_ENTRY_TYPE* objectTable = GetObjectTable();
	
			g_hash_table_iter_init (&iter, rc_GpendingCreates);
			while (g_hash_table_iter_next (&iter, &key, &value))
				if (objectTable[(GUID)key] != OBJECT_TABLE_RESERVED)
				{
					g_hash_table_steal(rc_GpendingCreates, key);
					DoCreate((QueueableItem)value, ((QueueableItem)value)->dataRequest);
				} 

			
			ProcessWaiters(GetObjectTable());
		}
#else
		GQueue* dq = g_hash_table_lookup(rc_Gwaiters, (void*)id);
		while(dq != NULL && !g_queue_is_empty(dq))
		{
			//printf(WHERESTR "processed a waiter for %d (%d)\n", WHEREARG, id, (unsigned int)g_queue_peek_head(dq));
			g_queue_push_tail(rc_GbagOfTasks, g_queue_pop_head(dq));
		}
#endif			
		//printf(WHERESTR "Done\n", WHEREARG);
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

void HandleInvalidateResponse(QueueableItem item)
{
	
	dataObject object;
	struct invalidateResponse* req = item->dataRequest;
	
	//printf(WHERESTR "processing invalidate response for\n", WHEREARG);
	if ((object = g_hash_table_lookup(rc_GpendingSequenceNr, (void*)req->requestID)) == NULL)
	{
		FREE(item->dataRequest);
		item->dataRequest = NULL;
		FREE(item);
		item  = NULL;

		REPORT_ERROR("Incoming invalidate response did not match a pending request");
		return;
	}

	//printf(WHERESTR "processing invalidate response for %d\n", WHEREARG, object->id);
	
	if (g_hash_table_lookup(rc_GallocatedItemsDirty, (void*)object) == NULL)
	{
		g_hash_table_remove(rc_GpendingSequenceNr, (void*)req->requestID);
		FREE(item->dataRequest);
		item->dataRequest = NULL;
		FREE(item);
		item  = NULL;
				
		REPORT_ERROR("Incoming invalidate response was found, but there was no object for it");
		return;
	}
	
	unsigned int* count = g_hash_table_lookup(rc_GallocatedItemsDirty, object);
	*count = *count - 1;
	if (*count <= 0) {
		g_hash_table_remove(rc_GallocatedItemsDirty, (void*)object);
	
		FREE(count);
		count = NULL;
		
		QueueableItem reciever = NULL;
		
		if((reciever = g_hash_table_lookup(rc_GwritebufferReady, object)) != NULL) {
			//printf(WHERESTR "The last response is in for: %d, sending writebuffer signal, %d\n", WHEREARG, object->id, (int)object);
		
			struct writebufferReady* invReq = (struct writebufferReady*)MALLOC(sizeof(struct writebufferReady));
			if (invReq == NULL)
				REPORT_ERROR("malloc error");
			
			g_hash_table_remove(rc_GwritebufferReady, object);
					
			invReq->packageCode = PACKAGE_WRITEBUFFER_READY;
			invReq->requestID = ((struct acquireRequest*)reciever->dataRequest)->requestID;
			invReq->dataItem = object->id;
		
			//printf(WHERESTR "Sending package code: %d\n", WHEREARG, invReq->packageCode);
			RespondAny(reciever, invReq);
		}
		else
		{
			//This happens when the invalidate comes from the network, or the acquireResponse has new data
			//printf(WHERESTR "Last invalidate was in for: %d, EA: %d, but there was not a recipient?\n", WHEREARG, object->id, (int)object);
		}

		dataObject temp = g_hash_table_lookup(rc_GallocatedItems, (void*)object->id);
		
		if (temp == NULL || temp != object)
		{  		
			//printf(WHERESTR "Special case: Why is object not in allocatedItemsDirty or allocedItems?\n", WHEREARG);
			//printf(WHERESTR "Item is no longer required, freeing: %d (%d,%d)\n", WHEREARG, object->id, (int)object, (int)object->EA);
			
			//Make sure we are not still using the actual memory
			if (temp == NULL || temp->EA != object->EA)
				FREE_ALIGN(object->EA);
			object->EA = NULL;
			//Release all control structures on the object
			if (object->Gwaitqueue != NULL)
				g_queue_free(object->Gwaitqueue);
			if (object->Gwaitqueue != NULL)
				g_queue_free(object->GrequestCount);
			FREE(object);
			object = NULL;
		}
	}
	else
	{
		//printf(WHERESTR "Count was: %d, %d\n", WHEREARG, *count, (int)object);
	}

	//printf(WHERESTR "removing pending invalidate response\n", WHEREARG);
	g_hash_table_remove(rc_GpendingSequenceNr, (void*)req->requestID);
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item  = NULL;
	//printf(WHERESTR "processing invalidate response for\n", WHEREARG);	
}

void SingleInvalidate(QueueableItem item, GUID id)
{
	struct invalidateRequest* req;
	if ((req = MALLOC(sizeof(struct invalidateRequest))) == NULL)
		REPORT_ERROR("malloc error");
		
	req->packageCode = PACKAGE_INVALIDATE_REQUEST;
	req->dataItem = id;
	req->requestID = 0;
	
	if (item->mutex != NULL)
	{
		//printf(WHERESTR "locking mutex\n", WHEREARG);
		pthread_mutex_lock(item->mutex);
		//printf(WHERESTR "locked mutex\n", WHEREARG);
	}
	g_queue_push_tail(*(item->Gqueue), req);
	
	if (item->event != NULL)
		pthread_cond_signal(item->event);
		
	if (item->mutex != NULL)
		pthread_mutex_unlock(item->mutex);
}

void HandleAcquireResponse(QueueableItem item)
{
	
	struct acquireResponse* req = item->dataRequest;
	dataObject object;

	//printf(WHERESTR "processing acquire response event for %d, reqId: %d\n", WHEREARG, req->dataItem, req->requestID);
	
	if (req->dataSize == 0 || (dsmcbe_host_number == OBJECT_TABLE_OWNER && req->dataItem == OBJECT_TABLE_ID))
	{
		if ((object = g_hash_table_lookup(rc_GallocatedItems, (void*)req->dataItem)) == NULL)
			REPORT_ERROR("Requester had sent response without data for non-existing local object");
			
		//printf(WHERESTR "acquire response had local copy, id: %d, size: %d\n", WHEREARG, req->dataItem, (int)req->dataSize);
		//printf(WHERESTR "acquire response had local copy, EA: %d, size: %d\n", WHEREARG, (int)object->EA, (int)object->size);
		
		//Copy back the object table on the object table owner
		if (req->dataSize != 0 && req->data != NULL && object->EA != NULL && req->data != object->EA)
			memcpy(object->EA, req->data, req->dataSize);
			
		req->dataSize = object->size;
		req->data = object->EA;
		
		//If data is not changed, there is no need to invalidate locally
		/*if (req->mode != ACQUIRE_MODE_READ && (dsmcbe_host_number != OBJECT_TABLE_OWNER || req->dataItem != OBJECT_TABLE_ID))
			DoInvalidate(object->id, TRUE); */
	}
	else
	{
		//printf(WHERESTR "registering item locally\n", WHEREARG);

		if ((object = (dataObject)MALLOC(sizeof(struct dataObjectStruct))) == NULL)
			REPORT_ERROR("MALLOC error");
		
		if (req->data == NULL)
			REPORT_ERROR("Acquire response had empty data");	
		object->id = req->dataItem;
		object->EA = req->data;
		object->size = req->dataSize;
		object->Gwaitqueue = NULL;
		object->GrequestCount = g_queue_new();

		/*if (req->dataItem == OBJECT_TABLE_ID)
			printf(WHERESTR "objecttable entry 1 = %d\n", WHEREARG, ((OBJECT_TABLE_ENTRY_TYPE*)object->EA)[1]);*/

						
		if(g_hash_table_lookup(rc_GallocatedItems, (void*)object->id) == NULL)
			g_hash_table_insert(rc_GallocatedItems, (void*)object->id, object);
		else
		{
			DoInvalidate(object->id, FALSE);
			if (g_hash_table_lookup(rc_GallocatedItems, (void*)object->id) == NULL)
				g_hash_table_insert(rc_GallocatedItems, (void*)object->id, object);
			else
				REPORT_ERROR("Could not insert into allocatedItems");
		}
	}

	//printf(WHERESTR "testing local copy, obj: %d, %d, %d\n", WHEREARG, (int)object, (int)waiters, object->id);
	
	//If the response is a objecttable acquire, check if items have been created, that we are awaiting 
	if (object->id == OBJECT_TABLE_ID)
	{
		ProcessWaiters(object->EA);
	}

	GQueue* dq = NULL;
	//If this is an acquire for an object we requested, release the waiters
	char isLocked = 1;
	if ((dq = g_hash_table_lookup(rc_GpendingRequests, (void*)object->id)) != NULL)
	{
		//printf(WHERESTR "testing local copy\n", WHEREARG);

		//printf(WHERESTR "locking mutex\n", WHEREARG);
		pthread_mutex_lock(&rc_queue_mutex);		
		//printf(WHERESTR "locked mutex\n", WHEREARG);
		
		while(!g_queue_is_empty(dq))
		{
			//printf(WHERESTR "processed a waiter for %d\n", WHEREARG, object->id);
			QueueableItem q = (QueueableItem)g_queue_pop_head(dq);
			//printf(WHERESTR "waiter package type: %d, reqId: %d, mode: %d\n", WHEREARG, ((struct createRequest*)q->dataRequest)->packageCode, ((struct createRequest*)q->dataRequest)->requestID, req->mode);
			if (((struct createRequest*)q->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE)
			{
				if (req->mode != ACQUIRE_MODE_READ)
				{
					//printf(WHERESTR "Sending AcquireResponse\n", WHEREARG);
					//printf(WHERESTR "unlocking mutex\n", WHEREARG);
					pthread_mutex_unlock(&rc_queue_mutex);
					isLocked = 0;
					//printf(WHERESTR "unlocked mutex\n", WHEREARG);
					RecordBufferRequest(q, object);
						
					RespondAcquire(q, g_hash_table_lookup(rc_GallocatedItems, (void*)object->id));
					DoInvalidate(object->id, TRUE);
					//SingleInvalidate(q, object->id);
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
				//printf(WHERESTR "Inserting: %d\n", WHEREARG, (unsigned int)q);
				g_queue_push_tail(rc_GbagOfTasks, q);
			}
		}
		if (isLocked)
		{
			//printf(WHERESTR "unlocking mutex\n", WHEREARG);
			pthread_mutex_unlock(&rc_queue_mutex);
			//printf(WHERESTR "unlocked mutex\n", WHEREARG);
		}
		//printf(WHERESTR "unlocking mutex\n", WHEREARG);
		if (g_queue_is_empty(dq))
		{
			g_hash_table_remove(rc_GpendingRequests, (void*)object->id);
			g_queue_free(dq);
			dq = NULL;
		}
		
	}

	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);				
	item = NULL;
	//printf(WHERESTR "Handled acquire response\n", WHEREARG);
}

void HandleAcquireBarrierResponse(QueueableItem item)
{
	//printf(WHERESTR "Handling barrier response\n", WHEREARG);
	
	struct acquireBarrierResponse* resp;
	
	if((resp = MALLOC(sizeof(struct acquireBarrierResponse))) == NULL)
		REPORT_ERROR("malloc error");
		
	resp->packageCode = ((struct acquireBarrierResponse*)item->dataRequest)->packageCode;
	resp->requestID = ((struct acquireBarrierResponse*)item->dataRequest)->requestID;
	resp->originator = ((struct acquireBarrierResponse*)item->dataRequest)->originator;
	resp->originalRequestID = ((struct acquireBarrierResponse*)item->dataRequest)->originalRequestID;
	resp->originalRecipient = ((struct acquireBarrierResponse*)item->dataRequest)->originalRecipient;
	
	
	RespondAny(item, resp);
}

//Handles an incoming migrationResponse
void rc_HandleMigrationResponse(QueueableItem item, struct migrationResponse* resp)
{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Recieving migration for object %d from %d\n", WHEREARG, resp->dataItem, resp->originator);
#endif
	
	//Clear any local copies first
	DoInvalidate(resp->dataItem, FALSE);

	//Make sure we are the new owner (the update is delayed)
	GetObjectTable()[resp->dataItem] = dsmcbe_host_number;

	//Pretend the package was an acquire response
	struct acquireResponse* aresp;
	if ((aresp = malloc(sizeof(struct acquireResponse))) == NULL)
		REPORT_ERROR("malloc error");
	
	//Copy all fields into the new structure	
	aresp->packageCode = PACKAGE_ACQUIRE_RESPONSE;
	aresp->data = resp->data;
	aresp->dataItem = resp->dataItem;
	aresp->dataSize = resp->dataSize;
	aresp->mode = resp->mode;
	aresp->originalRecipient = resp->originalRecipient;
	aresp->originalRequestID = resp->originalRequestID;
	aresp->originator = resp->originator;
	aresp->requestID = resp->requestID;
	item->dataRequest = aresp;

	//printf(WHERESTR "Mapping Acquire Response object %d from %d\n", WHEREARG, resp->dataItem, resp->originator);
	HandleAcquireResponse(item);

	//printf(WHERESTR "Migration complete %d from %d\n", WHEREARG, resp->dataItem, resp->originator);

	//HandleAcquireResponse assumes that the object is owned by another machine
	dataObject obj = g_hash_table_lookup(rc_GallocatedItems, (void*)resp->dataItem);
	if (obj == NULL)
		REPORT_ERROR("Recieved item, but it did not exist?")
	
	if (obj->Gwaitqueue == NULL)
		obj->Gwaitqueue = g_queue_new();
	if (obj->GrequestCount == NULL)
		obj->GrequestCount = g_queue_new();
		
	//Since the request is not registered, the object does not appear locked but it is
	g_queue_push_head(obj->Gwaitqueue, NULL); 
	free(resp);

}

void rc_PerformMigration(QueueableItem item, struct acquireRequest* req, dataObject obj, OBJECT_TABLE_ENTRY_TYPE owner)
{
#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Performing actual migration on object %d to %d\n", WHEREARG, obj->id, owner);
#endif

	//Update local table
	GetObjectTable()[obj->id] = owner;
	
	struct migrationResponse* resp;
	if ((resp = malloc(sizeof(struct migrationResponse))) == NULL)
		REPORT_ERROR("malloc error");
	
	resp->packageCode = PACKAGE_MIGRATION_RESPONSE;
	resp->data = obj->EA;
	resp->dataItem = obj->id;
	resp->dataSize = obj->size;
	resp->mode = req->packageCode == PACKAGE_ACQUIRE_REQUEST_READ ? ACQUIRE_MODE_READ : ACQUIRE_MODE_WRITE;
	
	resp->originalRecipient = req->originalRecipient;
	resp->originalRequestID = req->originalRequestID;
	resp->originator = req->originator;
	
	RespondAny(item, resp);
	
	//Forward all requests to the new owner
	while(!g_queue_is_empty(obj->Gwaitqueue))
	{
		QueueableItem tmp = g_queue_pop_head(obj->Gwaitqueue);
		NetRequest(item, owner);
		free(item->dataRequest);
		free(item);
	}
	
	//Notify others about the new owner, 
	//any package in-transit will be forwarded by this machine to the new owner
	NetUpdate(OBJECT_TABLE_ID, sizeof(OBJECT_TABLE_ENTRY_TYPE) * obj->id, sizeof(OBJECT_TABLE_ENTRY_TYPE), &(GetObjectTable()[obj->id]));
}

//This is the main thread function
void* rc_ProccessWork(void* data)
{
	
	QueueableItem item;
	unsigned int datatype;
	
	while(!terminate)
	{
		//Get the next item, or sleep until it arrives	
		//printf(WHERESTR "fetching job\n", WHEREARG);
			
		//printf(WHERESTR "locking mutex\n", WHEREARG);
		pthread_mutex_lock(&rc_queue_mutex);
		//printf(WHERESTR "locked mutex\n", WHEREARG);
		while (g_queue_is_empty(rc_GbagOfTasks) && g_queue_is_empty(rc_GpriorityResponses) && !terminate) {
			//printf(WHERESTR "waiting for event\n", WHEREARG);
			pthread_cond_wait(&rc_queue_ready, &rc_queue_mutex);
			//printf(WHERESTR "event recieved\n", WHEREARG);
		}
		
		if (terminate)
		{
			//printf(WHERESTR "unlocking mutex\n", WHEREARG);
			pthread_mutex_unlock(&rc_queue_mutex);
			//printf(WHERESTR "unlocked mutex\n", WHEREARG);
			break;
		}
		
		//We prioritize object table responses
		if (!g_queue_is_empty(rc_GpriorityResponses))
		{
			//printf(WHERESTR "fetching priority response\n", WHEREARG);
			if ((item = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
				REPORT_ERROR("MALLOC error");
			item->dataRequest = g_queue_pop_head(rc_GpriorityResponses);
			item->event = &rc_queue_ready;
			item->mutex = &rc_queue_mutex;
			item->Gqueue = &rc_GpriorityResponses;
			item->callback = NULL;
		}
		else
		{
			//printf(WHERESTR "fetching actual job\n", WHEREARG);
			item = (QueueableItem)g_queue_pop_head(rc_GbagOfTasks);
			if (item == NULL)
			{
				pthread_mutex_unlock(&rc_queue_mutex);
				REPORT_ERROR("Empty entry in request queue");
				continue;
			}
			if (item->dataRequest == NULL)
			{
				pthread_mutex_unlock(&rc_queue_mutex);
				REPORT_ERROR2("Empty request in queued item (%d)", (unsigned int)item)
				free(item);
				continue;
			}
		}
			
		//printf(WHERESTR "unlocking mutex\n", WHEREARG);
		pthread_mutex_unlock(&rc_queue_mutex);
		//printf(WHERESTR "unlocked mutex\n", WHEREARG);
		//Get the type of the package and perform the corresponding action
		//printf(WHERESTR "fetching event\n", WHEREARG);
		datatype = ((struct acquireRequest*)item->dataRequest)->packageCode;

		//printf(WHERESTR "processing package type: %s (%d)\n", WHEREARG, PACKAGE_NAME(datatype), datatype);
		
#ifdef DEBUG_PACKAGES
		printf(WHERESTR "processing type %s (%d), reqId: %d, possible id: %d\n", WHEREARG, PACKAGE_NAME(datatype), datatype, ((struct acquireRequest*)item->dataRequest)->requestID, ((struct acquireRequest*)item->dataRequest)->dataItem);
#endif		
		//printf(WHERESTR "processing type %d\n", WHEREARG, datatype);
		switch(datatype)
		{
			case PACKAGE_CREATE_REQUEST:
				HandleCreateRequest(item);
				break;
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				HandleAcquireRequest(item);
				break;
			case PACKAGE_RELEASE_REQUEST:
				HandleReleaseRequest(item);
				break;
			case PACKAGE_INVALIDATE_REQUEST:
				HandleInvalidateRequest(item);
				break;
			case PACKAGE_UPDATE:
				HandleUpdateRequest(item);
				break;
			case PACKAGE_INVALIDATE_RESPONSE:
				HandleInvalidateResponse(item);				
				break;
			case PACKAGE_ACQUIRE_RESPONSE:
				HandleAcquireResponse(item);
				break;
			case PACKAGE_ACQUIRE_BARRIER_REQUEST:
				DoAcquireBarrier(item, (struct acquireBarrierRequest*)item->dataRequest);
				break;
			case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
				HandleAcquireBarrierResponse(item);
				break;
			case PACKAGE_MIGRATION_RESPONSE:
				rc_HandleMigrationResponse(item, (struct migrationResponse*)item->dataRequest);
				break;
			default:
				printf(WHERESTR "Unknown package code: %i\n", WHEREARG, datatype);
				REPORT_ERROR("Unknown package recieved");
				RespondNACK(item);
		};	
		
		//All responses ensure that the QueueableItem and request structures are free'd
		//It is the obligation of the requestor to free the response
	}
	
	//Returning the unused argument removes a warning
	return data;
}

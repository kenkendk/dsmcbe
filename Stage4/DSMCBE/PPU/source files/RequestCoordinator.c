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
#include "../header files/RequestCoordinator.h"
#include "../header files/NetworkHandler.h"

#include "../../common/debug.h"

volatile int terminate;

pthread_mutex_t invalidate_queue_mutex;
pthread_mutex_t queue_mutex;
pthread_cond_t queue_ready;
queue bagOfTasks = NULL;
pthread_t workthread;

#define MAX_SEQUENCE_NR 1000000
unsigned int sequence_nr;

hashtable allocatedItems;
hashtable allocatedItemsDirty;
hashtable pendingSequenceNr;
hashtable waiters;
hashtable writebufferReady;

queue pagetableWaiters;
queue priorityResponses;

typedef struct dataObjectStruct *dataObject;

//This structure contains information about the registered objects
struct dataObjectStruct{
	
	GUID id;
	void* EA;
	unsigned long size;
	dqueue waitqueue;
};

typedef struct invalidateSubscriber* invalidateSubscriber;

//This structure is used to keep track of invalidate subscribers
struct invalidateSubscriber
{
	pthread_mutex_t* mutex;
	queue queue;
};

//This list contains all current invalidate subscribers;
slset invalidateSubscribers;


//This is used for sorting inside the hashtables
int lessint(void* a, void* b){
	return ((int)a) < ((int)b);
}

//This is a simple textbook hashing algorithm
int hashfc(void* a, unsigned int count){
	return ((int)a % count);
}

//This is the method the thread runs
void* ProccessWork(void* data);

//This cleans out all pending invalidates for the PPU handler
void processInvalidates();

//Add another subscriber to the list
void RegisterInvalidateSubscriber(pthread_mutex_t* mutex, queue* q)
{
	pthread_mutex_lock(&invalidate_queue_mutex);
	slset_insert(invalidateSubscribers, q, mutex);
	pthread_mutex_unlock(&invalidate_queue_mutex);
}

//Remove a subscriber from the list
void UnregisterInvalidateSubscriber(queue* q)
{
	pthread_mutex_lock(&invalidate_queue_mutex);
	slset_delete(invalidateSubscribers, q);
	pthread_mutex_unlock(&invalidate_queue_mutex);
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
	 	pthread_mutex_lock(&queue_mutex);
	 	queueEmpty = queue_empty(bagOfTasks);
	 	if (queueEmpty)
	 	{
	 		terminate = 1;
	 		pthread_cond_signal(&queue_ready);
	 	}
	 	pthread_mutex_unlock(&queue_mutex);
	}
	
	if (pagetableWaiters != NULL)
		queue_destroy(pagetableWaiters);
	queue_destroy(bagOfTasks);
	queue_destroy(priorityResponses);
	ht_destroy(allocatedItems);
	ht_destroy(allocatedItemsDirty);
	ht_destroy(waiters);
	
	pthread_join(workthread, NULL);
	
	pthread_mutex_destroy(&queue_mutex);
	pthread_cond_destroy(&queue_ready);
}

//This method initializes all items related to the coordinator and starts the handler thread
void InitializeCoordinator()
{
	pthread_attr_t attr;
	dataObject obj;
	size_t i;

	if (bagOfTasks == NULL)
	{
		bagOfTasks = queue_create();
		terminate = 0;
	
		/* Initialize mutex and condition variable objects */
		pthread_mutex_init(&queue_mutex, NULL);
		pthread_cond_init (&queue_ready, NULL);

		/* For portability, explicitly create threads in a joinable state */
		allocatedItems = ht_create(10, lessint, hashfc);
		allocatedItemsDirty = ht_create(10, lessint, hashfc);
		pendingSequenceNr = ht_create(10, lessint, hashfc);
		waiters = ht_create(10, lessint, hashfc);
		writebufferReady = ht_create(10, lessint, hashfc);
		pagetableWaiters = NULL;
		invalidateSubscribers = slset_create(lessint);
		priorityResponses = queue_create();
		
		if (dsmcbe_host_number == PAGE_TABLE_OWNER)
		{
			if ((obj = MALLOC(sizeof(struct dataObjectStruct))) == NULL)
				REPORT_ERROR("MALLOC error");
				
			obj->size = sizeof(unsigned int) * 10000;
			obj->EA = MALLOC_ALIGN(obj->size, 7);
			obj->id = PAGE_TABLE_ID;
			obj->waitqueue = dq_create();
			
			for(i = 0; i < 10000; i++)
				((unsigned int*)obj->EA)[i] = UINT_MAX;

			((unsigned int*)obj->EA)[PAGE_TABLE_ID] = PAGE_TABLE_OWNER; 
			ht_insert(allocatedItems, (void*)obj->id, obj);
		}

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_create(&workthread, &attr, ProccessWork, NULL);
		pthread_attr_destroy(&attr);
	
	}
}

//This method can be called from outside the module to set up a request
void EnqueItem(QueueableItem item)
{
	//printf(WHERESTR "adding item to queue: %i\n", WHEREARG, (int)item);
 	pthread_mutex_lock(&queue_mutex);
 	
 	queue_enq(bagOfTasks, (void*)item);
	//printf(WHERESTR "setting event\n", WHEREARG);
 	
 	pthread_cond_signal(&queue_ready);
 	pthread_mutex_unlock(&queue_mutex);

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
	
 	pthread_mutex_lock(&queue_mutex);
 	
 	queue_enq(priorityResponses, resp);
	//printf(WHERESTR "setting event\n", WHEREARG);
 	
 	pthread_cond_signal(&queue_ready);
 	pthread_mutex_unlock(&queue_mutex);
	
}

//Helper method with common code for responding
//It sets the requestID on the response, and frees the data structures
void RespondAny(QueueableItem item, void* resp)
{
	//printf(WHERESTR "responding to %i\n", WHEREARG, (int)item);
	//The actual type is not important, since the first two fields are 
	// layed out the same way for all packages
	((struct acquireResponse*)resp)->requestID = ((struct acquireRequest*)item->dataRequest)->requestID;

	//printf(WHERESTR "responding, locking %i\n", WHEREARG, (int)item->mutex);
	if (item->mutex != NULL)
		pthread_mutex_lock(item->mutex);
	//printf(WHERESTR "responding, locked %i\n", WHEREARG, (int)item->queue);
	
	if (item->queue != NULL)
		queue_enq(*(item->queue), resp);
		
	if (item->event != NULL)
		pthread_cond_signal(item->event);
	//printf(WHERESTR "responding, signalled %i\n", WHEREARG, (int)item->event);
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
		fprintf(stderr, WHERESTR "RequestCoordinator.c: MALLOC error\n", WHEREARG);
			
	resp->packageCode = PACKAGE_NACK;
	resp->hint = 0;
	
	RespondAny(item, resp);
}

//Responds to an acquire request
void RespondAcquire(QueueableItem item, dataObject obj)
{
	struct acquireResponse* resp;
	if ((resp = (struct acquireResponse*)MALLOC(sizeof(struct acquireResponse))) == NULL)
		REPORT_ERROR("MALLOC error");

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
	//printf(WHERESTR "Performing Acquire, mode: %d (code: %d)\n", WHEREARG, resp->mode, ((struct acquireRequest*)item->dataRequest)->packageCode);

	RespondAny(item, resp);	
}

//Responds to a release request
void RespondRelease(QueueableItem item)
{
	struct releaseResponse* resp;
	if ((resp = (struct releaseResponse*)MALLOC(sizeof(struct releaseResponse))) == NULL)
		fprintf(stderr, WHERESTR "RequestCoordinator.c: MALLOC error\n", WHEREARG);
	
	resp->packageCode = PACKAGE_RELEASE_RESPONSE;

	RespondAny(item, resp);	
}

//Performs all actions releated to a create request
void DoCreate(QueueableItem item, struct createRequest* request)
{
	unsigned long size;
	unsigned int transfersize;
	void* data;
	dataObject object;
	
	//Check that the item is not already created
	if (ht_member(allocatedItems, (void*)request->dataItem))
	{
		REPORT_ERROR("Create request for already existing item");
		RespondNACK(item);
		return;
	}

		
	size = request->dataSize;
	transfersize = size + ((16 - size) % 16);
	if (transfersize == 0)
		transfersize = 16;
		
	data = MALLOC_ALIGN(transfersize, 7);
	if (data == NULL)
	{
		REPORT_ERROR("Failed to allocate buffer for create");
		RespondNACK(item);
		return;
	}
	
	// Make datastructures for later use
	if ((object = (dataObject)MALLOC(sizeof(struct dataObjectStruct))) == NULL)
		REPORT_ERROR("MALLOC error");
	
	object->id = request->dataItem;
	object->EA = data;
	object->size = size;

	//If there are pending acquires, add them to the list
	if (ht_member(waiters, (void*)object->id))
	{
		object->waitqueue = ht_get(waiters, (void*)object->id);
		ht_delete(waiters, (void*)object->id);
	}
	else
		object->waitqueue = dq_create();
		
	//Acquire the item for the creator
	dq_enq_front(object->waitqueue, NULL);
	
	//Register this item as created
	ht_insert(allocatedItems, (void*)object->id, object);
	
	//Notify the requestor 
	RespondAcquire(item, object);	
}

//Perform all actions related to an invalidate
void DoInvalidate(GUID dataItem)
{
	keylist kl;
	dataObject obj;

	printf(WHERESTR "Invalidating...\n", WHEREARG);
	
	if (dataItem == PAGE_TABLE_ID && dsmcbe_host_number == PAGE_TABLE_OWNER)
		return;
	
	if (!ht_member(allocatedItems, (void*)dataItem))
	{
		REPORT_ERROR("Attempted to invalidate an item that was not registered");
		return;
	}
	obj = ht_get(allocatedItems, (void*)dataItem);
	
	if (dsmcbe_host_number != GetMachineID(dataItem))
	{
		if(kl != NULL) {
			// Mark memory as dirty
			ht_delete(allocatedItems, (void*)dataItem);
			unsigned int* count = (unsigned int*)MALLOC(sizeof(unsigned int));
			*count = 0;
			ht_insert(allocatedItemsDirty, obj, count);
		} else {
			FREE_ALIGN(obj->EA);
			obj->EA = NULL;
			ht_delete(allocatedItems, (void*)dataItem);
			FREE(obj);
			obj = NULL;
			return;
		}
	}
	else
	{
		unsigned int* count = (unsigned int*)MALLOC(sizeof(unsigned int));
		*count = 0;
		ht_insert(allocatedItemsDirty, obj, count);
	}	
	
	
	pthread_mutex_lock(&invalidate_queue_mutex);
	kl = invalidateSubscribers->elements;
		
	while(kl != NULL)
	{
		struct invalidateRequest* requ;
		if ((requ = (struct invalidateRequest*)MALLOC(sizeof(struct invalidateRequest))) == NULL)
			REPORT_ERROR("MALLOC error");
		
		requ->packageCode =  PACKAGE_INVALIDATE_REQUEST;
		requ->requestID = NEXT_SEQ_NO(sequence_nr, MAX_SEQUENCE_NR);
		requ->dataItem = dataItem;
		
		ht_insert(pendingSequenceNr, (void*)requ->requestID, obj);
		unsigned int* count = ht_get(allocatedItemsDirty, obj);
		*count = *count + 1;

		pthread_mutex_lock((pthread_mutex_t*)kl->data);
		queue_enq(*((queue*)kl->key), requ); 
		pthread_mutex_unlock((pthread_mutex_t*)kl->data); 
		
		printf(WHERESTR "Sent invalidate request sent\n", WHEREARG);
		
		kl = kl->next;
	}
	pthread_mutex_unlock(&invalidate_queue_mutex);

	//Clean out the PPU cache
	processInvalidates();

	printf(WHERESTR "Invalidate request sent\n", WHEREARG);
}

void RecordBufferRequest(QueueableItem item)
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

	if(!ht_member(writebufferReady, (void*)((struct acquireRequest*)item->dataRequest)->dataItem))
	{
		printf(WHERESTR "Inserted item into writebuffer table: %d\n", WHEREARG, ((struct acquireRequest*)item->dataRequest)->dataItem);
		ht_insert(writebufferReady, (void*)((struct acquireRequest*)item->dataRequest)->dataItem ,temp);
	}
	else
		REPORT_ERROR("Could not insert into writebufferReady, element exists");
}

//Performs all actions releated to an acquire request
void DoAcquire(QueueableItem item, struct acquireRequest* request)
{
	dqueue q;
	dataObject obj;
	
	//Check that the item exists
	if (ht_member(allocatedItems, (void*)request->dataItem))
	{
		obj = ht_get(allocatedItems, (void*)request->dataItem);
		q = obj->waitqueue;
						
		//If the object is not locked, register as locked and respond
		if (dq_empty(q))
		{
			printf(WHERESTR "Object not locked\n", WHEREARG);
			if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
				printf(WHERESTR "Acquiring READ on not locked object\n", WHEREARG);
				RespondAcquire(item, obj);
			} else if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE) {			
				printf(WHERESTR "Acquiring WRITE on not locked object\n", WHEREARG);
				dq_enq_front(q, NULL);
				
				RecordBufferRequest(item);
				RespondAcquire(item, obj);
					
				DoInvalidate(obj->id);
				NetInvalidate(obj->id);
			}

		}
		else {
			//Otherwise add the request to the wait list
			printf(WHERESTR "Object locked\n", WHEREARG);
			dq_enq_back(q, item);
		}
	}
	else
	{
		printf(WHERESTR "acquire requested for id %d, waiting for create\n", WHEREARG, request->dataItem);
		//Create a list if none exists
		if (!ht_member(waiters, (void*)request->dataItem))
			ht_insert(waiters, (void*)request->dataItem, dq_create());
		
		//Append the request to the waiters, for use when the object gets created
		q = (dqueue)ht_get(waiters, (void*)request->dataItem);
		dq_enq_back(q, item);		
	}
	
}

//Performs all actions releated to a release
void DoRelease(QueueableItem item, struct releaseRequest* request)
{
	dqueue q;
	dataObject obj;
	QueueableItem next;
	
	//printf(WHERESTR "Performing release for %d\n", WHEREARG, request->dataItem);
	if (request->mode == ACQUIRE_MODE_READ)
	{
		//printf(WHERESTR "Performing read-release for %d\n", WHEREARG, request->dataItem);
		return;
	}
	
	//Ensure that the item exists
	if (ht_member(allocatedItems, (void*)request->dataItem))
	{
		obj = ht_get(allocatedItems, (void*)request->dataItem);
		q = obj->waitqueue;
		
		//printf(WHERESTR "%d queue pointer: %d\n", WHEREARG, request->dataItem, (int)q);
		
		//Ensure that the item was actually locked
		if (dq_empty(q))
		{
			fprintf(stderr, WHERESTR "Bad release, item was not locked!\n", WHEREARG);
			RespondNACK(item);
		}
		else
		{
			//Get the next pending request
			next = dq_deq_front(q);
			if (next != NULL)
			{
				fprintf(stderr, WHERESTR "Bad queue, the top entry was not a locker!\n", WHEREARG);
				dq_enq_front(q, next);
				RespondNACK(item);
			}
			else
			{
				//Respond to the releaser
				printf(WHERESTR "Respond to the releaser\n", WHEREARG);
				RespondRelease(item);

				while (!dq_empty(q))
				{
					//Acquire for the next in the queue
					printf(WHERESTR "Acquire for the next in the queue\n", WHEREARG);
					next = dq_deq_front(q);
					if (((struct acquireRequest*)next->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE){
						//printf(WHERESTR "Acquiring WRITE on not locked object\n", WHEREARG);
						dq_enq_front(q, NULL);

						GUID id = obj->id;
						RecordBufferRequest(next);
						RespondAcquire(next, obj);
						DoInvalidate(id);
						NetInvalidate(id);
						
						break; //Done
					} else if (((struct acquireRequest*)next->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
						RespondAcquire(next, obj);						
					}
					else
						fprintf(stderr, WHERESTR "Error: packageCode where neither WRITE or READ", WHEREARG);						 
				}
			}					
		}
	}
	else
	{
		fprintf(stderr, WHERESTR "Tried to release a non-existing item!\n", WHEREARG);
		RespondNACK(item);		
	}
}

int isPageTableAvalible()
{
	if (dsmcbe_host_number == PAGE_TABLE_OWNER)
	{
		if (!ht_member(allocatedItems, (void*)PAGE_TABLE_ID))
			REPORT_ERROR("Host zero did not have the page table");
		
		return dq_empty(((dataObject)ht_get(allocatedItems, (void*)PAGE_TABLE_ID))->waitqueue);
	}
	else
		return ht_member(allocatedItems, (void*)PAGE_TABLE_ID);
}

void RequestPageTable(int mode)
{
	struct acquireRequest* acq;
	QueueableItem q;

	if (dsmcbe_host_number == PAGE_TABLE_OWNER && mode == ACQUIRE_MODE_READ) 
	{
		//We have to wait for someone to release it :(
		return;
	}
	else
	{
		if ((acq = MALLOC(sizeof(struct acquireRequest))) == NULL)
			REPORT_ERROR("MALLOC error");
		acq->dataItem = PAGE_TABLE_ID;
		acq->packageCode = mode == ACQUIRE_MODE_READ ? PACKAGE_ACQUIRE_REQUEST_READ : PACKAGE_ACQUIRE_REQUEST_WRITE;
		acq->requestID = 0;

		if ((q = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
			REPORT_ERROR("MALLOC error");

		q->dataRequest = acq;
		q->mutex = &queue_mutex;
		q->event = &queue_ready;
		q->queue = &priorityResponses;			

		printf(WHERESTR "processing PT event %d\n", WHEREARG, dsmcbe_host_number);

		if (dsmcbe_host_number != PAGE_TABLE_OWNER)
			NetRequest(q, PAGE_TABLE_OWNER);
		else
			DoAcquire(q, acq);		

		printf(WHERESTR "processed PT event\n", WHEREARG);
	}
}

unsigned int GetMachineID(GUID id)
{
	//printf(WHERESTR "Getting machine id for item %d\n", WHEREARG, id);
	dataObject obj = ht_get(allocatedItems, PAGE_TABLE_ID);
	//printf(WHERESTR "Getting machine id for item EA: %d\n", WHEREARG, obj);
	//printf(WHERESTR "Getting machine id for item EA: %d\n", WHEREARG, obj->EA);
	printf(WHERESTR "Getting machine id for item %d, result: %d\n", WHEREARG, id, ((unsigned int *)obj->EA)[id]);
	return ((unsigned int *)obj->EA)[id];
}

void HandleCreateRequest(QueueableItem item)
{
	struct createRequest* req = item->dataRequest;
	unsigned int machineId = GetMachineID(req->dataItem);
	printf(WHERESTR "processing create event\n", WHEREARG);
	if (machineId != dsmcbe_host_number) {
		printf(WHERESTR "Sending network request event\n", WHEREARG);
		NetRequest(item, machineId);
		printf(WHERESTR "Sent network request event\n", WHEREARG);
	} else { 
		printf(WHERESTR "Sending local request event\n", WHEREARG);
		DoCreate(item, req);
		printf(WHERESTR "Sent local request event\n", WHEREARG);
	}
}

void HandleAcquireRequest(QueueableItem item)
{
	struct acquireRequest* req = item->dataRequest;
	unsigned int machineId = GetMachineID(req->dataItem);
	
	printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
	
	if (machineId != dsmcbe_host_number && machineId != UINT_MAX)
	{
		printf(WHERESTR "Acquire for item %d must be handled remotely, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
		if (ht_member(allocatedItems, (void*)req->dataItem) && req->packageCode == PACKAGE_ACQUIRE_REQUEST_READ)
		{
			printf(WHERESTR "Read acquire for item %d, machineid: %d, machine id: %d, returned from local cache\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
			RespondAcquire(item, ht_get(allocatedItems, (void*)req->dataItem));
		}
		else
		{
			printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, registering\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
			if (!ht_member(waiters, (void*)((struct acquireRequest*)item->dataRequest)->dataItem))
			{
				printf(WHERESTR "Acquire for item %d, machineid: %d, machine id: %d, sending remote request\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
				ht_insert(waiters, (void*)((struct acquireRequest*)item->dataRequest)->dataItem, dq_create());
				NetRequest(item, machineId);
			}
			dq_enq_back(ht_get(waiters, (void*)req->dataItem), item);
		}
	}
	else if (machineId == UINT_MAX)
	{
		printf(WHERESTR "Acquire for non-existing item %d, registering request locally, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
		if (!ht_member(waiters, (void*)req->dataItem))
			ht_insert(waiters, (void*)req->dataItem, dq_create());
		dq_enq_back((dqueue)ht_get(waiters, (void*)req->dataItem), item);
	}
	else 
	{
		printf(WHERESTR "Processing acquire locally for %d, machineid: %d, machine id: %d\n", WHEREARG, req->dataItem, machineId, dsmcbe_host_number);
		DoAcquire(item, (struct acquireRequest*)item->dataRequest);
	}	
}

void HandleReleaseRequest(QueueableItem item)
{
	struct releaseRequest* req = item->dataRequest;
	unsigned int machineId = GetMachineID(req->dataItem);

	printf(WHERESTR "processing release event\n", WHEREARG);
	if (machineId != dsmcbe_host_number)
	{
		printf(WHERESTR "processing release event, not owner\n", WHEREARG);
		if (req->mode == ACQUIRE_MODE_WRITE)
			DoInvalidate(req->dataItem);
		NetRequest(item, machineId);
	}
	else
	{
		printf(WHERESTR "processing release event, owner\n", WHEREARG);
		if (req->mode == ACQUIRE_MODE_WRITE)
		{
			dataObject obj = ht_get(allocatedItems, (void*)req->dataItem);
			if (ht_member(allocatedItemsDirty, obj))
			{
				printf(WHERESTR "processing release event, object is in use, re-registering\n", WHEREARG);
				//The object is still in use, re-register, the last invalidate response will free it
				dqueue tmp = obj->waitqueue;
				obj->waitqueue = NULL;
				if (req->data == NULL)
					req->data = obj->EA;
				ht_delete(allocatedItems, (void*)req->dataItem);
				if ((obj = MALLOC(sizeof(struct dataObjectStruct))) == NULL)
					REPORT_ERROR("malloc error");
				
				obj->EA = req->data;
				obj->id = req->dataItem;
				obj->size = req->dataSize;
				obj->waitqueue = tmp;
				
				ht_insert(allocatedItems, (void*)obj->id, obj);
			}
			else
			{
				printf(WHERESTR "processing release event, object is not in use, updating\n", WHEREARG);
				//The object is not in use, just copy in the new version
				if (obj->EA != req->data && req->data != NULL)
				{
					memcpy(obj->EA, req->data, obj->size);
					FREE(req->data);
					req->data = NULL;
				}
			}
		}
		
		printf(WHERESTR "processing release event, owner\n", WHEREARG);
		DoRelease(item, (struct releaseRequest*)item->dataRequest);
	}
	printf(WHERESTR "processed release event\n", WHEREARG);
	
}

void HandleInvalidateRequest(QueueableItem item)
{
	struct invalidateRequest* req = item->dataRequest;
	
	printf(WHERESTR "processing network invalidate request for: %d\n", WHEREARG, req->dataItem);
	DoInvalidate(req->dataItem);
	
	if (dsmcbe_host_number != PAGE_TABLE_OWNER && req->dataItem == PAGE_TABLE_ID)
	{
		if (pagetableWaiters == NULL || queue_empty(pagetableWaiters))
		{
			printf(WHERESTR "issuing automatic request for page table\n", WHEREARG);
			pagetableWaiters = queue_create();
			RequestPageTable(ACQUIRE_MODE_READ);							
		}
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
	
	printf(WHERESTR "processing invalidate response for\n", WHEREARG);
	object = ht_get(pendingSequenceNr, (void*)req->requestID);
	unsigned int* count = ht_get(allocatedItemsDirty, (void*)object);
	*count = *count - 1;
	if (*count <= 0) {
		printf(WHERESTR "The last response is in for: %d\n", WHEREARG, object->id);
		ht_delete(allocatedItemsDirty, (void*)object);
				
		if(ht_member(writebufferReady, (void*)object->id)) {
			printf(WHERESTR "The last response is in for: %d, sending writebuffer signal\n", WHEREARG, object->id);
			
			QueueableItem reciever = ht_get(writebufferReady, (void*)object->id);
			
			struct writebufferReady* req = (struct writebufferReady*)MALLOC(sizeof(struct writebufferReady));
			if (req == NULL)
				REPORT_ERROR("malloc error");
				
			ht_delete(writebufferReady, (void*)object->id);
			req->packageCode = PACKAGE_WRITEBUFFER_READY;
			req->requestID = ((struct acquireRequest*)reciever->dataRequest)->requestID;
			req->dataItem = object->id;
		
			RespondAny(reciever, req);
		}

		if (!ht_member(allocatedItems, (void*)object->id) || ht_get(allocatedItems, (void*)object->id) != object)
		{  		
			printf(WHERESTR "Item is no longer required, freeing: %d\n", WHEREARG, object->id);
			FREE_ALIGN(object->EA);
			object->EA = NULL;
			FREE(object);
			object = NULL;
			FREE(count);
			count = NULL;
		}
	}

	printf(WHERESTR "removing pending invalidate response for: %d\n", WHEREARG, object->id);
	ht_delete(pendingSequenceNr, (void*)req->requestID);
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);
	item  = NULL;
	printf(WHERESTR "processing invalidate response for: %d\n", WHEREARG, object->id);
	
}

void HandleAcquireResponse(QueueableItem item)
{
	printf(WHERESTR "processing acquire response event\n", WHEREARG);
	struct acquireResponse* req = item->dataRequest;
	dataObject object;
	
	if (req->dataSize == 0 || (dsmcbe_host_number == PAGE_TABLE_OWNER && req->dataItem == PAGE_TABLE_ID))
	{
		printf(WHERESTR "acquire response had local copy\n", WHEREARG);
		//TODO: Should we copy the contents over?
		object = ht_get(allocatedItems, (void*)req->dataItem);
	}
	else
	{
		printf(WHERESTR "registering item locally\n", WHEREARG);

		if ((object = (dataObject)MALLOC(sizeof(struct dataObjectStruct))) == NULL)
			fprintf(stderr, WHERESTR "RequestCoordinator.c: MALLOC error\n", WHEREARG);
		
		if (req->data == NULL)
			REPORT_ERROR("Acquire response had empty data");	
		object->id = req->dataItem;
		object->EA = req->data;
		object->size = req->dataSize;
		object->waitqueue = NULL;

		if (req->dataItem == PAGE_TABLE_ID)
			printf(WHERESTR "pagetable entry 1 = %d\n", WHEREARG, ((unsigned int*)object->EA)[1]);
		ht_insert(allocatedItems, (void*)object->id, object);
	}

	printf(WHERESTR "testing local copy, obj: %d, %d, %d\n", WHEREARG, (int)object, (int)waiters, object->id);
	
	//If the response is a pagetable acquire, check if items have been created, that we are awaiting 
	if (object->id == PAGE_TABLE_ID)
	{
		printf(WHERESTR "Releasing local waiters\n", WHEREARG);
		pthread_mutex_lock(&queue_mutex);
		hashtableIterator it = ht_iter_create(waiters);
		while(ht_iter_next(it))
		{
			printf(WHERESTR "Trying item %d\n", WHEREARG, (GUID)ht_iter_get_key(it));
			if (((unsigned int*)object->EA)[(GUID)ht_iter_get_key(it)] != UINT_MAX)
			{
				printf(WHERESTR "Matched, emptying queue item %d\n", WHEREARG, (GUID)ht_iter_get_key(it));
				dqueue dq = ht_get(waiters, ht_iter_get_key(it));
				while(!dq_empty(dq))
				{
					printf(WHERESTR "processed a waiter for %d\n", WHEREARG, object->id);
					queue_enq(bagOfTasks, dq_deq_front(dq));
				}
					
				ht_delete(waiters, ht_iter_get_key(it));
				ht_iter_reset(it);							 
			}
		}
		ht_iter_destroy(it);
		pthread_mutex_unlock(&queue_mutex);
	}

	//If this is an acquire for an object we requested, release the waiters
	if (ht_member(waiters, (void*)object->id))
	{
		printf(WHERESTR "testing local copy\n", WHEREARG);
		dqueue dq = ht_get(waiters, (void*)object->id);
		ht_delete(waiters, (void*)object->id);
		
		pthread_mutex_lock(&queue_mutex);
		while(!dq_empty(dq))
		{
			printf(WHERESTR "processed a waiter for %d\n", WHEREARG, object->id);
			QueueableItem q = dq_deq_front(dq);
			printf(WHERESTR "waiter package type: %d\n", WHEREARG, ((struct createRequest*)q->dataRequest)->packageCode);
			queue_enq(bagOfTasks, q);
		}
		pthread_mutex_unlock(&queue_mutex);
		
	}

	printf(WHERESTR "testing local copy\n", WHEREARG);


	//We have recieved a new copy of the page table, re-enter all those awaiting this
	if (pagetableWaiters != NULL && req->dataItem == PAGE_TABLE_ID)
	{
		printf(WHERESTR "fixing pagetable waiters\n", WHEREARG);

		//TODO: Must be a double queue
		pthread_mutex_lock(&queue_mutex);
		while(!queue_empty(pagetableWaiters))
		{
			QueueableItem cr = queue_deq(pagetableWaiters);
			printf(WHERESTR "evaluating waiter\n", WHEREARG);
			
			if (((struct createRequest*)cr->dataRequest)->packageCode == PACKAGE_CREATE_REQUEST)
			{
				printf(WHERESTR "waiter was for create %d\n", WHEREARG, ((struct createRequest*)cr->dataRequest)->dataItem);
				if (((struct acquireResponse*)item->dataRequest)->mode != ACQUIRE_MODE_READ)
				{
					printf(WHERESTR "incoming response was for write %d\n", WHEREARG, req->dataItem);
					unsigned int* pagetable = (unsigned int*)req->data;
					GUID id = ((struct createRequest*)cr->dataRequest)->dataItem;
					//Ensure we are the creators
					if (pagetable[id] != UINT_MAX)
					{
						REPORT_ERROR("Tried to create an already existing item");
						RespondNACK(cr);						
					}
					else
					{
						pagetable[id] = dsmcbe_host_number;
						//We are the owners of this entry, so perform the required work 
						DoCreate(cr, (struct createRequest*)cr->dataRequest);
					}
					
					//We have the pagetable lock, so release it
					QueueableItem qs;
					struct releaseRequest* rr;

					if ((qs = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
						REPORT_ERROR("MALLOC error");
					if ((rr = MALLOC(sizeof(struct releaseRequest))) == NULL)
						REPORT_ERROR("MALLOC error");

					qs->event = NULL;
					qs->mutex = NULL;
					qs->queue = NULL;
					qs->dataRequest = rr;

					printf(WHERESTR "releasing pagetable, %d\n", WHEREARG, req->dataItem);
					rr->packageCode = PACKAGE_RELEASE_REQUEST;
					rr->data = req->data;
					rr->dataItem = req->dataItem;
					rr->dataSize = req->dataSize;
					rr->mode = req->mode;
					rr->offset = 0;
					rr->requestID = req->requestID;
					
					//Local pagetable, or remote?
					if (dsmcbe_host_number == PAGE_TABLE_OWNER)
						DoRelease(qs, (struct releaseRequest*)qs->dataRequest);
					else
						NetRequest(qs, PAGE_TABLE_OWNER);
				}
				
				break;
			}
			else
			{
				printf(WHERESTR "Reinserted package with type %d, into queue\n", WHEREARG, ((struct createRequest*)cr->dataRequest)->packageCode);					
				queue_enq(bagOfTasks, cr);
			}
		}
		pthread_mutex_unlock(&queue_mutex);
		pagetableWaiters = NULL;
	}
	
	FREE(item->dataRequest);
	item->dataRequest = NULL;
	FREE(item);				
	item = NULL;
	printf(WHERESTR "Handled acquire response\n", WHEREARG);
}

//This is the main thread function
void* ProccessWork(void* data)
{
	QueueableItem item;
	unsigned int datatype;
	int isPtResponse;
	
	while(!terminate)
	{
		//Get the next item, or sleep until it arrives	
		printf(WHERESTR "fetching job\n", WHEREARG);
			
		pthread_mutex_lock(&queue_mutex);
		while (queue_empty(bagOfTasks) && queue_empty(priorityResponses) && !terminate) {
			//printf(WHERESTR "waiting for event\n", WHEREARG);
			pthread_cond_wait(&queue_ready, &queue_mutex);
			//printf(WHERESTR "event recieved\n", WHEREARG);
		}
		
		if (terminate)
		{
			pthread_mutex_unlock(&queue_mutex);
			break;
		}
		
		isPtResponse = 0;

		//We prioritize page table responses
		if (!queue_empty(priorityResponses))
		{
			printf(WHERESTR "fetching priority response\n", WHEREARG);
			if ((item = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
				REPORT_ERROR("MALLOC error");
			item->dataRequest = queue_deq(priorityResponses);
			item->event = &queue_ready;
			item->mutex = &queue_mutex;
			item->queue = &priorityResponses;
			isPtResponse = 1;
		}
		else
		{
			printf(WHERESTR "fetching actual job\n", WHEREARG);
			item = (QueueableItem)queue_deq(bagOfTasks);
			if (item == NULL)
				REPORT_ERROR("Empty entry in request queue");
			if (item->dataRequest == NULL)
				REPORT_ERROR("Empty request in queued item")
			isPtResponse = ((struct createRequest*)item->dataRequest)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireRequest*)item->dataRequest)->dataItem == PAGE_TABLE_ID;
		}
			
		pthread_mutex_unlock(&queue_mutex);
		
		//Get the type of the package and perform the corresponding action
		printf(WHERESTR "fetching event\n", WHEREARG);
		datatype = ((struct acquireRequest*)item->dataRequest)->packageCode;

		printf(WHERESTR "processing package type %d\n", WHEREARG, datatype);


		//If we do not have an idea where to forward this, save it for later, 
		//but let pagetable responses go through		
		if ((!isPageTableAvalible() || datatype == PACKAGE_CREATE_REQUEST) && !isPtResponse )
		{
			printf(WHERESTR "defering package type %d, page table is missing\n", WHEREARG, datatype);
			if (pagetableWaiters == NULL)
			{
				RequestPageTable(datatype == PACKAGE_CREATE_REQUEST ? ACQUIRE_MODE_WRITE : ACQUIRE_MODE_READ);
				pagetableWaiters = queue_create();
			}
			
			queue_enq(pagetableWaiters, item);
			printf(WHERESTR "restarting loop\n", WHEREARG);
			continue;
		}
		
		printf(WHERESTR "processing type %d\n", WHEREARG, datatype);
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
			case PACKAGE_INVALIDATE_RESPONSE:
				HandleInvalidateResponse(item);				
				break;
			case PACKAGE_ACQUIRE_RESPONSE:
				HandleAcquireResponse(item);
				break;
			
			default:
				printf(WHERESTR "Unknown package code: %i\n", WHEREARG, datatype);
				fprintf(stderr, WHERESTR "Unknown package recieved\n", WHEREARG);
				RespondNACK(item);
		};	
		
		//All responses ensure that the QueueableItem and request structures are free'd
		//It is the obligation of the requestor to free the response
	}
	
	//Returning the unused argument removes a warning
	return data;
}

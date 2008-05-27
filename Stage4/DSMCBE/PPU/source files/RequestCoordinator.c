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

queue pagetableWaiters;
queue pagetableResponses;

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
	queue_destroy(pagetableResponses);
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
		pagetableWaiters = NULL;
		invalidateSubscribers = slset_create(lessint);
		pagetableResponses = queue_create();
		
		if (dsmcbe_host_number == 0)
		{
			if ((obj = malloc(sizeof(struct dataObjectStruct))) == NULL)
				REPORT_ERROR("malloc error");
				
			obj->size = sizeof(unsigned int) * 10000;
			obj->EA = _malloc_align(obj->size, 7);
			obj->id = PAGE_TABLE_ID;
			obj->waitqueue = dq_create();
			
			for(i = 0; i < 10000; i++)
				((unsigned int*)obj->EA)[i] = UINT_MAX;

			((unsigned int*)obj->EA)[PAGE_TABLE_ID] = 0; 
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
	
	free(item->dataRequest);
	item->dataRequest = NULL;
	free(item);
	item = NULL;
}

//Responds with NACK to a request
void RespondNACK(QueueableItem item)
{
	struct NACK* resp;
	if ((resp = (struct NACK*)malloc(sizeof(struct NACK))) == NULL)
		fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);
			
	resp->packageCode = PACKAGE_NACK;
	resp->hint = 0;
	
	RespondAny(item, resp);
}

//Responds to an acquire request
void RespondAcquire(QueueableItem item, dataObject obj)
{
	struct acquireResponse* resp;
	if ((resp = (struct acquireResponse*)malloc(sizeof(struct acquireResponse))) == NULL)
		fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);

	resp->packageCode = PACKAGE_ACQUIRE_RESPONSE;
	resp->dataSize = obj->size;
	resp->data = obj->EA;
	resp->dataItem = obj->id;
	resp->mode = ((struct acquireRequest*)item->dataRequest)->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE ? WRITE : READ;	
	//printf(WHERESTR "Performing Acquire, mode: %d (code: %d)\n", WHEREARG, resp->mode, ((struct acquireRequest*)item->dataRequest)->packageCode);

	RespondAny(item, resp);	
}

//Responds to a release request
void RespondRelease(QueueableItem item)
{
	struct releaseResponse* resp;
	if ((resp = (struct releaseResponse*)malloc(sizeof(struct releaseResponse))) == NULL)
		fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);
	
	resp->packageCode = PACKAGE_RELEASE_RESPONSE;

	RespondAny(item, resp);	
}

//Performs all actions releated to a create request
void DoCreate(QueueableItem item, struct createRequest* request)
{
	unsigned long size;
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
	data = _malloc_align(size + ((16 - size) % 16), 7);
	if (data == NULL)
	{
		REPORT_ERROR("Failed to allocate buffer for create");
		RespondNACK(item);
		return;
	}
	
	// Make datastructures for later use
	if ((object = (dataObject)malloc(sizeof(struct dataObjectStruct))) == NULL)
		REPORT_ERROR("malloc error");
	
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

	//printf(WHERESTR "Invalidating...\n", WHEREARG);
	
	if (dataItem == PAGE_TABLE_ID && dsmcbe_host_number == 0)
		return;
	
	pthread_mutex_lock(&invalidate_queue_mutex);
	kl = invalidateSubscribers->elements;
	
	if(dsmcbe_host_number == GetMachineID(dataItem)) {
		if(kl != NULL) {
			// Mark memory as dirty
			if(ht_member(allocatedItems, (void*)dataItem)) {
				obj = ht_get(allocatedItems, (void*)dataItem);
				ht_delete(allocatedItems, (void*)dataItem);
				unsigned int* count = (unsigned int*)malloc(sizeof(unsigned int));
				*count = 0;
				ht_insert(allocatedItemsDirty, obj, count);
			}
		} else {
			if(ht_member(allocatedItems, (void*)dataItem)) {
				obj = ht_get(allocatedItems, (void*)dataItem);
				_free_align(obj->EA);
				ht_delete(allocatedItems, (void*)dataItem);
				free(obj);
			}
		}
	}
		
	while(kl != NULL)
	{
		struct invalidateRequest* requ;
		if ((requ = (struct invalidateRequest*)malloc(sizeof(struct invalidateRequest))) == NULL)
			fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);
		
		requ->packageCode =  PACKAGE_INVALIDATE_REQUEST;
		requ->requestID = NEXT_SEQ_NO(sequence_nr, MAX_SEQUENCE_NR);
		requ->dataItem = dataItem;
		
		ht_insert(pendingSequenceNr, (void*)requ->requestID, obj);
		unsigned int* count = ht_get(allocatedItemsDirty, obj);
		*count = *count + 1;

		pthread_mutex_lock((pthread_mutex_t*)kl->data);
		queue_enq(*((queue*)kl->key), requ); 
		pthread_mutex_unlock((pthread_mutex_t*)kl->data); 
		
		kl = kl->next;
	}
	pthread_mutex_unlock(&invalidate_queue_mutex);

	//printf(WHERESTR "Invalidate request sent\n", WHEREARG);
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
	if (request->mode == READ)
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

						RespondAcquire(next, obj);
						DoInvalidate(obj->id);
						NetInvalidate(obj->id);
						
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
	if (dsmcbe_host_number == 0)
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

	if (dsmcbe_host_number == 0 && mode == READ) 
	{
		//We have to wait for someone to release it :(
		return;
	}
	else
	{
			if ((acq = malloc(sizeof(struct acquireRequest))) == NULL)
				REPORT_ERROR("malloc error");
			acq->dataItem = PAGE_TABLE_ID;
			acq->packageCode = mode == READ ? PACKAGE_ACQUIRE_REQUEST_READ : PACKAGE_ACQUIRE_REQUEST_WRITE;
			acq->requestID = 0;

			if ((q = malloc(sizeof(struct QueueableItemStruct))) == NULL)
				REPORT_ERROR("malloc error");

			q->dataRequest = acq;
			q->mutex = &queue_mutex;
			q->event = &queue_ready;
			q->queue = &pagetableResponses;			

			printf(WHERESTR "processing PT event %d\n", WHEREARG, dsmcbe_host_number);

			if (dsmcbe_host_number != 0)
				NetRequest(q, 0);
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

//This is the main thread function
void* ProccessWork(void* data)
{
	QueueableItem item;
	dataObject object;
	unsigned int datatype;
	int isPtResponse;
	
	while(!terminate)
	{
		//Get the next item, or sleep until it arrives	
		//printf(WHERESTR "fetching job\n", WHEREARG);
			
		pthread_mutex_lock(&queue_mutex);
		while (queue_empty(bagOfTasks) && queue_empty(pagetableResponses) && !terminate) {
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
		if (!queue_empty(pagetableResponses))
		{
			if ((item = malloc(sizeof(struct QueueableItemStruct))) == NULL)
				REPORT_ERROR("malloc error");
			item->dataRequest = queue_deq(pagetableResponses);
			item->event = &queue_ready;
			item->mutex = &queue_mutex;
			item->queue = &pagetableResponses;
			isPtResponse = 1;
		}
		else
		{
			item = (QueueableItem)queue_deq(bagOfTasks);
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
				RequestPageTable(datatype == PACKAGE_CREATE_REQUEST ? WRITE : READ);
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
				printf(WHERESTR "processing create event\n", WHEREARG);
				if (GetMachineID(((struct createRequest*)item->dataRequest)->dataItem) != dsmcbe_host_number) {
					printf(WHERESTR "Sending network request event\n", WHEREARG);
					NetRequest(item, GetMachineID(((struct createRequest*)item->dataRequest)->dataItem));
					printf(WHERESTR "Sent network request event\n", WHEREARG);
				} else { 
					printf(WHERESTR "Sending local request event\n", WHEREARG);
					DoCreate(item, (struct createRequest*)item->dataRequest);
					printf(WHERESTR "Sent local request event\n", WHEREARG);
				}
				break;
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				if (GetMachineID(((struct acquireRequest*)item->dataRequest)->dataItem) != dsmcbe_host_number && GetMachineID(((struct acquireRequest*)item->dataRequest)->dataItem) != UINT_MAX)
				{
					if (ht_member(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem))
						RespondAcquire(item, ht_get(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem));
					else
					{
						if (!ht_member(waiters, (void*)((struct acquireRequest*)item->dataRequest)->dataItem))
						{
							ht_insert(waiters, (void*)((struct acquireRequest*)item->dataRequest)->dataItem, dq_create());
							NetRequest(item, GetMachineID(((struct acquireRequest*)item->dataRequest)->dataItem));
						}
						dq_enq_back(ht_get(waiters, (void*)((struct acquireRequest*)item->dataRequest)->dataItem), item);
					}
				}
				else 
				{
					printf(WHERESTR "Processing acquire locally, machineid: %d, machine id: %d\n", WHEREARG, GetMachineID(((struct acquireRequest*)item->dataRequest)->dataItem), dsmcbe_host_number);
					DoAcquire(item, (struct acquireRequest*)item->dataRequest);
				}
				break;
			
			case PACKAGE_RELEASE_REQUEST:
				printf(WHERESTR "processing release event\n", WHEREARG);
				if (GetMachineID(((struct releaseRequest*)item->dataRequest)->dataItem) != dsmcbe_host_number)
				{
					printf(WHERESTR "processing release event, not owner\n", WHEREARG);
					if (((struct releaseRequest*)item->dataRequest)->mode == WRITE)
						DoInvalidate(((struct releaseRequest*)item->dataRequest)->dataItem);
					NetRequest(item, GetMachineID(((struct releaseRequest*)item->dataRequest)->dataItem));
				}
				else
				{
					printf(WHERESTR "processing release event, owner\n", WHEREARG);
					//TODO: If this is from the network, we only have a copy of data... 
					DoRelease(item, (struct releaseRequest*)item->dataRequest);
				}
				printf(WHERESTR "processed release event\n", WHEREARG);
				break;
			
			case PACKAGE_INVALIDATE_REQUEST:
				DoInvalidate(((struct invalidateRequest*)item->dataRequest)->dataItem);
				free(item->dataRequest);
				free(item);
				
				break;
			
			case PACKAGE_INVALIDATE_RESPONSE:				
				object = ht_get(pendingSequenceNr, (void*)((struct invalidateResponse*)item->dataRequest)->requestID);
				unsigned int* count = ht_get(allocatedItemsDirty, (void*)object);
				*count = *count - 1;
				if (*count <= 0) {
					_free_align(object->EA);
					ht_delete(allocatedItemsDirty, (void*)object);
					free(object);
					free(count);
				}
				ht_delete(pendingSequenceNr, (void*)((struct invalidateResponse*)item->dataRequest)->requestID);
				free(item->dataRequest);
				free(item);
			break;
			
			case PACKAGE_ACQUIRE_RESPONSE:

				printf(WHERESTR "processing acquire response event\n", WHEREARG);

				if (item->event != NULL || item->mutex != NULL || item->queue != NULL)
					REPORT_ERROR("Item from network was not cleaned");
				
				if (((struct acquireResponse*)item->dataRequest)->dataSize == 0 || (dsmcbe_host_number == 0 && ((struct acquireResponse*)item->dataRequest)->dataItem == PAGE_TABLE_ID))
				{
					printf(WHERESTR "acquire response had local copy\n", WHEREARG);
					object = ht_get(allocatedItems, (void*)((struct acquireResponse*)item->dataRequest)->dataItem);
				}
				else
				{
					printf(WHERESTR "registering item locally\n", WHEREARG);
	
					if ((object = (dataObject)malloc(sizeof(struct dataObjectStruct))) == NULL)
						fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);
						
					object->id = ((struct acquireResponse*)item->dataRequest)->dataItem;
					object->EA = ((struct acquireResponse*)item->dataRequest)->data;
					object->size = ((struct acquireResponse*)item->dataRequest)->dataSize;
					object->waitqueue = NULL;
	
					if (((struct acquireResponse*)item->dataRequest)->dataItem == PAGE_TABLE_ID)
						printf(WHERESTR "pagetable entry 1 = %d\n", WHEREARG, ((unsigned int*)object->EA)[1]);
					ht_insert(allocatedItems, (void*)object->id, object);
				}

				printf(WHERESTR "testing local copy, obj: %d, %d\n", WHEREARG, object, waiters);
			
				if (ht_member(waiters, (void*)object->id))
				{
					printf(WHERESTR "testing local copy\n", WHEREARG);
					dqueue dq = ht_get(waiters, (void*)object->id);
					ht_delete(waiters, (void*)object->id);
					
					while(!dq_empty(dq))
						RespondAcquire(dq_deq_front(dq), object);
				}

				printf(WHERESTR "testing local copy\n", WHEREARG);


				//We have recieved a new copy of the page table, re-enter all those awaiting this
				if (pagetableWaiters != NULL && isPtResponse)
				{
					printf(WHERESTR "fixing pagetable waiters\n", WHEREARG);

					//TODO: Must be a double queue
					pthread_mutex_lock(&queue_mutex);
					while(!queue_empty(pagetableWaiters))
					{
						QueueableItem cr = queue_deq(pagetableWaiters);
						if (((struct createRequest*)cr->dataRequest)->packageCode == PACKAGE_CREATE_REQUEST)
						{
							if (((struct acquireResponse*)item->dataRequest)->mode == WRITE)
							{
								unsigned int* pagetable = (unsigned int*)((struct acquireResponse*)item->dataRequest)->data;
								GUID id = ((struct createRequest*)cr->dataRequest)->dataItem;
								pagetable[id] = dsmcbe_host_number;
								
								QueueableItem qs;
								if ((qs = malloc(sizeof(struct QueueableItemStruct))) == NULL)
									REPORT_ERROR("malloc error");
									
								qs->event = NULL;
								qs->mutex = NULL;
								qs->queue = NULL;
								
								if ((qs->dataRequest = malloc(sizeof(struct releaseRequest))) == NULL)
									REPORT_ERROR("malloc error");
									
								((struct releaseRequest*)qs->dataRequest)->packageCode = PACKAGE_RELEASE_REQUEST;
								((struct releaseRequest*)qs->dataRequest)->data = ((struct acquireResponse*)item->dataRequest)->data;
								((struct releaseRequest*)qs->dataRequest)->dataItem = ((struct acquireResponse*)item->dataRequest)->dataItem;
								((struct releaseRequest*)qs->dataRequest)->dataSize = ((struct acquireResponse*)item->dataRequest)->dataSize;
								((struct releaseRequest*)qs->dataRequest)->mode = ((struct acquireResponse*)item->dataRequest)->mode;
								((struct releaseRequest*)qs->dataRequest)->offset = 0;
								((struct releaseRequest*)qs->dataRequest)->requestID = ((struct acquireResponse*)item->dataRequest)->requestID;
								
								if (dsmcbe_host_number == 0)
									DoRelease(qs, (struct releaseRequest*)qs->dataRequest);
								else
									NetRequest(qs, 0);
								 
								DoCreate(cr, (struct createRequest*)cr->dataRequest);
							}
							
							break;
						}
						else					
							queue_enq(bagOfTasks, cr);
					}
					pthread_mutex_unlock(&queue_mutex);
					pagetableWaiters = NULL;
				}
				
				printf(WHERESTR "re-inserted pagetable waiters\n", WHEREARG);
				free (item->dataRequest);
				item->dataRequest = NULL;
				printf(WHERESTR "re-inserted pagetable waiters\n", WHEREARG);
				free(item);				
				item = NULL;
				printf(WHERESTR "re-inserted pagetable waiters\n", WHEREARG);
			
				
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

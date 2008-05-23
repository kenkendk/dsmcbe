/*
 *
 * This module contains implementation code for
 * Coordinating requests from various sources
 *
 */
 
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include "../header files/RequestCoordinator.h"
#include "../header files/NetworkHandler.h"

#include "../../common/debug.h"

volatile int terminate;

pthread_mutex_t invalidate_queue_mutex;
pthread_mutex_t queue_mutex;
pthread_cond_t queue_ready;
queue bagOfTasks = NULL;
pthread_t workthread;

hashtable allocatedItems;
hashtable allocatedItemsDirty;
hashtable waiters;

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
		
	pthread_join(workthread, NULL);
	
	pthread_mutex_destroy(&queue_mutex);
	pthread_cond_destroy(&queue_ready);
}

//This method initializes all items related to the coordinator and starts the handler thread
void InitializeCoordinator()
{
	pthread_attr_t attr;

	if (bagOfTasks == NULL)
	{
		bagOfTasks = queue_create();
		terminate = 0;
	
		/* Initialize mutex and condition variable objects */
		pthread_mutex_init(&queue_mutex, NULL);
		pthread_cond_init (&queue_ready, NULL);

		/* For portability, explicitly create threads in a joinable state */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_create(&workthread, &attr, ProccessWork, NULL);
		pthread_attr_destroy(&attr);
		allocatedItems = ht_create(10, lessint, hashfc);
		waiters = ht_create(10, lessint, hashfc);
		invalidateSubscribers = slset_create(lessint);
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
	pthread_mutex_lock(item->mutex);
	//printf(WHERESTR "responding, locked %i\n", WHEREARG, (int)item->queue);
	queue_enq(*(item->queue), resp);
	if (item->event != NULL)
		pthread_cond_signal(item->event);
	//printf(WHERESTR "responding, signalled %i\n", WHEREARG, (int)item->event);
	pthread_mutex_unlock(item->mutex);
	//printf(WHERESTR "responding, done\n", WHEREARG);
	
	free(item->dataRequest);
	free(item);
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
		fprintf(stderr, WHERESTR "RequestCoordinator.c: Create request for already existing item\n", WHEREARG);
		RespondNACK(item);
	}
	else
	{
		size = request->dataSize;
		data = _malloc_align(size + ((16 - size) % 16), 7);
		if (data == NULL)
		{
			fprintf(stderr, WHERESTR "Failed to allocate buffer for create\n", WHEREARG);
			RespondNACK(item);
			return;
		}
			

		// Make datastructures for later use
		if ((object = (dataObject)malloc(sizeof(struct dataObjectStruct))) == NULL)
			fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);
		
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
	
}

//Perform all actions related to an invalidate
void DoInvalidate(GUID dataItem)
{
	keylist kl;

	//printf(WHERESTR "Invalidating...\n", WHEREARG);
	
	pthread_mutex_lock(&invalidate_queue_mutex);
	kl = invalidateSubscribers->elements;
	
	if(dsmcbe_host_number == GetMachineID(dataItem)) {
		if(kl != NULL) {
			// Mark memory as dirty
			// Record invalidateRequests i while-loop
			// When invalidateResponse received, remove record
			// from list.
			// When list is empty, free memory.
		} else {
			if(ht_member(allocatedItems, (void*)dataItem)) {
				dataObject obj = ht_get(allocatedItems, (void*)dataItem);
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
		requ->requestID = 0; //There is no response
		requ->dataItem = dataItem;

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
			//printf(WHERESTR "Object not locked\n", WHEREARG);
			if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_READ) {
				//printf(WHERESTR "Acquiring READ on not locked object\n", WHEREARG);
				RespondAcquire(item, obj);
			} else if (request->packageCode == PACKAGE_ACQUIRE_REQUEST_WRITE) {			
				//printf(WHERESTR "Acquiring WRITE on not locked object\n", WHEREARG);
				dq_enq_front(q, NULL);

				RespondAcquire(item, obj);
				DoInvalidate(obj->id);
				NetInvalidate(obj->id);
			}

		}
		else {
			//Otherwise add the request to the wait list
			//printf(WHERESTR "Object locked\n", WHEREARG);
			dq_enq_back(q, item);
		}
	}
	else
	{
		//printf(WHERESTR "acquire requested for id %d, waiting for create\n", WHEREARG, request->dataItem);
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
				//printf(WHERESTR "Respond to the releaser\n", WHEREARG);
				RespondRelease(item);

				while (!dq_empty(q))
				{
					//Acquire for the next in the queue
					//printf(WHERESTR "Acquire for the next in the queue\n", WHEREARG);
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

//This is the main thread function
void* ProccessWork(void* data)
{
	QueueableItem item;
	dataObject object;
	unsigned int datatype;
	
	while(!terminate)
	{

		//Get the next item, or sleep until it arrives	
		//printf(WHERESTR "fetching job\n", WHEREARG);
			
		pthread_mutex_lock(&queue_mutex);
		while (queue_empty(bagOfTasks) && !terminate) {
			//printf(WHERESTR "waiting for event\n", WHEREARG);
			pthread_cond_wait(&queue_ready, &queue_mutex);
			//printf(WHERESTR "event recieved\n", WHEREARG);
		}
		if (terminate)
			break;

		//printf(WHERESTR "fetching event\n", WHEREARG);
		item = (QueueableItem)queue_deq(bagOfTasks);
		pthread_mutex_unlock(&queue_mutex);
		
		//Get the type of the package and perform the corresponding action
		datatype = ((struct acquireRequest*)item->dataRequest)->packageCode;
		switch(datatype)
		{
			case PACKAGE_CREATE_REQUEST:
				if (GetMachineID(((struct createRequest*)item->dataRequest)->dataItem) != dsmcbe_host_number)
					NetRequest(item);
				else 
					DoCreate(item, (struct createRequest*)item->dataRequest);
				break;
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				if (GetMachineID(((struct acquireRequest*)item->dataRequest)->dataItem) != dsmcbe_host_number)
				{
					if (ht_member(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem))
						RespondAcquire(item, ht_get(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem));
					else
					{
						if (!ht_member(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem))
						{
							ht_insert(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem, dq_create());
							NetRequest(item);
						}
						dq_enq_back(ht_get(allocatedItems, (void*)((struct acquireRequest*)item->dataRequest)->dataItem), item);
					}
				}
				else 
					DoAcquire(item, (struct acquireRequest*)item->dataRequest);
				break;
			
			case PACKAGE_RELEASE_REQUEST:
				if (GetMachineID(((struct releaseRequest*)item->dataRequest)->dataItem) != dsmcbe_host_number)
				{
					if (((struct releaseRequest*)item->dataRequest)->mode == WRITE)
						DoInvalidate(((struct releaseRequest*)item->dataRequest)->dataItem);
					NetRequest(item);
				}
				else 
					DoRelease(item, (struct releaseRequest*)item->dataRequest);
				break;
			
			case PACKAGE_INVALIDATE_REQUEST:
				DoInvalidate(((struct invalidateRequest*)item->dataRequest)->dataItem);
				free(item->dataRequest);
				free(item);
				
				break;
			
			case PACKAGE_ACQUIRE_RESPONSE:

				if (item->event != NULL || item->mutex != NULL || item->queue != NULL)
					REPORT_ERROR("Item from network was not cleaned");
				
				if ((object = (dataObject)malloc(sizeof(struct dataObjectStruct))) == NULL)
					fprintf(stderr, WHERESTR "RequestCoordinator.c: malloc error\n", WHEREARG);
		
				object->id = ((struct acquireResponse*)item->dataRequest)->dataItem;
				object->EA = ((struct acquireResponse*)item->dataRequest)->data;
				object->size = ((struct acquireResponse*)item->dataRequest)->dataSize;
				object->waitqueue = NULL;

				ht_insert(allocatedItems, (void*)object->id, object);

				free (item->dataRequest);
				item->dataRequest = NULL;
				free(item);				
				item = NULL;

				
				if (ht_member(waiters, (void*)object->id))
				{
					dqueue dq = ht_get(waiters, (void*)object->id);
					ht_delete(waiters, (void*)object->id);
					
					while(!dq_empty(dq))
						RespondAcquire(dq_deq_front(dq), object);
				}
				
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

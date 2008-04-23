/*
 *
 * This module contains implementation code for
 * Coordinating requests from various sources
 *
 */
 
#include "RequestCoordinator.h"

volatile int terminate;

pthread_mutex_t queue_mutex;
pthread_cond_t queue_ready;
queue bagOfTasks = NULL;
pthread_t workthread;

hashtable allocatedItems;
hashtable waiters;


typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{
	
	GUID id;
	void* EA;
	unsigned long size;
	queue waitqueue;
};

int lessint(void* a, void* b){
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	return ((int)a % count);
}


void* ProccessWork(void* data);

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
	}
}
 
void EnqueItem(QueueableItem item)
{
 	pthread_mutex_lock(&queue_mutex);
 	
 	queue_enq(bagOfTasks, (void*)item);
 	
 	pthread_cond_signal(&queue_ready);
 	pthread_mutex_unlock(&queue_mutex);
}

void DoCreate(QueueableItem item, struct createRequest* request)
{
	struct acquireResponse* resp;
	unsigned long size;
	void* data;
	queue prevWaiters;
	
	if (ht_member(allocatedItems, (void*)request->dataItem))
	{
		data = NULL;
		size = 0;
	}
	else
	{
		size = request->dataSize;
		data = _malloc_align(size, 7);

		// Make datastructures for later use
		dataObject object = (dataObject)malloc(sizeof(struct dataObjectStruct));
		object->id = request->dataItem;
		object->EA = data;
		object->size = size;
		object->waitqueue = queue_create();
		
		queue_enq(object->waitqueue, NULL);
		if (ht_member(waiters, (void*)object->id))
		{
			prevWaiters = ht_get(waiters, (void*)object->id);
			while(!queue_empty(prevWaiters))
				queue_enq(object->waitqueue, queue_deq(prevWaiters));
				
			ht_delete(waiters, (void*)object->id);
		}
		
		ht_insert(allocatedItems, (void*)object->id, object);
	}
	
	resp = (struct acquireResponse*)malloc(sizeof(struct acquireResponse));
	resp->packageCode = PACKAGE_ACQUIRE_RESPONSE;
	resp->requestID = request->requestID;
	resp->dataSize = request->dataSize;
	resp->data = data;
	
	pthread_mutex_lock(&item->mutex);
	queue_enq(item->queue, resp);
	pthread_cond_signal(&item->event);
	pthread_mutex_unlock(&item->mutex);
	
	free(request);
	free(item);
	
}

void DoAcquire(QueueableItem item, struct acquireRequest* request)
{
	queue q;
	dataObject obj;
	struct acquireResponse* resp;
	
	if (ht_member(allocatedItems, (void*)request->dataItem))
	{
		obj = ht_get(allocatedItems, (void*)request->dataItem);
		q = obj->waitqueue;
		if (queue_empty(q))
		{
			queue_enq(q, NULL);
	
			resp = (struct acquireResponse*)malloc(sizeof(struct acquireResponse));
			resp->packageCode = PACKAGE_ACQUIRE_RESPONSE;
			resp->requestID = request->requestID;
			resp->dataSize = obj->size;
			resp->data = obj->EA;
			
			pthread_mutex_lock(&item->mutex);
			queue_enq(item->queue, resp);
			pthread_cond_signal(&item->event);
			pthread_mutex_unlock(&item->mutex);
			
			free(request);
			free(item);
		}
		else
			queue_enq(q, item);
	}
	else
	{
		if (!ht_member(waiters, (void*)request->dataItem))
			ht_insert(waiters, (void*)request->dataItem, queue_create());
		q = (queue)ht_get(waiters, (void*)request->dataItem);
		queue_enq(q, item);		
	}
	
}


void* ProccessWork(void* data)
{
	QueueableItem item;
	unsigned int datatype;
	
	while(!terminate)
	{
		pthread_mutex_lock(&queue_mutex);
		if (queue_empty(bagOfTasks))
			pthread_cond_wait(&queue_ready, &queue_mutex);
		item = (QueueableItem)queue_deq(bagOfTasks);
		pthread_mutex_unlock(&queue_mutex);	
		
		datatype = ((unsigned char*)item->dataRequest)[0];
		switch(datatype)
		{
			case PACKAGE_CREATE_REQUEST:
				DoCreate(item, (struct createRequest*)item->dataRequest);
				break;
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				DoAcquire(item, (struct acquireRequest*)item->dataRequest);
				break;
			
			case PACKAGE_RELEASE_REQUEST:
				break;
			
			case PACKAGE_INVALIDATE_REQUEST:
				break;
			
		};	
		
		//TODO: Remember to free(item) and free(item->dataRequest) when it is no longer needed
	}
	
	//Returning the unused argument removes a warning
	return data;
}

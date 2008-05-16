/*
 *
 * This module contains code that handles requests 
 * from PPU units
 *
 */

#include <pthread.h>
#include <malloc.h>
#include <stdio.h>
#include "../../common/datastructures.h"
#include "../../common/datapackages.h"
#include "../header files/PPUEventHandler.h"
#include "../header files/RequestCoordinator.h"

#include "../../common/debug.h"

//This mutex protects the pointer list
pthread_mutex_t pointer_mutex;
//This mutex protects the old pointer list
pthread_mutex_t pointerOld_mutex;
//This mutex protects the invalidate queue
pthread_mutex_t ppu_invalidate_mutex;

//These two tables contains the registered pointers, either active or retired
hashtable pointers, pointersOld;

//This is the queue of pending invalidates
queue pendingInvalidate;



int lessint(void* a, void* b);
int hashfc(void* a, unsigned int count);

//Setup the PPUHandler
void InitializePPUHandler()
{
	pointers = ht_create(10, lessint, hashfc);
	pthread_mutex_init(&pointer_mutex, NULL);
	pointersOld = ht_create(10, lessint, hashfc);
	pthread_mutex_init(&pointerOld_mutex, NULL);
	pendingInvalidate = queue_create();
	pthread_mutex_init(&ppu_invalidate_mutex, NULL);
	
	RegisterInvalidateSubscriber(&ppu_invalidate_mutex, &pendingInvalidate);
}

//Terminate the PPUHandler and release all resources
void TerminatePPUHandler()
{
	hashtableIterator it;
	queue keys;
	PointerEntry pe;
	
	it = ht_iter_create(pointers);
	keys = queue_create();
	while(ht_iter_next(it))
	{
		pe = ht_iter_get_value(it);
		queue_enq(keys, ht_iter_get_key(it)); 
		free(pe->data);
		free(pe);
	}
	ht_iter_destroy(it);
	
	while(!queue_empty(keys))
		ht_delete(pointers, queue_deq(keys));
	
	ht_destroy(pointers);
	queue_destroy(keys);
	

	it = ht_iter_create(pointersOld);
	keys = queue_create();
	while(ht_iter_next(it))
	{
		pe = ht_iter_get_value(it);
		queue_enq(keys, ht_iter_get_key(it)); 
		free(pe->data);
		free(pe);
	}
	ht_iter_destroy(it);
	
	while(!queue_empty(keys))
		ht_delete(pointersOld, queue_deq(keys));
	
	ht_destroy(pointers);
	queue_destroy(keys);
	
	UnregisterInvalidateSubscriber(&pendingInvalidate);
	
	pthread_mutex_destroy(&pointer_mutex);
	pthread_mutex_destroy(&pointerOld_mutex);
	pthread_mutex_destroy(&ppu_invalidate_mutex);
	queue_destroy(pendingInvalidate);
}

//Sends a request into the coordinator, and awaits the response (blocking)
void* forwardRequest(void* data)
{
	queue dummy;
	pthread_mutex_t m;
	pthread_cond_t e;
	QueueableItem q;
	
	//printf(WHERESTR "creating item\n", WHEREARG);
	//Create the entry, this will be released by the coordinator
	if ((q = (QueueableItem)malloc(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("PPUEventHandler.c: malloc error");
	
	dummy = queue_create();
	pthread_mutex_init(&m, NULL);
	pthread_cond_init(&e, NULL);
	q->dataRequest = data;
	q->event = &e;
	q->mutex = &m;
	q->queue = &dummy;
	
	//printf(WHERESTR "adding item to queue\n", WHEREARG);
	EnqueItem(q);
	//printf(WHERESTR "item added to queue %i\n", WHEREARG, (int)q);
	
	pthread_mutex_lock(&m);
	//printf(WHERESTR "locked %i\n", WHEREARG, (int)&m);
	
	while (queue_empty(dummy)) {
		//printf(WHERESTR "waiting for queue %i\n", WHEREARG, (int)&e);
		pthread_cond_wait(&e, &m);
		//printf(WHERESTR "queue filled\n", WHEREARG);
	}
	
	data = queue_deq(dummy);
	pthread_mutex_unlock(&m);

	//printf(WHERESTR "returning response\n", WHEREARG);
	
	queue_destroy(dummy);
	pthread_mutex_destroy(&m);
	pthread_cond_destroy(&e);
	
	return data;
}

//Record information about the returned pointer
void recordPointer(void* retval, GUID id, unsigned long size, unsigned long offset, int type)
{
	PointerEntry ent;
	
	//If the response was valid, record the item data
	if (retval != NULL)
	{		
		//printf(WHERESTR "recording entry\n", WHEREARG);
		if ((ent = (PointerEntry)malloc(sizeof(struct PointerEntryStruct))) == NULL)
			REPORT_ERROR("PPUEventHandler.c: malloc error");
		ent->data = retval;
		ent->id = id;
		ent->offset = offset;
		ent->size = size;
		ent->mode = type;
		
		pthread_mutex_lock(&pointer_mutex);
		ht_insert(pointers, retval, ent);
		pthread_mutex_unlock(&pointer_mutex);
	}	
}

//Perform a create in the current thread
void* threadCreate(GUID id, unsigned long size)
{
	struct createRequest* cr;
	struct acquireResponse* ar;
	void* retval;
	
	//printf(WHERESTR "creating structure\n", WHEREARG);
	//Create the request, this will be released by the coordinator
	if ((cr = (struct createRequest*)malloc(sizeof(struct createRequest))) == NULL)
		REPORT_ERROR("PPUEventHandler.c: malloc error");
	cr->packageCode = PACKAGE_CREATE_REQUEST;
	cr->requestID = 0;
	cr->dataItem = id;
	cr->dataSize = size == 0 ? 1 : size;
	
	retval = NULL;
	
	//Perform the request and await the response
	ar = (struct acquireResponse*)forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		printf(WHERESTR "response was negative\n", WHEREARG);
		if (ar->packageCode != PACKAGE_NACK)
			REPORT_ERROR("Unexcepted response for a Create request");
	}
	else
	{
		//printf(WHERESTR "response was positive\n", WHEREARG);
		//The response was positive
		retval = ar->data;
		#if DEBUG
		if (ar->dataSize != size)
			REPORT_ERROR("Bad size returned in create");
		if (ar->requestID != 0)
			REPORT_ERROR("Bad request ID returned in create");
		#endif
	}

	recordPointer(retval, id, size, 0, WRITE);	
	
	free(ar);
	return retval;	
}

void processInvalidates()
{
	struct invalidateRequest* req;
	queue temp;
	PointerEntry pe;
	hashtableIterator it;
	
	pthread_mutex_lock(&ppu_invalidate_mutex);
	if (!queue_empty(pendingInvalidate))
	{
		temp = queue_create();
		while(!queue_empty(pendingInvalidate))
		{
			req = queue_deq(pendingInvalidate);
			pthread_mutex_lock(&pointerOld_mutex);
			if (ht_member(pointersOld, (void*)req->dataItem))
			{
				pe = ht_get(pointersOld, (void*)req->dataItem);
				ht_delete(pointersOld, (void*)req->dataItem);
				pthread_mutex_unlock(&pointerOld_mutex);
				free(pe);
			}
			else
			{
				pthread_mutex_unlock(&pointerOld_mutex);
				
				pthread_mutex_lock(&pointer_mutex);
				it = ht_iter_create(pointers);
				while(ht_iter_next(it))
				{
					pe = ht_iter_get_value(it);
					if (pe->id == req->dataItem)
					{
						queue_enq(temp, req);
						req = NULL;
						break;
					}
				}
				ht_iter_destroy(it);
				pthread_mutex_unlock(&pointer_mutex);
			}
			
			if (req != NULL)
				free(req);
		}

		//Re-insert items
		while(!queue_empty(temp))
			queue_enq(pendingInvalidate, queue_deq(temp));
			
		queue_destroy(temp);
	}
	pthread_mutex_unlock(&ppu_invalidate_mutex);
}

//Perform an acquire in the current thread
void* threadAcquire(GUID id, unsigned long* size, int type)
{
	void* retval;
	struct acquireRequest* cr;
	struct acquireResponse* ar;
	
	// If acquire is of type read and id is in pointerOld, then we
	// reacquire, without notifying system.
	
	processInvalidates();
	
	pthread_mutex_lock(&pointerOld_mutex);
	if (ht_member(pointersOld, (void*)id)) {
		//printf(WHERESTR "Starting reacquire on id: %i\n", WHEREARG, id);
		PointerEntry pe = ht_get(pointersOld, (void*)id);
		ht_delete(pointersOld, (void*)id);
		pthread_mutex_unlock(&pointerOld_mutex);	
		
		//Reads can just return the copy
		if (type == READ) {
			//printf(WHERESTR "Full reacquire on id: %i\n", WHEREARG, id);
			pe->mode = type;
			pthread_mutex_lock(&pointer_mutex);
			ht_insert(pointers, pe->data, pe);
			pthread_mutex_unlock(&pointer_mutex);
			return pe->data;
		} else {
			//printf(WHERESTR "Re-acquire was for write, clearing local cache, id: %i\n", WHEREARG, id);
			free(pe);
		}			
	}
	else
		pthread_mutex_unlock(&pointerOld_mutex);
		
	//Create the request, this will be released by the coordinator	
	if ((cr = (struct acquireRequest*)malloc(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");

	if (type == WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	}
	else if (type == READ) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	}
	else
		REPORT_ERROR("Starting acquiring in unknown mode");
		
	cr->requestID = 0;
	cr->dataItem = id;

	retval = NULL;
		
	//Perform the request and await the response
	ar = (struct acquireResponse*)forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		REPORT_ERROR("Unexcepted response for an Acquire request");
		retval = NULL;
	}
	else
	{
		//printf(WHERESTR "Done acquiring id: %i\n", WHEREARG, id);
		//The request was positive
		retval = ar->data;
		(*size) = ar->dataSize;
		
		#if DEBUG
		if (ar->requestID != 0)
			REPORT_ERROR("Bad request ID returned in create");
		#endif
	
		recordPointer(retval, id, *size, 0, type);	
	}
	
	free(ar);
	return retval;	
}

//Perform a release on the current thread
void threadRelease(void* data)
{
	PointerEntry pe;
	struct releaseRequest* re;
	struct releaseResponse* rr;
	
	//Verify that the pointer is registered
	pthread_mutex_lock(&pointer_mutex);
	if (ht_member(pointers, data))
	{
		//Extract the pointer, and release the mutex fast
		pe = ht_get(pointers, data);
		ht_delete(pointers, data);
		pthread_mutex_unlock(&pointer_mutex);
		
		if (pe->mode == WRITE)
		{
			//Create a request, this will be released by the coordinator
			if ((re = (struct releaseRequest*)malloc(sizeof(struct releaseRequest))) == NULL)
				REPORT_ERROR("malloc error");
			re->packageCode = PACKAGE_RELEASE_REQUEST;
			re->requestID = 0;
			re->dataItem = pe->id;
			re->dataSize = pe->size;
			re->offset = pe->offset;
			re->data = data;
			re->mode = pe->mode;

			//TODO: What happens if there are multiple readers are trying to release the same item?
			pthread_mutex_lock(&pointerOld_mutex);
			ht_insert(pointersOld, (void*)pe->id, pe);
			pthread_mutex_unlock(&pointerOld_mutex);			
			
			//Perform the request and await the response
			rr = (struct releaseResponse*)forwardRequest(re);
			if(rr->packageCode != PACKAGE_RELEASE_RESPONSE)
				REPORT_ERROR("Reponse to release had unexpected type");
			
			free(rr);
		}
		else
		{
			pthread_mutex_lock(&pointerOld_mutex);
			ht_insert(pointersOld, (void*)pe->id, pe);
			pthread_mutex_unlock(&pointerOld_mutex);	
		}
		
		processInvalidates();
		
	}
	else
	{
		pthread_mutex_unlock(&pointer_mutex);
		REPORT_ERROR("Pointer given to release was not registered");
	}
	
	
}


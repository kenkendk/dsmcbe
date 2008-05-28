/*
 *
 * This module contains code that handles requests 
 * from PPU units
 *
 */

#include <pthread.h>
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
//This signals when an item has bee released
pthread_cond_t pointerOld_cond;

//These two tables contains the registered pointers, either active or retired
hashtable pointers, pointersOld;

//This is the queue of pending invalidates
queue pendingInvalidate;

#define BLOCKED (ACQUIRE_MODE_READ + ACQUIRE_MODE_WRITE + ACQUIRE_MODE_CREATE + 1)

typedef struct PointerEntryStruct *PointerEntry;
struct PointerEntryStruct
{
	GUID id;
	void* data;
	unsigned long offset;
	unsigned long size;	
	int mode;
	unsigned int count;
};



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
	pthread_cond_init(&pointerOld_cond, NULL);
	
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
		pe->data = NULL;
		free(pe);
		pe = NULL;
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
		pe->data = NULL;
		free(pe);
		pe = NULL;
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
	pthread_cond_destroy(&pointerOld_cond);
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
	if ((q = (QueueableItem)MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
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
	
	if (((struct acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireResponse*)data)->mode == ACQUIRE_MODE_WRITE) {
		while (queue_empty(dummy)) {
			//printf(WHERESTR "waiting for queue %i\n", WHEREARG, (int)&e);
			pthread_cond_wait(&e, &m);
			//printf(WHERESTR "queue filled\n", WHEREARG);
		}
		
		struct writebufferReady* data2 = queue_deq(dummy);
		pthread_mutex_unlock(&m);
		if (data2->packageCode != PACKAGE_WRITEBUFFER_READY)
			REPORT_ERROR("Expected PACKAGE_WRITEBUFFER_READY");
		
		FREE(data2);
		data2 = NULL;
	}

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
	
	if (type != ACQUIRE_MODE_READ && type != ACQUIRE_MODE_WRITE)
		REPORT_ERROR("pointer was neither READ nor WRITE");
	
	//If the response was valid, record the item data
	if (retval != NULL)
	{		
		pthread_mutex_lock(&pointer_mutex);
		
		if (ht_member(pointers, retval))
		{
			ent->count++;
		}
		else
		{
			//printf(WHERESTR "recording entry\n", WHEREARG);
			if ((ent = (PointerEntry)MALLOC(sizeof(struct PointerEntryStruct))) == NULL)
				REPORT_ERROR("PPUEventHandler.c: malloc error");
			ent->data = retval;
			ent->id = id;
			ent->offset = offset;
			ent->size = size;
			ent->mode = type;
			ent->count = 1;
		}
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
	
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return NULL;
	}
	
	//printf(WHERESTR "creating structure\n", WHEREARG);
	//Create the request, this will be released by the coordinator
	if ((cr = (struct createRequest*)MALLOC(sizeof(struct createRequest))) == NULL)
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

	recordPointer(retval, id, size, 0, ACQUIRE_MODE_WRITE);	
	
	free(ar);
	ar = NULL;
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
				if (pe->count == 0 && pe->mode != BLOCKED)
				{
					ht_delete(pointersOld, (void*)req->dataItem);
					// Send invalidateResponse
					QueueableItem q;
					struct invalidateResponse* cr;
					
					if ((cr = (struct invalidateResponse*)MALLOC(sizeof(struct invalidateResponse))) == NULL)
						REPORT_ERROR("malloc error");
	
					cr->packageCode = PACKAGE_INVALIDATE_RESPONSE;
					cr->requestID = req->requestID;
	
					if ((q = (QueueableItem)MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
						REPORT_ERROR("PPUEventHandler.c: malloc error");
					
					q->dataRequest = cr;
					q->event = NULL;
					q->mutex = NULL;
					q->queue = NULL;
					
					//printf(WHERESTR "adding item to queue\n", WHEREARG);
					EnqueItem(q);
					
					
					free(pe);
					pe = NULL;
				}
				else
					queue_enq(temp, req);
				pthread_mutex_unlock(&pointerOld_mutex);
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
			
			if (req != NULL) {
				free(req);
				req = NULL;
			}
		}

		//Re-insert items
		while(!queue_empty(temp))
			queue_enq(pendingInvalidate, queue_deq(temp));
			
		queue_destroy(temp);
	}
	pthread_mutex_unlock(&ppu_invalidate_mutex);
}

int isPendingInvalidate(GUID id)
{
	list *l;
	
	if (queue_empty(pendingInvalidate))
		return 0;
	
	l = &pendingInvalidate->head;
	
	while (l != NULL)
		if ((*l)->element == (void*)id)
			return 1;
		else
			l = &(*l)->next;
			
	return 0; 
	
}

//Perform an acquire in the current thread
void* threadAcquire(GUID id, unsigned long* size, int type)
{
	void* retval;
	struct acquireRequest* cr;
	struct acquireResponse* ar;
	PointerEntry pe;
	
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return NULL;
	}
	
	// If acquire is of type read and id is in pointerOld, then we
	// reacquire, without notifying system.
	
	processInvalidates();
	
	pthread_mutex_lock(&pointerOld_mutex);
	if (ht_member(pointersOld, (void*)id)) {
		//printf(WHERESTR "Starting reacquire on id: %i\n", WHEREARG, id);
		PointerEntry pe = ht_get(pointersOld, (void*)id);
		if (type == ACQUIRE_MODE_READ && (pe->count == 0 || pe->mode == ACQUIRE_MODE_READ) && !isPendingInvalidate(id))
		{
			pe->mode = type;
			pe->count++;
			pthread_mutex_unlock(&pointerOld_mutex);	

			pthread_mutex_lock(&pointer_mutex);
			if (!ht_member(pointers, pe->data))
				ht_insert(pointers, pe->data, pe);
			pthread_mutex_unlock(&pointer_mutex);
			
			return pe->data;
		}
	}

	pthread_mutex_unlock(&pointerOld_mutex);
		
	//Create the request, this will be released by the coordinator	
	if ((cr = (struct acquireRequest*)MALLOC(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");

	if (type == ACQUIRE_MODE_WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	}
	else if (type == ACQUIRE_MODE_READ) {
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

		if (type == ACQUIRE_MODE_WRITE)
		{
			pthread_mutex_lock(&pointerOld_mutex);
			if (ht_member(pointersOld, (void*)id))
			{
				pe = ht_get(pointersOld, (void*)id);
				pe->mode = BLOCKED;
				
				while(pe->count != 0)
					pthread_cond_wait(&pointerOld_cond, &pointerOld_mutex);
			}
			pthread_mutex_unlock(&pointerOld_mutex);
		}	
	
		recordPointer(retval, id, *size, 0, type);
	}
	
	free(ar);
	ar = NULL;
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
		pthread_mutex_unlock(&pointer_mutex);
		
		if (pe->id == PAGE_TABLE_ID)
		{
			REPORT_ERROR("cannot request pagetable");
			return;
		}
		
		pthread_mutex_lock(&pointerOld_mutex);
		pe->count--;
					
		if (pe->count == 0)
		{
			pthread_mutex_unlock(&pointerOld_mutex);
			
			pthread_mutex_lock(&pointer_mutex);
			ht_delete(pointers, data);
			pthread_mutex_unlock(&pointer_mutex);
		}
		else
			pthread_mutex_unlock(&pointerOld_mutex);
		
		if (pe->mode == ACQUIRE_MODE_WRITE)
		{
			//Create a request, this will be released by the coordinator
			if ((re = (struct releaseRequest*)MALLOC(sizeof(struct releaseRequest))) == NULL)
				REPORT_ERROR("malloc error");
			re->packageCode = PACKAGE_RELEASE_REQUEST;
			re->requestID = 0;
			re->dataItem = pe->id;
			re->dataSize = pe->size;
			re->offset = pe->offset;
			re->data = data;
			re->mode = pe->mode;

			//Perform the request and await the response
			rr = (struct releaseResponse*)forwardRequest(re);
			if(rr->packageCode != PACKAGE_RELEASE_RESPONSE)
				REPORT_ERROR("Reponse to release had unexpected type");
			
			free(rr);
			rr = NULL;
		}

		pthread_mutex_lock(&pointerOld_mutex);
		pthread_cond_broadcast(&pointerOld_cond);
		pthread_mutex_unlock(&pointerOld_mutex);

		processInvalidates();
		
	}
	else
	{
		pthread_mutex_unlock(&pointer_mutex);
		REPORT_ERROR("Pointer given to release was not registered");
	}
}


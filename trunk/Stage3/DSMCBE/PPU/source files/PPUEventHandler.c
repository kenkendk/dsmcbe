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

pthread_mutex_t pointer_mutex_Write, pointer_mutex_Read, pointer_mutex_Old;
hashtable pointersWrite, pointersRead, pointersOld;

//Simple hashtable comparision
int cmplessint(void* a, void* b)
{
	return ((int)a) < ((int)b);
}

//Basic text book hash function
int cmphashfc(void* a, unsigned int count)
{
	return ((int)a) % count;
}

//Setup the PPUHandler
void InitializePPUHandler()
{
	pointersWrite = ht_create(10, cmplessint, cmphashfc);
	pthread_mutex_init(&pointer_mutex_Write, NULL);
	pointersRead = ht_create(10, cmplessint, cmphashfc);
	pthread_mutex_init(&pointer_mutex_Read, NULL);
	pointersOld = ht_create(10, cmplessint, cmphashfc);
	pthread_mutex_init(&pointer_mutex_Old, NULL);
}

//Terminate the PPUHandler and release all resources
void TerminatePPUHandler()
{
	//ht_free(pointers);
	pthread_mutex_destroy(&pointer_mutex_Write);
	pthread_mutex_destroy(&pointer_mutex_Read);
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
		perror("PPUEventHandler.c: malloc error");
	
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
	
	//queue_free(dummy);
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
			perror("PPUEventHandler.c: malloc error");
		ent->data = retval;
		ent->id = id;
		ent->offset = offset;
		ent->size = size;
		
		if (type == WRITE) {
			pthread_mutex_lock(&pointer_mutex_Write);
			ht_insert(pointersWrite, retval, ent);
			pthread_mutex_unlock(&pointer_mutex_Write);
		} else if (type == READ) {
			pthread_mutex_lock(&pointer_mutex_Read);
			ht_insert(pointersRead, retval, ent);
			pthread_mutex_unlock(&pointer_mutex_Read);
		}
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
		perror("PPUEventHandler.c: malloc error");
	cr->packageCode = PACKAGE_CREATE_REQUEST;
	cr->requestID = 0;
	cr->dataItem = id;
	cr->dataSize = size;
	
	retval = NULL;
	
	//Perform the request and await the response
	ar = (struct acquireResponse*)forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		printf(WHERESTR "response was negative\n", WHEREARG);
		if (ar->packageCode != PACKAGE_NACK)
			perror("Unexcepted response for a Create request");
	}
	else
	{
		//printf(WHERESTR "response was positive\n", WHEREARG);
		//The response was positive
		retval = ar->data;
		#if DEBUG
		if (ar->dataSize != size)
			perror("Bad size returned in create");
		if (ar->requestID != 0)
			perror("Bad request ID returned in create");
		#endif
	}

	recordPointer(retval, id, size, 0, WRITE);	
	
	free(ar);
	return retval;	
}

//Perform an acquire in the current thread
void* threadAcquire(GUID id, unsigned long* size, int type)
{
	void* retval;
	
	struct acquireRequest* cr;

	struct acquireResponse* ar;
	
	// If acquire is of type read and id is in pointerOld, then we
	// reacquire, whitout notifying system.
	
	pthread_mutex_lock(&pointer_mutex_Old);
	if (type == READ && ht_member(pointersOld, (void*)id)) {
		printf(WHERESTR "Starting reacquire on id: %i\n", WHEREARG, id);
		PointerEntry pe = ht_get(pointersOld, (void*)id);
		ht_delete(pointersOld, (void*)id);
		pthread_mutex_unlock(&pointer_mutex_Old);	
		
		pthread_mutex_lock(&pointer_mutex_Read);
		ht_insert(pointersRead, pe->data, pe);
		pthread_mutex_unlock(&pointer_mutex_Read);
		return pe->data;	
	}
	pthread_mutex_unlock(&pointer_mutex_Old);
		
	//Create the request, this will be released by the coordinator	
	if ((cr = (struct acquireRequest*)malloc(sizeof(struct acquireRequest))) == NULL)
		perror("PPUEventHandler.c: malloc error");

	if (type == WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	}
	else if (type == READ) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	}
	else
		perror("Starting acquiring in unknown mode");
		
	cr->requestID = 0;
	cr->dataItem = id;

	retval = NULL;
		
	//Perform the request and await the response
	ar = (struct acquireResponse*)forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		if (ar->packageCode != PACKAGE_NACK)
			perror("Unexcepted response for a Create request");
	}
	else
	{
		//The request was positive
		retval = ar->data;
		(*size) = ar->dataSize;
		
		#if DEBUG
		if (ar->requestID != 0)
			perror("Bad request ID returned in create");
		#endif
	}
	
	recordPointer(retval, id, *size, 0, type);	
	
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
	pthread_mutex_lock(&pointer_mutex_Write);
	if (ht_member(pointersWrite, data))
	{
		//Extract the pointer, and release the mutex fast
		pe = ht_get(pointersWrite, data);
		ht_delete(pointersWrite, data);
		pthread_mutex_unlock(&pointer_mutex_Write);
		
		//Create a request, this will be released by the coordinator
		if ((re = (struct releaseRequest*)malloc(sizeof(struct releaseRequest))) == NULL)
			perror("PPUEventHandler.c: malloc error");
		re->packageCode = PACKAGE_RELEASE_REQUEST;
		re->requestID = 0;
		re->dataItem = pe->id;
		re->dataSize = pe->size;
		re->offset = pe->offset;
		re->data = data;
		
		//The pointer data is no longer needed
		free(pe);
		
		//Perform the request and await the response
		rr = (struct releaseResponse*)forwardRequest(re);
		if(rr->packageCode != PACKAGE_RELEASE_RESPONSE)
			perror("Reponse to release had unexpected type");
		
		free(rr);
	}
	else
	{
		pthread_mutex_unlock(&pointer_mutex_Write);		
		pthread_mutex_lock(&pointer_mutex_Read);
		if (ht_member(pointersRead, data)) {
			//Extract the pointer, and release the mutex fast
			pe = ht_get(pointersRead, data);
			ht_delete(pointersRead, data);
			pthread_mutex_unlock(&pointer_mutex_Read);	
			
			pthread_mutex_lock(&pointer_mutex_Old);
			ht_insert(pointersOld, (void*)pe->id, pe);
			pthread_mutex_unlock(&pointer_mutex_Old);	
			
			//The pointer data is no longer needed
			//free(pe);
								
		} else {
			pthread_mutex_unlock(&pointer_mutex_Read);
			fprintf(stderr, WHERESTR "Pointer given to release was not registered", WHEREARG);
		}
				
		
	}
}


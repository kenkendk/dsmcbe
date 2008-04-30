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

pthread_mutex_t pointer_mutex;
hashtable pointers;

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
	pointers = ht_create(10, cmplessint, cmphashfc);
	pthread_mutex_init(&pointer_mutex, NULL);
}

//Terminate the PPUHandler and release all resources
void TerminatePPUHandler()
{
	//ht_free(pointers);
	pthread_mutex_destroy(&pointer_mutex);
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
	q = (QueueableItem)malloc(sizeof(struct QueueableItemStruct));
	
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
void recordPointer(void* retval, GUID id, unsigned long size, unsigned long offset)
{
	PointerEntry ent;
	
	//If the response was valid, record the item data
	if (retval != NULL)
	{
		//printf(WHERESTR "recording entry\n", WHEREARG);
		ent = (PointerEntry)malloc(sizeof(struct PointerEntryStruct));
		ent->data = retval;
		ent->id = id;
		ent->offset = offset;
		ent->size = size;

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
	cr = (struct createRequest*)malloc(sizeof(struct createRequest));
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

	recordPointer(retval, id, size, 0);	
	
	free(ar);
	return retval;	
}

//Perform an acquire in the current thread
void* threadAcquire(GUID id, unsigned long* size)
{
	struct acquireRequest* cr;
	void* retval;
	struct acquireResponse* ar;

	//Create the request, this will be released by the coordinator	
	cr = (struct acquireRequest*)malloc(sizeof(struct acquireRequest));
	cr->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
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
	
	recordPointer(retval, id, *size, 0);	
	
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
		
		//Create a request, this will be released by the coordinator
		re = (struct releaseRequest*)malloc(sizeof(struct releaseRequest));
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
		perror("Pointer given to release was not registered");
		pthread_mutex_unlock(&pointer_mutex);
	}
}


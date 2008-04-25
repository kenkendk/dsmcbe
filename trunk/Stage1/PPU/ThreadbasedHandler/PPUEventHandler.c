/*
 *
 * This module contains code that handles requests 
 * from PPU units
 *
 */
#include "PPUEventHandler.h"
#include "../datastructures.h"
#include "datapackages.h"
#include <pthread.h>
#include "RequestCoordinator.h"
#include <malloc.h>
#include <stdio.h>


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
	
	printf("PPUEventandler.c: creating item\n");
	//Create the entry, this will be released by the coordinator
	q = (QueueableItem)malloc(sizeof(struct QueueableItemStruct));
	
	dummy = queue_create();
	pthread_mutex_init(&m, NULL);
	pthread_cond_init(&e, NULL);
	q->dataRequest = data;
	q->event = &e;
	q->mutex = &m;
	q->queue = &dummy;
	
	printf("PPUEventandler.c: adding item to queue\n");
	EnqueItem(q);
	printf("PPUEventandler.c: item added to queue %i\n", (int)q);
	
	pthread_mutex_lock(&m);
	printf("PPUEventandler.c: locked %i\n", (int)&m);
	
	while (queue_empty(dummy)) {
		printf("PPUEventandler.c: waiting for queue %i\n", (int)&e);
		pthread_cond_wait(&e, &m);
		printf("PPUEventandler.c: queue filled\n");
	}
	
	data = queue_deq(dummy);
	pthread_mutex_unlock(&m);

	printf("PPUEventandler.c: returning response\n");
	
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
		printf("PPUEventandler.c: recording entry\n");
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
	
	printf("PPUEventandler.c: creating structure\n");
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
		printf("PPUEventandler.c: response was negative\n");
		if (ar->packageCode != PACKAGE_NACK)
			perror("Unexcepted response for a Create request");
	}
	else
	{
		printf("PPUEventandler.c: response was positive\n");
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


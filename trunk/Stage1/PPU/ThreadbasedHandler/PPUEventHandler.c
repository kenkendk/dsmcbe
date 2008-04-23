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

int cmplessint(void* a, void* b)
{
	return ((int)a) < ((int)b);
}

int cmphashfc(void* a, unsigned int count)
{
	return ((int)a) % count;
}


void InitializePPUHandler()
{
	pointers = ht_create(10, cmplessint, cmphashfc);
	pthread_mutex_init(&pointer_mutex, NULL);
}

void TerminatePPUHandler()
{
	//ht_free(pointers);
	pthread_mutex_destroy(&pointer_mutex);
}

void* forwardRequest(void* data)
{
	queue dummy;
	pthread_mutex_t m;
	pthread_cond_t e;
	QueueableItem q;
	
	q = (QueueableItem)malloc(sizeof(struct QueueableItemStruct));
	
	dummy = queue_create();
	pthread_mutex_init(&m, NULL);
	pthread_cond_init(&e, NULL);
	q->dataRequest = data;
	q->event = e;
	q->mutex = m;
	q->queue = dummy;
	
	EnqueItem(q);
	
	pthread_mutex_lock(&m);
	pthread_cond_wait(&e, &m);
	
	data = queue_deq(dummy);
	pthread_mutex_unlock(&m);
	
	//queue_free(dummy);
	pthread_mutex_destroy(&m);
	pthread_cond_destroy(&e);
	
	return data;
}
 
void* threadCreate(GUID id, unsigned long size)
{
	struct createRequest* cr;
	void* retval;
	struct acquireResponse* ar;
	PointerEntry ent;
	
	cr = (struct createRequest*)malloc(sizeof(struct createRequest));
	cr->packageCode = PACKAGE_CREATE_REQUEST;
	cr->requestID = 0;
	cr->dataItem = id;
	cr->dataSize = size;

	retval = NULL;
		
	ar = (struct acquireResponse*)forwardRequest(ar);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		if (ar->packageCode != PACKAGE_NACK)
			perror("Unexcepted response for a Create request");
	}
	else
	{
		retval = ar->data;
		#if DEBUG
		if (ar->dataSize != size)
			perror("Bad size returned in create");
		if (ar->requestID != 0)
			perror("Bad request ID returned in create");
		#endif
	}
	
	if (retval != NULL)
	{
		ent = (PointerEntry)malloc(sizeof(struct PointerEntryStruct));
		ent->data = retval;
		ent->id = id;
		ent->offset = 0;
		ent->size = size;

		pthread_mutex_lock(&pointer_mutex);
		ht_insert(pointers, retval, ent);
		pthread_mutex_unlock(&pointer_mutex);
	}	
	
	free(ar);
	return retval;	
}

void* threadAcquire(GUID id, unsigned long* size)
{
	struct acquireRequest* cr;
	void* retval;
	struct acquireResponse* ar;
	PointerEntry ent;
	
	cr = (struct acquireRequest*)malloc(sizeof(struct acquireRequest));
	cr->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	cr->requestID = 0;
	cr->dataItem = id;

	retval = NULL;
		
	ar = (struct acquireResponse*)forwardRequest(ar);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		if (ar->packageCode != PACKAGE_NACK)
			perror("Unexcepted response for a Create request");
	}
	else
	{
		retval = ar->data;
		(*size) = ar->dataSize;
		
		#if DEBUG
		if (ar->requestID != 0)
			perror("Bad request ID returned in create");
		#endif
	}	
	
	if (retval != NULL)
	{
		ent = (PointerEntry)malloc(sizeof(struct PointerEntryStruct));
		ent->data = retval;
		ent->id = id;
		ent->offset = 0;
		ent->size = (*size);

		pthread_mutex_lock(&pointer_mutex);
		ht_insert(pointers, retval, ent);
		pthread_mutex_unlock(&pointer_mutex);
	}	
	
	free(ar);
	return retval;	
}

void threadRelease(void* data)
{
	PointerEntry pe;
	struct releaseRequest* re;
	struct releaseResponse* rr;
	
	pthread_mutex_lock(&pointer_mutex);
	if (ht_member(pointers, data))
	{
		pe = ht_get(pointers, data);
		ht_delete(pointers, data);
		pthread_mutex_unlock(&pointer_mutex);
		
		re = (struct releaseRequest*)malloc(sizeof(struct releaseRequest));
		re->packageCode = PACKAGE_RELEASE_REQUEST;
		re->requestID = 0;
		re->dataItem = pe->id;
		re->dataSize = pe->size;
		re->packageCode = pe->offset;
		re->data = data;
		
		free(pe);
		
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


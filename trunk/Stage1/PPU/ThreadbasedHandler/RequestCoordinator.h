/*
 * 
 * This file contains declarations for communcating with the RequestCoordinator
 * 
 * 
 */

#include <malloc.h>
#include <malloc_align.h>
#include "../dsmcbe.h"
#include "../datastructures.h"
#include "datapackages.h"

#ifndef REQUESTCOORDINATOR_H_
#define REQUESTCOORDINATOR_H_

typedef struct QueueableItemStruct *QueueableItem;

//When interacting with the provider, the caller must use this structure
//The mutex is locked before appending the request to the queue
//The event is signaled once data has been written to the queue
//The requester must call free() on the reponse in the queue
struct QueueableItemStruct {
	pthread_mutex_t* mutex;
	pthread_cond_t* event;
	queue* queue;
	void* dataRequest;
};

typedef struct PointerEntryStruct *PointerEntry;
struct PointerEntryStruct
{
	GUID id;
	void* data;
	unsigned long offset;
	unsigned long size;	
};

//Requesters must call this function to interact with the coordinator
//The coordinator will free the QueueableItem and the request
//The requester must free the response
extern void EnqueItem(QueueableItem item);
extern void InitializeCoordinator();
extern void TerminateCoordinator(int force);


#endif /*REQUESTCOORDINATOR_H_*/

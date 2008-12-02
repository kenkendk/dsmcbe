/*
 * 
 * This file contains declarations for communcating with the RequestCoordinator
 * 
 * 
 */

#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include <glib.h>
#include "../../dsmcbe.h"
#include "../../common/datapackages.h"

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
	GQueue** Gqueue;
	void* dataRequest;
	void (*callback)(QueueableItem, void* data);
};

//Requesters must call this function to interact with the coordinator
//The coordinator will free the QueueableItem and the request
//The requester must free the response
extern void EnqueItem(QueueableItem item);
extern void InitializeCoordinator();
extern void TerminateCoordinator(int force);

//Responds to an invalidate with high priority
void EnqueInvalidateResponse(unsigned int requestNumber);

//Threads wishing to recieve invalidation notification must register/unregister with the two calls below
extern void RegisterInvalidateSubscriber(pthread_mutex_t* mutex, pthread_cond_t* event, GQueue** q, int network);
extern void UnregisterInvalidateSubscriber(GQueue** q);

#endif /*REQUESTCOORDINATOR_H_*/

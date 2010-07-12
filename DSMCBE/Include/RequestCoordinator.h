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
#include "dsmcbe.h"
#include "datapackages.h"

#ifndef REQUESTCOORDINATOR_H_
#define REQUESTCOORDINATOR_H_

typedef struct dsmcbe_QueueableItemStruct *QueueableItem;

typedef void (*dsmcbe_rc_callback)(QueueableItem, void* data);

//When interacting with the provider, the caller must use this structure
//The mutex is locked before appending the request to the queue
//The event is signaled once data has been written to the queue
//The requester must call free() on the reponse in the queue
struct dsmcbe_QueueableItemStruct {
	pthread_mutex_t* mutex;
	pthread_cond_t* event;
	GQueue** Gqueue;
	void* dataRequest;
	dsmcbe_rc_callback callback;
};

//Requesters must call this function to interact with the coordinator
//The coordinator will free the QueueableItem and the request
//The requester must free the response
extern void dsmcbe_rc_EnqueItem(QueueableItem item);
extern void dsmcbe_rc_initialize();
extern void dsmcbe_rc_terminate(int force);

//Responds to an invalidate with high priority
extern void dsmcbe_rc_EnqueInvalidateResponse(GUID id, unsigned int requestNumber);

//Threads wishing to recieve invalidation notification must register/unregister with the two calls below
extern void dsmcbe_rc_RegisterInvalidateSubscriber(pthread_mutex_t* mutex, pthread_cond_t* event, GQueue** q, int network);
extern void dsmcbe_rc_UnregisterInvalidateSubscriber(GQueue** q);

extern QueueableItem dsmcbe_rc_new_QueueableItem(pthread_mutex_t* mutex, pthread_cond_t* cond, GQueue** queue, void* dataRequest, dsmcbe_rc_callback callback);

OBJECT_TABLE_ENTRY_TYPE dsmcbe_rc_GetMachineID(GUID id);

#endif /*REQUESTCOORDINATOR_H_*/

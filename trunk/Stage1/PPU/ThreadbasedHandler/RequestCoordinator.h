/*
 * 
 * This file contains declarations for communcating with the RequestCoordinator
 * 
 * 
 */

#include <pthread.h>
#include "../datastructures.h"
#include "datapackages.h"

#ifndef REQUESTCOORDINATOR_H_
#define REQUESTCOORDINATOR_H_

typedef struct QueueableItemStruct *QueueableItem;
struct QueueableItemStruct {
	pthread_mutex_t mutex;
	pthread_cond_t event;
	queue queue;
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


extern void EnqueItem(QueueableItem item);
extern void InitializeCoordinator();
extern void TerminateCoordinator(int force);


#endif /*REQUESTCOORDINATOR_H_*/

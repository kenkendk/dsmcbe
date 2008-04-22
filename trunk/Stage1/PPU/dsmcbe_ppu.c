#include "dsmcbe_ppu.h"
#include <stdio.h>
#include <malloc_align.h>
#include <libspe2.h>
#include <free_align.h>
#include <pthread.h>

spe_context_ptr_t SPE;

typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{
	
	GUID id;
	void* EA;
	unsigned long size;
	queue waitqueue;
};


int lessint(void* a, void* b){
	
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	
	return ((int)a % count);
}

void ReplyAcquire(dataObject object, int requestID){
	
	spe_in_mbox_write(SPE, (void*)3,  sizeof(unsigned int), SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (void*)requestID,  sizeof(unsigned int), SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (void*)object->size,  sizeof(unsigned long), SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, object->EA,  sizeof(void*), SPE_MBOX_ALL_BLOCKING);
}

void ReplyRelease(int requestID){

	spe_in_mbox_write(SPE, (void*)3,  sizeof(unsigned int), SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (void*)requestID,  sizeof(unsigned int), SPE_MBOX_ALL_BLOCKING);	
}

void* create(GUID id, unsigned long size){

	// Allocate memory
	void* allocated = _malloc_align(size, 7);
	
	// Make datastructures for later use
	dataObject object = malloc(sizeof(struct dataObjectStruct));
	object->id = id;
	object->EA = allocated;
	object->size = size;
	object->waitqueue = queue_create();
	ht_insert(allocatedItems, (void*)id, object);
	
	// Acquire object
#if DEBUG
	void* acquired = acquire(id, &size)
	if (allocated != acquired){
		perror("Allocated pointer does't match acquired pointer");
		return NULL;
	}
#else
	allocated = acquire(id, &size);
#endif	
		
	// Return memory location
	return allocated;
}

void* acquire(int requestID){
	
	void* data;
	spe_out_mbox_read(SPE, data, 1);
	GUID id = (GUID)data;
	
	// Find "id" in allocatedItems
	if(ht_member(allocatedItems, (void*)id)){
		
		dataObject object = ht_get(allocatedItems, (void*)id);
		
		// Is "id" locked?
		if(queue_empty(object->waitqueue)){
			// This thread is the only one in the waitqueue and 
			// therefore does't need to create condition variable
			queue_enq(object->waitqueue, NULL);
			ReplyAcquire(object, requestID);			
		}else{
			// Object "ID" is locked. Therefore we need to setup 
			// and wait on condition variable to be signaled, 
			// before continuing
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			pthread_mutex_init(&mutex, NULL);
			pthread_cond_init (&cond, NULL);
			
			queue_enq(object->waitqueue, &cond);
			
			pthread_cond_wait(&cond, &mutex);
			
			// This thread is now the firste element of the waitqueue!			
			ReplyAcquire(object, requestID);
		} 
	}else{
		// Add ID to waitlist for object "ID"
	}
	
	// Return memory location 
	return NULL;
}

void release(int requestID){
	
	void* data;
	spe_out_mbox_read(SPE, data, 1);
	GUID id = (GUID)data;
	
	// Find "id" in allocatedItems
	if(ht_member(allocatedItems, (void*)id)){
		
		dataObject object = ht_get(allocatedItems, (void*)id);
	
		ReplyRelease(requestID);
		
		while(!queue_empty(object->waitqueue))
		{
			void* cond = queue_deq(object->waitqueue);
			if(cond != NULL)
			{
				pthread_cond_signal(cond);
				break;
			}
		}
	}
}


int main(int argc, char **argv) {
	if(argc != 2){
		perror("Not enough arguments to start thread");
		return -1;
	}
	
	SPE = (spe_context_ptr_t)argv[1];

	void* data;
	void* requestID;
	
	while(1){
		spe_out_mbox_read(SPE, data, 1);
		spe_out_mbox_read(SPE, requestID, 1);
		switch((int)data){
			// Acquire request
			case 1:
				acquire((int)requestID);
				break;			
			
			// Release request			
			case 5:
				release((int)requestID);
				break;
			
			//Unknown request
			default:
				perror("Recieved unknown request");
				break;
		}
	}
	
	//allocatedItems = ht_create(10, lessint, hashfc);
	return 0;	
}

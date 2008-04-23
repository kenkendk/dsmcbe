#include <free_align.h>
#include <malloc_align.h>
#include "dsmcbe_ppu.h"
#include <stdio.h>
#define DEBUG 1

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

void setup() {
	allocatedItems = ht_create(10, lessint, hashfc);
}


void ReplyAcquire(dataObject object, int requestID){
	unsigned int x;
	x = 3;
	spe_in_mbox_write(SPE, &x,  1, SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (unsigned int*)&requestID,  1, SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (unsigned int*)&object->size, 2, SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (unsigned int*)&object->EA, 1, SPE_MBOX_ALL_BLOCKING);
}

void ReplyRelease(int requestID){

	unsigned int x;
	x = 3;

	spe_in_mbox_write(SPE, &x,  1, SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, &requestID,  1, SPE_MBOX_ALL_BLOCKING);	
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
	void* acquired = acquire(id, 1);
	if (allocated != acquired){
		perror("Allocated pointer does't match acquired pointer");
		return NULL;
	}
#else
	allocated = acquire(id, 1);
#endif	
		
	// Return memory location
	return allocated;
}

void* acquire(GUID id, int requestID){
	
	// Find "id" in allocatedItems
	if(ht_member(allocatedItems, (void*)id)){
		printf("Item existed %i\n", id);
		
		dataObject object = ht_get(allocatedItems, (void*)id);
		
		// Is "id" locked?
		if(queue_empty(object->waitqueue)){
			printf("Item was not locked\n");
			// This thread is the only one in the waitqueue and 
			// therefore does't need to create condition variable
			queue_enq(object->waitqueue, NULL);
			if(requestID != 1)
				ReplyAcquire(object, requestID);
			
			return object->EA;			
		}else{
			printf("Item was locked\n");
			// Object "ID" is locked. Therefore we need to setup 
			// and wait on condition variable to be signaled, 
			// before continuing
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			pthread_mutex_init(&mutex, NULL);
			pthread_cond_init (&cond, NULL);
			
			queue_enq(object->waitqueue, &cond);
			
			printf("Waiting for release\n");
			pthread_cond_wait(&cond, &mutex);
			printf("Released!\n");
			
			// This thread is now the firste element of the waitqueue!			
			if(requestID != 1)
				ReplyAcquire(object, requestID);

			return object->EA;			
		} 
	}else{
		printf("Item %i did not exist\n", id);
		// Add ID to waitlist for object "ID"
	}
	
	// Return memory location 
	return NULL;
}

void release(GUID id, int requestID){

	// Find "id" in allocatedItems
	if(ht_member(allocatedItems, (void*)id)){
		
		dataObject object = ht_get(allocatedItems, (void*)id);

		if(requestID != 1)	
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

void* ppu_pthread_com_function(void* arg) {
	
	unsigned int data;
	unsigned int requestID;
	GUID id;
	
	printf("dsmcbe.c: dsmcbe communication thread started\n");
	printf("In listener loop: %i\n", SPE);
	
	SPE = (spe_context_ptr_t)arg;

	printf("dsmcbe.c: Waiting for incoming SPU communication\n");	
	while(1) {
		if (spe_out_mbox_status(SPE) != 0) {
			printf("dsmcbe.c: SPU communication started\n");
			spe_out_mbox_read(SPE, &data, 1);
			spe_out_mbox_read(SPE, &requestID, 1);
			printf("dsmcbe.c: recieved signal from SPU (%i, %i)\n", data, requestID);
			
			switch((int)data){
				// Acquire request
				case 1:
					printf("dsmcbe.c: recieved acquire from SPU\n");
					spe_out_mbox_read(SPE, &id, 1);
					printf("dsmcbe.c: acquire ID recieved from SPU is %i\n", id);
					data = acquire(id, (unsigned int)requestID);
					printf("dsmcbe.c: acquire completed returned EA %i\n", data);
					break;			
				
				// Release request			
				case 5:
					spe_out_mbox_read(SPE, &id, 1);
					release((GUID)id, (int)requestID);
					break;
				
				//Unknown request
				default:
					//perror("Recieved unknown request");
					break;
			}
		}
	}
	
	return 0;	
}

#include "dsmcbe_ppu.h"
#include <stdio.h>
#include <malloc_align.h>
#include <libspe2.h>
#include <free_align.h>

spe_context_ptr_t SPE;

typedef struct requesterStruct *Requester;

struct requesterStruct{
	
	int Type;
	void* ID;
};

typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{
	
	GUID id;
	void* EA;
	unsigned long size;
	queue waitlist;
};


int lessint(void* a, void* b){
	
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	
	return ((int)a % count);
}

void ReplyAcquire(dataObject object){
	
	spe_in_mbox_write(SPE, (void*)3,  sizeof(unsigned int), SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, (void*)object->size,  sizeof(unsigned long), SPE_MBOX_ALL_BLOCKING);
	spe_in_mbox_write(SPE, object->EA,  sizeof(void*), SPE_MBOX_ALL_BLOCKING);
}

void ReplyRelease(){
	
}

void* create(GUID id, unsigned long size){

	// Allocate memory
	void* allocated = _malloc_align(size, 7);
	
	// Make datastructures for later use
	dataObject object = malloc(sizeof(struct dataObjectStruct));
	object->id = id;
	object->EA = allocated;
	object->size = size;
	object->waitlist = queue_create();
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

void* acquire(GUID id, unsigned long* size){
	
	// Find "id" in allocatedItems
	if(ht_member(allocatedItems, (void*)id)){
		
		dataObject object = ht_get(allocatedItems, (void*)id);
		
		// Is "id" locked?
		if(queue_empty(object->waitlist)){
			//queue_enq();
			ReplyAcquire(object);			
		}else{
			//queue_enq();
			// wait for event
			ReplyAcquire(object);
		} 
	}else{
		// Add ID to waitlist for object "ID"
	}
	
	// Return memory location 
	return NULL;
}

void release(void* data){
	
}


int main(int argc, char **argv) {
	if(argc != 2){
		perror("Not enough arguments to start thread");
		return -1;
	}
	
	SPE = (spe_context_ptr_t)argv[1];

	void* data;
	
	while(1){
		spe_out_mbox_read(SPE, data, 1);
		
		//switch(data)
		//do stuff
	}
	
	//allocatedItems = ht_create(10, lessint, hashfc);
	return 0;	
}

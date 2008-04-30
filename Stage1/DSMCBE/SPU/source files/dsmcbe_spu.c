#include <stdio.h>
#include <spu_intrinsics.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_mfcio.h> 

#include "../../dsmcbe_spu.h"
#include "../../common/datapackages.h"
#include "../../common/datastructures.h"
#include "../header files/DMATransfer.h"
#include "../../common/debug.h"

static hashtable allocatedItems;

typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{	
	GUID id;
	void* EA;
	int data;
	unsigned long size;
};

int lessint(void* a, void* b){
	
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	
	return ((int)a % count);
}

void* acquire(GUID id, unsigned long* size) {
	
	int data;
	unsigned int transfer_size;
	
	if (allocatedItems == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return NULL;
	}
	
	printf(WHERESTR "Starting acquiring id: %i\n", WHEREARG, id);
	spu_write_out_mbox(PACKAGE_ACQUIRE_REQUEST_WRITE);
	spu_write_out_mbox(2);
	spu_write_out_mbox(id);

	data = spu_read_in_mbox();
	printf(WHERESTR "Message type: %i\n", WHEREARG, (int)data);
		
	data = spu_read_in_mbox();
	printf(WHERESTR "Request id: %i\n", WHEREARG, (int)data);
		
	*size = spu_read_in_mbox();
	printf(WHERESTR "Data size: %i\n", WHEREARG, (int)*size);
	
	data = spu_read_in_mbox();
	printf(WHERESTR "Data EA: %i\n", WHEREARG, (int)data);
	
	transfer_size = *size + ((16 - *size) % 16);
	void* allocation;
	if ((allocation = _malloc_align(transfer_size, 7)) == NULL)
		perror("Failed to allocate memory on SPU");		

	printf(WHERESTR "Allocation: %i\n", WHEREARG, (int)allocation);

	// Make datastructures for later use
	dataObject object;
	if ((object = malloc(sizeof(struct dataObjectStruct))) == NULL)
			perror("Failed to allocate memory on SPU");		
			
	object->id = id;
	object->EA = (void*)data;
	object->size = *size;
	ht_insert(allocatedItems, allocation, object);
	
	printf(WHERESTR "Starting DMA transfer\n", WHEREARG);
	StartDMAReadTransfer(allocation, (int)data, transfer_size, 0);
	
	printf(WHERESTR "Waiting for DMA transfer\n", WHEREARG);
	WaitForDMATransferByGroup(0);
	
	printf(WHERESTR "Finished DMA transfer\n", WHEREARG);
	printf(WHERESTR "Acquire completed id: %i\n", WHEREARG, id);
	
	return allocation;	
}

void release(void* data){
	
	unsigned int transfersize;
	
	if (allocatedItems == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return;
	}

	if (ht_member(allocatedItems, data)) {
		
		dataObject object = ht_get(allocatedItems, data);
		
		transfersize = object->size + ((16 - object->size) % 16);
		printf(WHERESTR "Release for id: %i\n", WHEREARG, object->id);
		StartDMAWriteTransfer(data, (int)object->EA, transfersize, 1);
		WaitForDMATransferByGroup(1);
		
		free_align(data);
	
		//printf(WHERESTR "Release DMA completed\n", WHEREARG);
		//lwsync();	
		spu_write_out_mbox(PACKAGE_RELEASE_REQUEST);
		spu_write_out_mbox(2);

		printf(WHERESTR "Release for id: %i\n", WHEREARG, object->id);

		spu_write_out_mbox(object->id);
		spu_write_out_mbox(object->size);
		
		spu_write_out_mbox((int)data);
		
		int result = spu_read_in_mbox();
		//printf(WHERESTR "Message type: %i\n", WHEREARG, result);
			
		result = spu_read_in_mbox();
		//printf(WHERESTR "Request id: %i\n", WHEREARG, result);
		
		ht_delete(allocatedItems, data);
	}
}

void initialize(){
	allocatedItems = ht_create(10, lessint, hashfc);
}


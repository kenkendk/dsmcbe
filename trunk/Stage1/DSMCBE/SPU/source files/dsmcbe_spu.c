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
	unsigned int tmp[2];
	
	printf(WHERESTR "Starting acquiring id: %i\n", WHEREARG, id);
	spu_write_out_mbox(PACKAGE_ACQUIRE_REQUEST_WRITE);
	spu_write_out_mbox(2);
	spu_write_out_mbox(id);

	data = spu_read_in_mbox();
	printf(WHERESTR "Message type: %i\n", WHEREARG, (int)data);
		
	data = spu_read_in_mbox();
	printf(WHERESTR "Request id: %i\n", WHEREARG, (int)data);
		
	tmp[0] = spu_read_in_mbox();
	tmp[1] = spu_read_in_mbox();
	*size = *((unsigned long*)tmp);
	printf(WHERESTR "Data size: %i\n", WHEREARG, (int)size);
	
	data = spu_read_in_mbox();
	printf(WHERESTR "Data EA: %i\n", WHEREARG, (int)data);
	
	void* allocation = _malloc_align(*size, 7);
	printf(WHERESTR "Memory allocated\n", WHEREARG);

	// Make datastructures for later use
	dataObject object = malloc(sizeof(struct dataObjectStruct));
	object->id = id;
	object->EA = (void*)data;
	object->size = *size;
	ht_insert(allocatedItems, allocation, object);
	
	
	printf(WHERESTR "Starting DMA transfer\n", WHEREARG);
	StartDMAReadTransfer(allocation, (int)data, *size, 0);
	
	printf(WHERESTR "Waiting for DMA transfer\n", WHEREARG);
	WaitForDMATransferByGroup(0);
	
	printf(WHERESTR "Finished DMA transfer\n", WHEREARG);
	
	return allocation;	
}

void release(void* data){
	
	if (ht_member(allocatedItems, data)) {
		
		dataObject object = ht_get(allocatedItems, data);
		
		StartDMAWriteTransfer(data, (int)object->EA, object->size, 1);
		WaitForDMATransferByGroup(1);
	
		spu_write_out_mbox(PACKAGE_RELEASE_REQUEST);
		spu_write_out_mbox(2);
		printf("spu.c: Release for id: %i\n", object->id);
		spu_write_out_mbox(object->id);
		
		spu_write_out_mbox(((unsigned int*)&object->size)[0]);
		spu_write_out_mbox(((unsigned int*)&object->size)[1]);
		
		spu_write_out_mbox((int)data);
		
		int result = spu_read_in_mbox();
		printf(WHERESTR "Message type: %i\n", WHEREARG, result);
			
		result = spu_read_in_mbox();
		printf(WHERESTR "Request id: %i\n", WHEREARG, result);
		
		ht_delete(allocatedItems, data);
	}
}

void initialize(){
	allocatedItems = ht_create(10, lessint, hashfc);
}


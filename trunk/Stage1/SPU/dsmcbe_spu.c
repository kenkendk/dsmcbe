#include "dsmcbe_spu.h"
#include <stdio.h>
#include <spu_intrinsics.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_mfcio.h> 
#include "datastructures.h"
#include "../PPU/guids.h"
#include "DMATransfer.h"

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
	
	printf("spu.c: Starting acquiring id: %i\n", id);
	spu_write_out_mbox(PACKAGE_ACQUIRE_REQUEST_WRITE);
	spu_write_out_mbox(2);
	spu_write_out_mbox(id);

	data = spu_read_in_mbox();
	printf("spu.c: Message type: %i\n", (int)data);
		
	data = spu_read_in_mbox();
	printf("spu.c: Request id: %i\n", (int)data);
		
	tmp[0] = spu_read_in_mbox();
	tmp[1] = spu_read_in_mbox();
	*size = *((unsigned long*)tmp);
	printf("spu.c: Data size: %i\n", (int)size);
	
	data = spu_read_in_mbox();
	printf("spu.c: Data EA: %i\n", (int)data);
	
	void* allocation = _malloc_align(*size, 7);
	printf("spu.c: Memory allocated\n");

	// Make datastructures for later use
	dataObject object = malloc(sizeof(struct dataObjectStruct));
	object->id = id;
	object->EA = (void*)data;
	object->size = *size;
	ht_insert(allocatedItems, allocation, object);
	
	
	printf("spu.c: Starting DMA transfer\n");
	StartDMAReadTransfer(allocation, (int)data, *size, 0);
	
	printf("spu.c: Waiting for DMA transfer\n");
	WaitForDMATransferByGroup(0);
	
	printf("spu.c: Finished DMA transfer\n");
	
	return allocation;	
}

void release(void* data){
	
	if (ht_member(allocatedItems, data)) {
		
		dataObject object = ht_get(allocatedItems, data);
		
		StartDMAWriteTransfer(data, (int)object->EA, object->size, 1);
		WaitForDMATransferByGroup(1);
	
		spu_write_out_mbox(PACKAGE_RELEASE_REQUEST);
		spu_write_out_mbox(2);
		spu_write_out_mbox(object->id);
		spu_write_out_mbox(object->size);
		spu_write_out_mbox((int)data);
		
		int result = spu_read_in_mbox();
		printf("spu.c: Message type: %i\n", result);
			
		result = spu_read_in_mbox();
		printf("spu.c: Request id: %i\n", result);
		
		ht_delete(allocatedItems, data);
	}
}

void setup(){
	allocatedItems = ht_create(10, lessint, hashfc);
}

int main(int argc, char **argv) {
	
	setup();
	
	int data = (int)spu_read_in_mbox();
	
	if(data == 1) {
		printf("spu.c: Hello World\n");
		unsigned long size;
		
		int* allocation = acquire(ETTAL+1, &size);
	
		printf("spu.c: Value read from acquire is: %i\n", *allocation);
		
		*allocation = 210;
				
		release(allocation);
		printf("spu.c: Release completed\n");
	}
	
	printf("spu.c: Done\n");
	
	return 0;
}

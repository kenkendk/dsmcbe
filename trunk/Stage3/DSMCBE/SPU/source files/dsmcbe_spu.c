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

static hashtable allocatedItemsWrite, allocatedItemsRead, allocatedItemsOld;
static queue allocatedID;

typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{	
	GUID id;
	void* EA;
	void* data;
	unsigned long size;
};

int lessint(void* a, void* b){
	
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	
	return ((int)a % count);
}

int clear() {	
	int id = (int)queue_deq(allocatedID);
	//printf(WHERESTR "Trying to clear id %i\n", WHEREARG, id);
	if(ht_member(allocatedItemsOld, (void*)id)) {
		dataObject object = ht_get(allocatedItemsOld, (void*)id);		
		ht_delete(allocatedItemsOld, (void*)id);
		free_align(object->data);
		free(object);
		//printf(WHERESTR "Item removed\n", WHEREARG);
		return 1;				
	} else
		//printf(WHERESTR "Item not found\n", WHEREARG);
	
	return 0;
}

void sendMailbox(void* dataItem, int packagetype) {
	switch(packagetype)
	{
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			spu_write_out_mbox(((struct acquireRequest*)dataItem)->packageCode);
			spu_write_out_mbox(((struct acquireRequest*)dataItem)->requestID);
			spu_write_out_mbox(((struct acquireRequest*)dataItem)->dataItem);
			break;
		
		case PACKAGE_RELEASE_REQUEST:
			spu_write_out_mbox(((struct releaseRequest*)dataItem)->packageCode);
			spu_write_out_mbox(((struct releaseRequest*)dataItem)->requestID);
			spu_write_out_mbox(((struct releaseRequest*)dataItem)->dataItem);
			spu_write_out_mbox(((struct releaseRequest*)dataItem)->dataSize);
			spu_write_out_mbox((int)((struct releaseRequest*)dataItem)->data);			
			break;
		
		case PACKAGE_INVALIDATE_RESPONSE:
			spu_write_out_mbox(((struct invalidateResponse*)dataItem)->packageCode);
			spu_write_out_mbox(((struct invalidateResponse*)dataItem)->requestID);
			break;
		
		default:
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, packagetype);
	}
	free(dataItem);	
}

void invalidate(struct invalidateRequest* item) {
	
	// Insert code here	
	struct invalidateResponse* resp;
	
	if ((resp = malloc(sizeof(struct invalidateResponse))) == NULL)
		perror("SPUEventHandler.c: malloc error");;
	
	resp->packageCode = PACKAGE_INVALIDATE_RESPONSE;
	resp->requestID = item->requestID;
	
	sendMailbox(resp, PACKAGE_INVALIDATE_RESPONSE);
	
	free(resp);
}

void* readMailbox() {
	void* dataItem;
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	GUID itemid;
	void* datapointer;
		
	int packagetype = spu_read_in_mbox();
	switch(packagetype)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			if ((dataItem = malloc(sizeof(struct acquireResponse))) == NULL)
				perror("SPUEventHandler.c: malloc error");;

			requestID = spu_read_in_mbox();
			datasize = spu_read_in_mbox();
			datapointer = (void*)spu_read_in_mbox();

			//printf(WHERESTR "Data EA: %i\n", WHEREARG, (int)data);			
			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 
			((struct acquireResponse*)dataItem)->dataSize = datasize;
			((struct acquireResponse*)dataItem)->data = datapointer;
			break;
		
		case PACKAGE_RELEASE_RESPONSE:
			if ((dataItem = malloc(sizeof(struct releaseResponse))) == NULL)
				perror("SPUEventHandler.c: malloc error");;

			requestID = spu_read_in_mbox();
			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 
			
			break;
		
		case PACKAGE_INVALIDATE_REQUEST:			
			printf(WHERESTR "INVALIDATE package recieved\n", WHEREARG);

			if ((dataItem = malloc(sizeof(struct invalidateRequest))) == NULL)
				perror("SPUEventHandler.c: malloc error");;

			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			
			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID;
			((struct acquireResponse*)dataItem)->requestID = itemid; 

			invalidate(dataItem);
			
			dataItem = readMailbox();			
			break;
		
		default:
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, datatype);
			fprintf(stderr, WHERESTR "Unknown package recieved", WHEREARG);
	};	
	
	return dataItem;
}

void* acquire(GUID id, unsigned long* size, int type) {
	
	void* allocation;
	unsigned int transfer_size;
	
	if (type == READ && ht_member(allocatedItemsOld, (void*)id)) {
		dataObject object = ht_get(allocatedItemsOld, (void*)id);		
		//printf(WHERESTR "ReAcquire for READ id: %i\n", WHEREARG, id);

		ht_delete(allocatedItemsOld, (void*)id);
		ht_insert(allocatedItemsRead, object->data, object);	
		
		queue temp = queue_create();
		GUID value;
		while(!queue_empty(allocatedID)) {
			value = (GUID)queue_deq(allocatedID);
			if(id != value) 
				queue_enq(temp, (void*)value);
		}
		queue_destroy(allocatedID);
		allocatedID = temp;
		
		return object->data;
	}
	
	struct acquireRequest* request;
	if ((request = malloc(sizeof(struct acquireRequest))) == NULL)
		perror("SPUEventHandler.c: malloc error");

	if (allocatedItemsWrite == NULL || allocatedItemsRead == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return NULL;
	}
	
	if (type == WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		//spu_write_out_mbox(PACKAGE_ACQUIRE_REQUEST_WRITE);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	} else if (type == READ) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		//spu_write_out_mbox(PACKAGE_ACQUIRE_REQUEST_READ);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	} else {
		perror("Starting acquiring in unknown mode");
		return NULL;
	}
	
	request->requestID = 2;
	request->dataItem = id;
	
	sendMailbox(request, request->packageCode);

	struct acquireResponse* resp = readMailbox();
	
//	printf(WHERESTR "Message type: %i\n", WHEREARG, (int)resp->packageCode);		
//	printf(WHERESTR "Request id: %i\n", WHEREARG, (int)resp->requestID);		
//	printf(WHERESTR "Data size: %i\n", WHEREARG, (int)resp->dataSize);	
//	printf(WHERESTR "Data EA: %i\n", WHEREARG, (int)resp->data);
	
	*size = resp->dataSize;
	
	transfer_size = *size + ((16 - *size) % 16);

	while((allocation = _malloc_align(transfer_size, 7)) == NULL) {
		if (clear() == 0) {
			fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);
			break;
		}
	}
	
	//printf(WHERESTR "Allocation: %i\n", WHEREARG, (int)allocation);

	// Make datastructures for later use
	dataObject object;
	while((object = malloc(sizeof(struct dataObjectStruct))) == NULL) {
		if (clear() == 0) {
			fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);
			break;
		}
	}
			
	object->id = id;
	object->EA = resp->data;
	object->size = *size;
	object->data = allocation;
	if (type == WRITE)
		ht_insert(allocatedItemsWrite, allocation, object);
	else
		ht_insert(allocatedItemsRead, allocation, object);
	
	//printf(WHERESTR "Starting DMA transfer\n", WHEREARG);
	StartDMAReadTransfer(allocation, (unsigned int)resp->data, transfer_size, 0);
	
	//printf(WHERESTR "Waiting for DMA transfer\n", WHEREARG);
	WaitForDMATransferByGroup(0);
	
	//printf(WHERESTR "Finished DMA transfer\n", WHEREARG);
	//printf(WHERESTR "Acquire completed id: %i\n", WHEREARG, id);		
	
	free(resp);
	
	return allocation;	
}

void release(void* data){
	
	unsigned int transfersize;
	
	if (allocatedItemsWrite == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return;
	}

	if (ht_member(allocatedItemsWrite, data)) {
		
		dataObject object = ht_get(allocatedItemsWrite, data);
		
		transfersize = object->size + ((16 - object->size) % 16);
		//printf(WHERESTR "Release for WRITE id: %i\n", WHEREARG, object->id);
		
		//printf(WHERESTR "Starting DMA transfer\n", WHEREARG);
		StartDMAWriteTransfer(data, (int)object->EA, transfersize, 1);
		//printf(WHERESTR "Waiting for DMA transfer\n", WHEREARG);
		WaitForDMATransferByGroup(1);
		//printf(WHERESTR "Finished DMA transfer\n", WHEREARG);
		
		free_align(data);
	
		struct releaseRequest* request;
		if ((request = malloc(sizeof(struct releaseRequest))) == NULL)
			perror("SPUEventHandler.c: malloc error");

		//printf(WHERESTR "Release DMA completed\n", WHEREARG);
		//lwsync();	
		//spu_write_out_mbox(PACKAGE_RELEASE_REQUEST);
		request->packageCode = PACKAGE_RELEASE_REQUEST; 
		//spu_write_out_mbox(2);
		request->requestID = 2;
		
		//printf(WHERESTR "Release for id: %i\n", WHEREARG, object->id);

		//spu_write_out_mbox(object->id);
		request->dataItem = object->id;		
		//spu_write_out_mbox(object->size);
		request->dataSize = object->size;
		
		//spu_write_out_mbox((int)data);
		request->data = data;
		sendMailbox(request, request->packageCode);
		
		struct releaseResponse* resp = readMailbox();
		free(resp);
	
//		printf(WHERESTR "Message type: %i\n", WHEREARG, resp->packageCode);
//		printf(WHERESTR "Request id: %i\n", WHEREARG, resp->requestID);
		
		ht_delete(allocatedItemsWrite, data);
		free(object);
	} else if (ht_member(allocatedItemsRead, data)) {

		dataObject object = ht_get(allocatedItemsRead, data);		
		//printf(WHERESTR "Release for READ id: %i\n", WHEREARG, object->id);

		ht_delete(allocatedItemsRead, data);
		ht_insert(allocatedItemsOld, (void*)object->id, object);
		queue_enq(allocatedID, (void*)object->id);
		
		// Later we will move data to list containing release read objects, instead
		// of freeing them. This can be used to make it possible to reacquire read data, 		 
		// whitout transfering data from main memory.
		//free_align(data);
	}
}


void initialize(){
	allocatedItemsWrite = ht_create(10, lessint, hashfc);
	allocatedItemsRead = ht_create(10, lessint, hashfc);
	allocatedItemsOld = ht_create(10, lessint, hashfc);
	allocatedID = queue_create();
}


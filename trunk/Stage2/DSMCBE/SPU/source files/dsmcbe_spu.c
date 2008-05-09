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

#define ASYNC_STATUS_ERROR -1
#define ASYNC_STATUS_REQUEST_SENT 1
#define ASYNC_STATUS_DMA_PENDING 2
#define ASYNC_STATUS_COMPLETE 3

//There are only 32 DMA tags avalible
#define MAX_DMA_GROUPS 32
//The SPU cannot hold more than a few unserviced requests
#define MAX_REQ_NO 100000

static hashtable allocatedItems;
static unsigned int requestNo = 1;
static int DMAGroupNo = 0;
static hashtable pending;
static hashtable memoryList;

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

void sendMailbox(void* dataItem) {
	switch(((struct releaseRequest*)dataItem)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			ht_insert(memoryList, (void*)((struct createRequest*)dataItem)->requestID, (void*)((struct createRequest*)dataItem)->dataItem);
			spu_write_out_mbox(((struct createRequest*)dataItem)->packageCode);
			spu_write_out_mbox(((struct createRequest*)dataItem)->requestID);
			spu_write_out_mbox(((struct createRequest*)dataItem)->dataItem);
			spu_write_out_mbox(((struct createRequest*)dataItem)->dataSize);
			break;			
		
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			ht_insert(memoryList, (void*)((struct acquireRequest*)dataItem)->requestID, (void*)((struct acquireRequest*)dataItem)->dataItem);
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
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, ((struct releaseRequest*)dataItem)->packageCode);
	}
	FREE(dataItem);	
}

void StartDMATransfer(struct acquireResponse* resp)
{
	void* allocation;
	unsigned int transfer_size;
	int dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	
	transfer_size = ALIGNED_SIZE(resp->dataSize);

	allocation = MALLOC_ALIGN(transfer_size, 7);
	if (allocation == NULL)
	{
		printf(WHERESTR "Pending fill: %d, memoryList fill: %d, allocated fill: %d\n", WHEREARG, pending->fill, memoryList->fill, allocatedItems->fill);
		fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);
	}
		
	//printf(WHERESTR "Poping ID response for %d \n", WHEREARG, resp->requestID); 
		
	GUID id = (GUID)ht_get(memoryList, (void*)resp->requestID);
	ht_delete(memoryList, (void*)resp->requestID);

	//printf(WHERESTR "Poping ID response for %d -> %d\n", WHEREARG, resp->requestID, id); 
	
	ht_insert(memoryList, (void*)resp->requestID, allocation);
	//printf(WHERESTR "Allocation: %i\n", WHEREARG, (int)allocation);

	// Make datastructures for later use
	dataObject object;
	object = MALLOC(sizeof(struct dataObjectStruct));
	if (object == NULL)
		fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);

	resp->requestID = dmaNo;

	object->id = id;
	object->EA = resp->data;
	object->size = resp->dataSize;
	object->data = allocation;
	ht_insert(allocatedItems, allocation, object);

	//printf(WHERESTR "Starting DMA transfer\n", WHEREARG);
	StartDMAReadTransfer(allocation, (unsigned int)resp->data, transfer_size, dmaNo);
}

void readMailbox() {
	void* dataItem;
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	void* datapointer;
	dataItem = NULL;
	
	int packagetype = spu_read_in_mbox();
	switch(packagetype)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			if ((dataItem = MALLOC(sizeof(struct acquireResponse))) == NULL)
				perror("SPUEventHandler.c: malloc error");;

			requestID = spu_read_in_mbox();
			datasize = spu_read_in_mbox();
			datapointer = (void*)spu_read_in_mbox();

			//printf(WHERESTR "Acquire response for %d \n", WHEREARG, requestID); 

			//printf(WHERESTR "Data EA: %i\n", WHEREARG, (int)data);			
			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 
			((struct acquireResponse*)dataItem)->dataSize = datasize;
			((struct acquireResponse*)dataItem)->data = datapointer;
			
			if (ht_member(pending, (void*)((struct acquireResponse*)dataItem)->requestID))
			{
				ht_delete(pending, (void*)((struct acquireResponse*)dataItem)->requestID);
				ht_insert(pending, (void*)((struct acquireResponse*)dataItem)->requestID, dataItem);
			}
			else
				fprintf(stderr, WHERESTR "Recieved a request with an unexpected requestID\n", WHEREARG);
			
			StartDMATransfer((struct acquireResponse*)dataItem);
			break;
		
		case PACKAGE_RELEASE_RESPONSE:
			if ((dataItem = MALLOC(sizeof(struct releaseResponse))) == NULL)
				perror("SPUEventHandler.c: malloc error");;

			requestID = spu_read_in_mbox();
			//printf(WHERESTR "Release response for %d \n", WHEREARG, requestID); 

			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID;
			
			if (ht_member(pending, (void*)((struct acquireResponse*)dataItem)->requestID))
			{
				ht_delete(pending, (void*)((struct acquireResponse*)dataItem)->requestID);
				ht_insert(pending, (void*)((struct acquireResponse*)dataItem)->requestID, dataItem);
			}
			else
				fprintf(stderr, WHERESTR "Recieved a request with an unexpected requestID\n", WHEREARG);
			 
			break;
		
		default:
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, datatype);
			fprintf(stderr, WHERESTR "Unknown package recieved", WHEREARG);
	};	
}

void initialize(){
	allocatedItems = ht_create(10, lessint, hashfc);
	pending = ht_create(10, lessint, hashfc);
	memoryList = ht_create(10, lessint, hashfc);
}

void terminate() {
	ht_destroy(allocatedItems);
	ht_destroy(pending);
	ht_destroy(memoryList);
	allocatedItems = pending = memoryList = NULL;
}

unsigned int beginCreate(GUID id, unsigned long size)
{
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);

	if (allocatedItems == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	struct createRequest* request;
	if ((request = MALLOC(sizeof(struct createRequest))) == NULL)
		perror("SPUEventHandler.c: malloc error");
	
	request->packageCode = PACKAGE_CREATE_REQUEST;
	request->requestID = nextId;
	request->dataSize = size;
	request->dataItem = id;
	
	sendMailbox(request);

	//printf(WHERESTR "Issued an create for %d, with req %d\n", WHEREARG, id, nextId); 
	
	ht_insert(pending, (void*)nextId, NULL);
	
	return nextId;
}

unsigned int beginAcquire(GUID id)
{
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);
	
	if (allocatedItems == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	struct acquireRequest* request;
	if ((request = MALLOC(sizeof(struct acquireRequest))) == NULL)
		perror("SPUEventHandler.c: malloc error");
	
	//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
	//spu_write_out_mbox(PACKAGE_ACQUIRE_REQUEST_WRITE);
	request->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	request->requestID = nextId;
	request->dataItem = id;
	
	sendMailbox(request);

	//printf(WHERESTR "Issued an acquire for %d, with req %d\n", WHEREARG, id, nextId); 

	ht_insert(pending, (void*)nextId, NULL);
	return nextId;
}

unsigned int beginRelease(void* data)
{
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);
	unsigned int transfersize;
	int dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	
	if (allocatedItems == NULL)
	{
		fprintf(stderr, WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	if (ht_member(allocatedItems, data)) {
		
		dataObject object = ht_get(allocatedItems, data);
		
		transfersize = ALIGNED_SIZE(object->size);
		//printf(WHERESTR "Release for WRITE id: %i\n", WHEREARG, object->id);
		
		//printf(WHERESTR "Starting DMA transfer\n", WHEREARG);
		StartDMAWriteTransfer(data, (int)object->EA, transfersize, dmaNo);
		//printf(WHERESTR "Waiting for DMA transfer\n", WHEREARG);
		
		struct releaseRequest* request;
		if ((request = MALLOC(sizeof(struct releaseRequest))) == NULL)
			perror("SPUEventHandler.c: malloc error");

		request->packageCode = PACKAGE_RELEASE_REQUEST; 
		request->requestID = dmaNo;
		request->dataItem = object->id;
		request->dataSize = object->size;
		request->offset = 0;
		
		ht_insert(pending, (void*)nextId, request);
		ht_insert(memoryList, (void*)nextId, object->data);

		//printf(WHERESTR "Issued a release for %d, with req %d\n", WHEREARG, object->id, nextId); 

		ht_delete(allocatedItems, data);
		FREE(object);
		
		
		return nextId;
	}
	else
	{
		fprintf(stderr, WHERESTR "Tried to release non allocated item\n", WHEREARG);
		return 0;
	}

}

int getAsyncStatus(unsigned int requestNo)
{
	if (!ht_member(pending, (void*)requestNo))
		return ASYNC_STATUS_ERROR;
	else
	{
		while (spu_stat_in_mbox() != 0)
			readMailbox();
		
		void* state = ht_get(pending, (void*)requestNo);
		if (state == NULL)
			return ASYNC_STATUS_REQUEST_SENT;
		else
		{
			if (((struct acquireRequest*)state)->packageCode == PACKAGE_RELEASE_RESPONSE)
				return ASYNC_STATUS_COMPLETE;
				
			if (!IsDMATransferGroupCompleted(((struct acquireRequest*)state)->requestID))
				return ASYNC_STATUS_DMA_PENDING;
			else
			{
				if (((struct acquireRequest*)state)->packageCode == PACKAGE_RELEASE_REQUEST)
				{
					//printf(WHERESTR "DMA transfer completed for %d\n", WHEREARG, requestNo); 
			
					ht_delete(pending, (void*)requestNo);
					FREE_ALIGN(ht_get(memoryList, (void*)requestNo));
					ht_delete(memoryList, (void*)requestNo);
					ht_insert(pending, (void*)requestNo, NULL);
					
					((struct acquireRequest*)state)->requestID = requestNo;
					sendMailbox(state);
					return ASYNC_STATUS_REQUEST_SENT;
				}
				else
				{
					return ASYNC_STATUS_COMPLETE;
				}
			}
		}
	}
}

void* endAsync(unsigned int requestNo, unsigned long* size)
{
	int state;
	void* dataItem;

	void* alloc;
	
	state = getAsyncStatus(requestNo);
	//printf(WHERESTR "Status for %d is %d\n", WHEREARG, requestNo, state); 

	while(state != ASYNC_STATUS_COMPLETE)
	{
		if (state == ASYNC_STATUS_ERROR)
		{
			size = NULL;
			return NULL;
		}
		else if(state == ASYNC_STATUS_REQUEST_SENT)
		{
			if (IsThreaded())
			{
				while(state == ASYNC_STATUS_REQUEST_SENT)
				{
					YieldThread();
					state = getAsyncStatus(requestNo);
				}
				//Avoid the extra read
				continue;
			}
			//Non-threaded, just block
			else
				readMailbox();
		}
		else if (state == ASYNC_STATUS_DMA_PENDING)
		{
			if (IsThreaded())
			{
				while(state == ASYNC_STATUS_DMA_PENDING)
				{
					YieldThread();
					state = getAsyncStatus(requestNo);
				}
				//Avoid the extra read
				continue;
			}
			//Non-threaded, just block
			else
			{
				//printf(WHERESTR "Waiting for a DMA transfer...\n", WHEREARG);
				WaitForDMATransferByGroup(((struct acquireRequest*)ht_get(pending, (void*)requestNo))->requestID);
			}
		}

		state = getAsyncStatus(requestNo);
		//printf(WHERESTR "Status for %d is %d\n", WHEREARG, requestNo, state); 
		
	}

	dataItem = ht_get(pending, (void*)requestNo);
	ht_delete(pending, (void*)requestNo);

	
	switch(((struct acquireResponse*)dataItem)->packageCode)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			//printf(WHERESTR "Acquire response for %d \n", WHEREARG, requestNo); 
			alloc = ht_get(memoryList, (void*)requestNo);
			ht_delete(memoryList, (void*)requestNo);
			*size = ((struct acquireResponse*)dataItem)->dataSize;
			FREE(dataItem);
			if (!ht_member(allocatedItems, alloc))
				fprintf(stderr, WHERESTR "Newly acquired item was not registered\n", WHEREARG);
			return alloc;
			break;
			
		case PACKAGE_RELEASE_RESPONSE:
			//printf(WHERESTR "Release response for %d \n", WHEREARG, requestNo); 
			FREE(dataItem);
			return 0;
			break;
		default:
			fprintf(stderr, WHERESTR "Invalid package response\n", WHEREARG);
			FREE(dataItem);
			size = NULL;
			return NULL;
			break;
	}
		
}
 
void* acquire(GUID id, unsigned long* size) {
	unsigned int req = beginAcquire(id);
	return (void*)endAsync(req, size);
}

void release(void* data){
	unsigned int req = beginRelease(data);
	endAsync(req, NULL);
}

void* create(GUID id, unsigned long size)
{
	unsigned int req = beginCreate(id, size);
	return (void*)endAsync(req, &size);
}

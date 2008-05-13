#include <stdio.h>
#include <spu_intrinsics.h>
#include <libmisc.h>
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

//This table keeps all active items, key is the pointer, data is a dataObject
static hashtable allocatedItems;

//This table keeps all items that have been loaded, but is not active, key is the GUID, data is a dataObject
static hashtable allocatedItemsOld;

//List of pending invalidate requests, key is GUID
static hashtable pendingInvalidate;  

//This is all IDs kept after release, sorted with last used first
static queue allocatedID;

//A running number to distinguish requests
static unsigned int requestNo = 1;
//A running number to distinguish DMA transfers
static int DMAGroupNo = 0;

//A list of pending requests. 
//If value is NULL a mailbox communication is expected
//If value is not NULL, the value is the package with a pending DMA transfer  
static hashtable pending;

typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{	
	GUID id;
	void* EA;
	void* data;
	unsigned long size;
	int mode;
};

typedef struct pendingRequestStruct *pendingRequest;

struct pendingRequestStruct
{
	dataObject object;
	void* request;
	GUID id;
	int dmaNo;
	unsigned long size;
	int state;
	int mode;
};

int lessint(void* a, void* b){
	
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	
	return ((int)a % count);
}

void elementsInQueue(struct queue* q) {
	int count = 0;
	while(!queue_empty(q)) {
		queue_deq(q);
		count++;
	}
	printf(WHERESTR "Queue contained %i elements\n", WHEREARG, count);
}

int clearSimple() {
	
	int id;
	
	//printf(WHERESTR "Trying to free memory\n", WHEREARG);
	
	if (!queue_empty(allocatedID)) {
		id = (int)queue_deq(allocatedID);
		//printf(WHERESTR "Trying to free id %i\n", WHEREARG, id);
	} else {
		//printf(WHERESTR "Failed to free memory, allocatedID is empty\n", WHEREARG);
		return 0;
	}
	if(ht_member(allocatedItemsOld, (void*)id)) {
		dataObject object = ht_get(allocatedItemsOld, (void*)id);
		ht_delete(allocatedItemsOld, (void*)id);
		//printf(WHERESTR "Free is called\n", WHEREARG);
		free_align(object->data);
		free(object);
		//printf(WHERESTR "Succes, free some memory\n", WHEREARG);		
		return 1;					
	} else {
		//printf(WHERESTR "Failed to free memory, allocatedID not found in allocatedItemsOld\n", WHEREARG);
		return 0;
	}
	//printf(WHERESTR "Failed to free memory\n", WHEREARG);	
	return 0;
}

int clear(unsigned long size) {	
	
	//printf(WHERESTR "Starting to free memory\n", WHEREARG);	
	unsigned long freedmemory = 0;

	while(freedmemory < size + 10) {
		int id;
		if (!queue_empty(allocatedID))
			id = (int)queue_deq(allocatedID);
		else
			return 0;
	
		//printf(WHERESTR "Trying to clear id %i\n", WHEREARG, id);		
		if(ht_member(allocatedItemsOld, (void*)id)) {
			dataObject object = ht_get(allocatedItemsOld, (void*)id);
			ht_delete(allocatedItemsOld, (void*)id);
			freedmemory += (object->size + sizeof(struct dataObjectStruct));
			FREE_ALIGN(object->data);
			FREE(object);				
		}
	}
	
	return 1;
}

void sendMailbox(void* dataItem) {
	switch(((struct releaseRequest*)dataItem)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			spu_write_out_mbox(((struct createRequest*)dataItem)->packageCode);
			spu_write_out_mbox(((struct createRequest*)dataItem)->requestID);
			spu_write_out_mbox(((struct createRequest*)dataItem)->dataItem);
			spu_write_out_mbox(((struct createRequest*)dataItem)->dataSize);
			break;			
		
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
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, ((struct releaseRequest*)dataItem)->packageCode);
	}
}

void sendInvalidateResponse(struct invalidateRequest* item) {
	printf(WHERESTR "Sending invalidateResponse on data with id: %i\n", WHEREARG, item->dataItem);
	struct invalidateResponse* resp;
	
	if ((resp = MALLOC(sizeof(struct invalidateResponse))) == NULL)
		fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);
	
	resp->packageCode = PACKAGE_INVALIDATE_RESPONSE;
	resp->requestID = item->requestID;
	
	sendMailbox(resp);
}

void invalidate(struct invalidateRequest* item) {
	
	printf(WHERESTR "Trying to invalidate data with id: %i\n", WHEREARG, item->dataItem);
	
	GUID id = item->dataItem;
	
	if(ht_member(allocatedItemsOld, (void*)id)) {
		printf(WHERESTR "Data with id: %i is allocated but has been released\n", WHEREARG, id);
		dataObject object = ht_get(allocatedItemsOld, (void*)id);
		//printf(WHERESTR "ReAcquire for READ id: %i\n", WHEREARG, id);

		ht_delete(allocatedItemsOld, (void*)id);
		
		queue temp = queue_create();
		GUID value;
		while(!queue_empty(allocatedID)) {
			value = (GUID)queue_deq(allocatedID);
			if(id != value) 
				queue_enq(temp, (void*)value);
		}
		queue_destroy(allocatedID);
		allocatedID = temp;
		
		FREE_ALIGN(object->data);
		FREE(object);			
	} else if(ht_member(pendingInvalidate, (void*)id)) {
		printf(WHERESTR "Data with id: %i is allocated and acquired\n", WHEREARG, id);

		// Put in list and don't send reponse to invalidate, before release has been called.
		if (!ht_member(pendingInvalidate, (void*)id))
			ht_insert(pendingInvalidate, (void*)id, item);
		else
		{
			fprintf(stderr, WHERESTR "Recieved another invalidateRequest for the same item\n", WHEREARG);
			FREE(item);
		}				
		
		return;
	}
	else {
		printf(WHERESTR "Data with id: %i not allocated anymore\n", WHEREARG, id);
	}
	
	// Ready to send response!
	//sendInvalidateResponse(item);
}

void StartDMATransfer(struct acquireResponse* resp)
{
	unsigned int transfer_size;
	pendingRequest req = (pendingRequest)ht_get(pending, (void*)resp->requestID);
	
	//printf(WHERESTR "Processing ACQUIRE package for %d (IsThreaded: %d)\n", WHEREARG, req->id);
	
	if (ht_member(allocatedItemsOld, (void*)(req->id))) {
		req->object = (dataObject)ht_get(allocatedItemsOld, (void*)(req->id));
		ht_delete(allocatedItemsOld, (void*)(req->id));
		//printf(WHERESTR "Item %d (%d) was known, returning local copy\n", WHEREARG, req->id, (int)req->object->data);

		req->object->mode = req->mode;
		req->state = ASYNC_STATUS_COMPLETE;
		
		if (ht_member(allocatedItems, req->object->data))
			fprintf(stderr, WHERESTR "Re-acquried existing item %d\n", WHEREARG, req->id);
			
		ht_insert(allocatedItems, req->object->data, req->object);
		//printf(WHERESTR "Item %d is now inserted into allocatedItems\n", WHEREARG, req->id);
		return;
	}

 	req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	req->state = ASYNC_STATUS_DMA_PENDING;
			
	//printf(WHERESTR "Allocation: %i\n", WHEREARG, (int)allocation);

	// Make datastructures for later use
	req->object = MALLOC(sizeof(struct dataObjectStruct));
	if (req->object == NULL)
		fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);

	req->object->id = req->id;
	req->object->EA = resp->data;
	req->object->size = resp->dataSize;
	req->object->mode = req->mode;
	
	req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	req->size = req->object->size;

	//printf(WHERESTR "Item %d was not known, starting DMA transfer\n", WHEREARG, req->id);
	transfer_size = ALIGNED_SIZE(resp->dataSize);

	req->object->data = MALLOC_ALIGN(transfer_size, 7);
	if (req->object->data == NULL)
	{
		printf(WHERESTR "Pending fill: %d, pending invalidate: %d, allocatedItems: %d, allocatedItemsOld: %d\n", WHEREARG, pending->fill, pendingInvalidate->fill, allocatedItems->fill, allocatedItemsOld->fill);
		fprintf(stderr, WHERESTR "Failed to allocate memory on SPU\n", WHEREARG);
	}

	ht_insert(allocatedItems, req->object->data, req->object);
	//printf(WHERESTR "Registered dataobject with id: %d\n", WHEREARG, object->id);

	StartDMAReadTransfer(req->object->data, (unsigned int)resp->data, transfer_size, req->dmaNo);
}

void readMailbox() {
	void* dataItem;
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	GUID itemid;
	void* datapointer;
	pendingRequest req;
		
	int packagetype = spu_read_in_mbox();
	switch(packagetype)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			//printf(WHERESTR "ACQUIRE package recieved\n", WHEREARG);
			if ((dataItem = MALLOC(sizeof(struct acquireResponse))) == NULL)
				fprintf(stderr, WHERESTR "Failed to allocate memory on SPU\n", WHEREARG);

			requestID = spu_read_in_mbox();
			datasize = spu_read_in_mbox();
			datapointer = (void*)spu_read_in_mbox();

			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 
			((struct acquireResponse*)dataItem)->dataSize = datasize;
			((struct acquireResponse*)dataItem)->data = datapointer;

			if (ht_member(pending, (void*)((struct acquireResponse*)dataItem)->requestID))
			{
				req = ht_get(pending, (void*)((struct acquireResponse*)dataItem)->requestID);
				if (req->request != NULL)
					FREE(req->request);
				req->request = dataItem;
			}
			else
				fprintf(stderr, WHERESTR "Recieved a request with an unexpected requestID\n", WHEREARG);
			
			StartDMATransfer((struct acquireResponse*)dataItem);
			//printf(WHERESTR "Done with ACQUIRE package\n", WHEREARG);			
			break;
		
		case PACKAGE_RELEASE_RESPONSE:
			//printf(WHERESTR "RELEASE package recieved\n", WHEREARG);
			if ((dataItem = MALLOC(sizeof(struct releaseResponse))) == NULL)
				fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);

			requestID = spu_read_in_mbox();
			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 

			if (ht_member(pending, (void*)((struct acquireResponse*)dataItem)->requestID))
			{
				req = ht_get(pending, (void*)((struct acquireResponse*)dataItem)->requestID);
				if (req->request != NULL)
					FREE(req->request);
				req->request = dataItem;
				req->state = ASYNC_STATUS_COMPLETE;
			}
			else
				fprintf(stderr, WHERESTR "Recieved a request with an unexpected requestID\n", WHEREARG);

			break;
		
		case PACKAGE_INVALIDATE_REQUEST:			
			printf(WHERESTR "INVALIDATE package recieved\n", WHEREARG);

			if ((dataItem = MALLOC(sizeof(struct invalidateRequest))) == NULL)
				fprintf(stderr, WHERESTR "Failed to allocate memory on SPU", WHEREARG);

			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			
			((struct invalidateRequest*)dataItem)->packageCode = packagetype;									
			((struct invalidateRequest*)dataItem)->requestID = requestID;
			((struct invalidateRequest*)dataItem)->dataItem = itemid; 

			invalidate(dataItem);
			break;
		
		default:
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, datatype);
			fprintf(stderr, WHERESTR "Unknown package recieved", WHEREARG);
	};	
}

unsigned int beginCreate(GUID id, unsigned long size)
{
	pendingRequest req;
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);

	if (allocatedItemsOld == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	if ((req = MALLOC(sizeof(struct pendingRequestStruct))) == NULL)
		fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);

	struct createRequest* request;
	if ((request = MALLOC(sizeof(struct createRequest))) == NULL)
		fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);
	
	request->packageCode = PACKAGE_CREATE_REQUEST;
	request->requestID = nextId;
	request->dataSize = size;
	request->dataItem = id;
	
	sendMailbox(request);
	
	req->id = id;
	req->object = NULL;
	req->request = request;
	req->state = ASYNC_STATUS_REQUEST_SENT;
	req->mode = WRITE;

	//printf(WHERESTR "Issued an create for %d, with req %d\n", WHEREARG, id, nextId); 
	
	ht_insert(pending, (void*)nextId, req);
	if (!ht_member(pending, (void*)nextId))
		fprintf(stderr, WHERESTR "Failed to insert item in pending list\n", WHEREARG);
	
	return nextId;
}

unsigned int beginAcquire(GUID id, int type)
{
	pendingRequest req;
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);
	
	if (allocatedItemsOld == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	while (spu_stat_in_mbox() > 0)
		readMailbox();

	if ((req = MALLOC(sizeof(struct pendingRequestStruct))) == NULL)
		fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);

	if (ht_member(allocatedItemsOld, (void*)id))
	{
		dataObject object = ht_get(allocatedItemsOld, (void*)id);
		if (type == READ)
		{	
			//printf(WHERESTR "Reacquire for READ id: %i\n", WHEREARG, id);
	
			object->mode = type;
			ht_delete(allocatedItemsOld, (void*)id);
			ht_insert(allocatedItems, object->data, object);
	
			queue temp = queue_create();
			GUID value;
			while(!queue_empty(allocatedID)) {
				value = (GUID)queue_deq(allocatedID);
				if(id != value) 
					queue_enq(temp, (void*)value);
			}
			queue_destroy(allocatedID);
			allocatedID = temp;
	
			req->id = id;
			req->object = object;
			req->request = NULL;
			req->size = object->size;
			req->state = ASYNC_STATUS_COMPLETE;
			req->mode = type;
			
			ht_insert(pending, (void*)nextId, req);
			return nextId;
		}		
	}
	
	struct acquireRequest* request;
	if ((request = MALLOC(sizeof(struct acquireRequest))) == NULL)
		perror("SPUEventHandler.c: malloc error");
	
	if (type == WRITE)
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	else if (type == READ)
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	else {
		perror("Starting acquiring in unknown mode");
		return 0;
	}
	request->requestID = nextId;
	request->dataItem = id;

	req->id = id;
	req->object = NULL;
	req->request = request;
	req->state = ASYNC_STATUS_REQUEST_SENT;
	req->mode = type;
	
	sendMailbox(request);

	ht_insert(pending, (void*)nextId, req);
	if (!ht_member(pending, (void*)nextId))
		fprintf(stderr, WHERESTR "Failed to insert item in pending list\n", WHEREARG);
	return nextId;
}

unsigned int beginRelease(void* data)
{
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);
	unsigned int transfersize;
	dataObject object;
	pendingRequest req;
	
	//printf(WHERESTR "Starting a release\n", WHEREARG); 
	
	if (allocatedItemsOld == NULL)
	{
		fprintf(stderr, WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	if ((req = MALLOC(sizeof(struct pendingRequestStruct))) == NULL)
		fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);

	if (ht_member(allocatedItems, data)) {
		object = ht_get(allocatedItems, data);

		if (object->mode == WRITE) {	
			//printf(WHERESTR "Starting a release for %d in write mode (ls: %d, data: %d)\n", WHEREARG, (int)object->id, (int)object->data, (int)data); 
			req->id = object->id;
			req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
			req->object = object;
			req->state = ASYNC_STATUS_DMA_PENDING;
			req->size = object->size;
			
			transfersize = ALIGNED_SIZE(object->size);
			StartDMAWriteTransfer(data, (int)object->EA, transfersize, req->dmaNo);

			//printf(WHERESTR "DMA release for %d in write mode\n", WHEREARG, object->id); 

			struct releaseRequest* request;
			if ((request = MALLOC(sizeof(struct releaseRequest))) == NULL)
				fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);
	
			request->packageCode = PACKAGE_RELEASE_REQUEST; 
			request->requestID = nextId;
			request->dataItem = object->id;
			request->dataSize = object->size;
			request->offset = 0;

			req->request = request;
			
			ht_delete(allocatedItems, data);
			ht_insert(allocatedItemsOld, (void*)object->id, object);
		} else if (object->mode == READ) {
			//printf(WHERESTR "Starting a release for %d in read mode (ls: %d, data: %d)\n", WHEREARG, (int)object->id, (int)object->data, (int)data); 

			ht_delete(allocatedItems, data);
			
			if (ht_member(pendingInvalidate, (void*)object->id)) {
				struct invalidateRequest* item;
				item = ht_get(pendingInvalidate, (void*)object->id);
				sendInvalidateResponse(item);
				FREE(item);
				ht_delete(pendingInvalidate, (void*)object->id);
			} else {
				//printf(WHERESTR "Local release for %d in read mode\n", WHEREARG, object->id); 
				ht_insert(allocatedItemsOld, (void*)object->id, object);
			}
			
			req->id = object->id;
			req->object = object;
			req->size = object->size;
			req->state = ASYNC_STATUS_COMPLETE;
			req->request = NULL;
			req->mode = READ;
		}
		 
		ht_insert(pending, (void*)nextId, req);
		if (!ht_member(pending, (void*)nextId))
			fprintf(stderr, WHERESTR "Failed to insert item in pending list\n", WHEREARG);
		
		return nextId;
	} else {
		fprintf(stderr, WHERESTR "Tried to release non allocated item\n", WHEREARG);
		return 0;
	}		
}

void initialize(){
	allocatedItems = ht_create(10, lessint, hashfc);
	allocatedItemsOld = ht_create(10, lessint, hashfc);
	pendingInvalidate = ht_create(10, lessint, hashfc);
	allocatedID = queue_create();
	pending = ht_create(10, lessint, hashfc);
}

void terminate() {
	ht_destroy(allocatedItems);
	ht_destroy(allocatedItemsOld);
	ht_destroy(pendingInvalidate);
	queue_destroy(allocatedID);
	ht_destroy(pending);
	allocatedItems = allocatedItemsOld = pending = pendingInvalidate = allocatedID = NULL;
}


int getAsyncStatus(unsigned int requestNo)
{
	pendingRequest req;
	
	//printf(WHERESTR "In AsyncStatus for %d\n", WHEREARG, requestNo);
	
	if (!ht_member(pending, (void*)requestNo))
		return ASYNC_STATUS_ERROR;
	else
	{
		while (spu_stat_in_mbox() != 0)
		{
			//printf(WHERESTR "Processing pending mailbox messages, in %d\n", WHEREARG, requestNo);
			readMailbox();
		}
	
		req = (pendingRequest)ht_get(pending, (void*)requestNo);
		
		if (req->state == ASYNC_STATUS_DMA_PENDING)
		{
			if (IsDMATransferGroupCompleted(req->dmaNo))
				req->state = ASYNC_STATUS_COMPLETE;
			if (req->request != NULL)
				if (req->state == ASYNC_STATUS_COMPLETE && ((struct acquireRequest*)req->request)->packageCode == PACKAGE_RELEASE_REQUEST)
				{
					req->state = ASYNC_STATUS_REQUEST_SENT;
					((struct acquireRequest*)req->request)->requestID = requestNo;
					sendMailbox(req->request);
					//printf(WHERESTR "Handling release status for %d\n", WHEREARG, requestNo);
				}		
		}	
		return req->state;
	}
}

void* endAsync(unsigned int requestNo, unsigned long* size)
{
	pendingRequest req;
	void* retval;
	
	if (getAsyncStatus(requestNo) == ASYNC_STATUS_ERROR)
	{
		fprintf(stderr, WHERESTR "RequestNo was not for a pending request\n", WHEREARG);
		return NULL;
	}
		
	req = ht_get(pending, (void*)requestNo);
	//printf(WHERESTR "In endAsync for: %d, initial state was: %d\n", WHEREARG, requestNo, req->state);
	
	while(req->state != ASYNC_STATUS_COMPLETE)
	{
		if (req->state == ASYNC_STATUS_ERROR)
		{
			size = NULL;
			return NULL;
		}
		else if(req->state == ASYNC_STATUS_REQUEST_SENT)
		{
			//printf(WHERESTR "Awaiting mailbox response\n", WHEREARG); 
			if (IsThreaded())
			{
				while(req->state == ASYNC_STATUS_REQUEST_SENT)
				{
					YieldThread();
					getAsyncStatus(requestNo);
				}
				//Avoid the extra read
				continue;
			}
			//Non-threaded, just block
			else
			{
				//printf(WHERESTR "Blocking on mailbox response\n", WHEREARG); 
				readMailbox();
				//printf(WHERESTR "Blocked on mailbox response\n", WHEREARG); 
			}
			
			//printf(WHERESTR "After waiting for mbox, response status was: %d\n", WHEREARG, req->state);
		}
		else if (req->state == ASYNC_STATUS_DMA_PENDING)
		{
			//printf(WHERESTR "Awaiting DMA transfer\n", WHEREARG); 
			if (IsThreaded())
			{
				while(req->state == ASYNC_STATUS_DMA_PENDING)
				{
					YieldThread();
					getAsyncStatus(requestNo);
				}
				//Avoid the extra read
				continue;
			}
			//Non-threaded, just block
			else
			{
				//printf(WHERESTR "Waiting for a DMA transfer...\n", WHEREARG);
				WaitForDMATransferByGroup(req->dmaNo);
			}
		}

		getAsyncStatus(requestNo);
		//printf(WHERESTR "Status for %d is %d\n", WHEREARG, requestNo, state); 
	}

	ht_delete(pending, (void*)requestNo);
	if (req->object == NULL)
	{
		size = NULL;
		retval = NULL;
	}
	else
	{
		*size = req->object->size;
		retval = req->object->data;
		//printf(WHERESTR "In AsyncStatus for %d, obj id: %d, obj data: %d\n", WHEREARG, requestNo, req->object->id, (int)req->object->data);
	}
	
	if (req->object != NULL)
	{
		if (!ht_member(allocatedItemsOld, (void*)req->id) && !ht_member(allocatedItems, req->object->data))
		{
			//printf(WHERESTR "Dataobject %d (%d) was not registered anymore\n", WHEREARG, req->id, (int)req->object->data); 
			FREE_ALIGN(req->object->data);
			FREE(req->object);
		}
	}

	if (req->request != NULL)
		FREE(req->request);

	FREE(req);
	
	//printf(WHERESTR "Pending fill: %d, count: %d\n", WHEREARG, pendingInvalidate->fill, pendingInvalidate->count);
	//printf(WHERESTR "In endAsync for %d, returning %d\n", WHEREARG, requestNo, (int)retval);	
	return retval;
}
 
void* acquire(GUID id, unsigned long* size, int type) {
	unsigned int req = beginAcquire(id, type);
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
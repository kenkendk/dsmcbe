#include <stdio.h>
#include <spu_intrinsics.h>
#include <libmisc.h>
#include <spu_mfcio.h>
#include <unistd.h>  

#include "../../dsmcbe_spu.h"
#include "../../common/datapackages.h"
#include "../../common/datastructures.h"
#include "../header files/DMATransfer.h"
#include "../../common/debug.h"

#define ASYNC_STATUS_ERROR -1
#define ASYNC_STATUS_REQUEST_SENT 1
#define ASYNC_STATUS_DMA_PENDING 2
#define ASYNC_STATUS_BLOCKED 3
#define ASYNC_STATUS_COMPLETE 4

#define BLOCKED (READ + WRITE + 1)

//#define REPORT_MALLOC(x) printf(WHERESTR "Malloc gave %d, balance: %d\n", WHEREARG, (int)x, ++balance);
//#define REPORT_FREE(x) printf(WHERESTR "Free'd %d, balance: %d\n", WHEREARG, (int)x, --balance);

//There are only 32 DMA tags avalible
#define MAX_DMA_GROUPS 32

//There can be no more than this many ongoing requests
#define MAX_PENDING_REQUESTS 10

//The number of unprocessed pending invalidates
#define MAX_PENDING_INVALIDATES 32

//This table keeps all loaded items, key is the pointer, data is a dataObject, duals with itemsById
static hashtable itemsByPointer;

//This table keeps all items that have been loaded, key is the GUID, data is a dataObject, duals with itemsByPointer
static hashtable itemsById;

//This is all IDs kept after release, sorted with last used first
static queue allocatedID;

//A sequential number to distinguish requests
static unsigned int requestNo = 0;

//A sequential number to distinguish DMA transfers
static unsigned int DMAGroupNo = 0;

//The map of used pending request
static unsigned int pendingMap = 0;

//The list of items requested for invalidate
static GUID pendingInvalidates[MAX_PENDING_INVALIDATES];

//The map of used pending invalidates
static unsigned int pendingInvalidateMap = 0; 

#define SETBIT(seqNo,maxNo,map) (map |= (1 << (seqNo = (seqNo % maxNo))))
#define GETBIT(seqNo,maxNo,map) ((map & (1 << (seqNo = (seqNo % maxNo)))) != 0) 
#define CLEARBIT(seqNo,maxNo,map) map = (map & (~(1 << (seqNo = (seqNo % maxNo))))) 

void sendMailbox();
void readMailbox();
void invalidate(GUID id);

typedef struct dataObjectStruct *dataObject;

struct dataObjectStruct{	
	GUID id;
	void* EA;
	void* data;
	unsigned long size;
	int mode;
	unsigned int count;
};

typedef struct pendingRequestStruct *pendingRequest;

struct pendingRequestStruct
{
	dataObject object;
	struct packageBuffer request;
	GUID id;
	int dmaNo;
	unsigned long size;
	int state;
	int mode;
};

static struct pendingRequestStruct pendingRequestBuffer[MAX_PENDING_REQUESTS];

int lessint(void* a, void* b){
	
	return ((int)a) < ((int)b);
}

int hashfc(void* a, unsigned int count){
	
	return ((int)a % count);
}

//This function looks in map for a zero bit, if found, flips it and returns the matching number
inline unsigned int findFreeItem(unsigned int* seq, unsigned int max, unsigned int* map)
{
	size_t i;

	//printf(WHERESTR "Args: %d, %d, %d\n", WHEREARG, *seq, max, *map);	
	
	//Optimal, the corresponding number fits
	if (!GETBIT(*seq, max, *map))
	{
		SETBIT(*seq, max, *map);
		return *seq;
	}
	else
	{
		for(i = 0; i < (max-1); i++)
		{
			(*seq)++;
			if (!GETBIT(*seq, max, *map))
			{
				SETBIT(*seq, max, *map);
				return *seq;
			}
			
		}
	}

	REPORT_ERROR("Failed to retrieve a sequence number, try to increase the avalible amount");
	return -1;
}

int pendingInvalidateContains(GUID id, int clear)
{
	size_t i;
	if (pendingInvalidateMap == 0)
		return -1;
		
	for(i = 0; i < MAX_PENDING_INVALIDATES; i++)
		if (GETBIT(i, MAX_PENDING_INVALIDATES, pendingInvalidateMap) && pendingInvalidates[i] == id)
		{
			printf(WHERESTR "Found a pending invalidate\n", WHEREARG);
			if (clear)
				CLEARBIT(i, MAX_PENDING_INVALIDATES, pendingInvalidateMap);
			return i;
		}
		
	return -1;
}

//We cannot free items if they are in transit
int pendingContains(dataObject obj)
{
	size_t i;
	for(i = 0; i < MAX_PENDING_REQUESTS; i++)
		if (GETBIT(i, MAX_PENDING_REQUESTS, pendingMap) && pendingRequestBuffer[i].object == obj)
			return 1;
	
	return 0; 
}

void processPendingInvalidate(GUID id)
{
	int index = pendingInvalidateContains(id, 1);
	if (index >= 0)
		invalidate(id);
}

void removeAllocatedID(GUID id)
{
	list tmp;
	tmp = allocatedID->head;
	
	if ((GUID)tmp->element == id)
	{
		queue_deq(allocatedID);
		return;
	}
	
	while(tmp->next != NULL)
	{
		if ((GUID)tmp->next->element == id)
		{
			tmp->next = cdr_and_free(tmp->next);
			break;
		}
		
		tmp = tmp->next;
	}
}

void unsubscribe(dataObject object)
{
	/* Sending the package directly reduces memory overhead, but is a little less maintainable */
	//printf(WHERESTR "Unregistering item with id: %d\n", WHEREARG, object->id);
	spu_write_out_mbox(PACKAGE_RELEASE_REQUEST);
	spu_write_out_mbox(MAX_PENDING_REQUESTS + 1); //Invalid number
	spu_write_out_mbox(object->id);
	spu_write_out_mbox(READ);
	spu_write_out_mbox(object->size);
	spu_write_out_mbox((int)object->EA);
}

void clean(GUID id)
{
	dataObject object;
	if (ht_member(itemsById, (void*)id))
	{
		object = ht_get(itemsById, (void*)id);
		if (object->count == 0 && !pendingContains(object))
		{
			ht_delete(itemsById, (void*)id);
			ht_delete(itemsByPointer, object->data);
			removeAllocatedID(id);
			//printf(WHERESTR "Removed id %d\n", WHEREARG, object->id);
			unsubscribe(object);
			pendingInvalidateContains(object->id, 1);

			FREE_ALIGN(object->data);
			object->data = NULL;
			FREE(object);
			object = NULL;
		}
	}
}

void* clearAlign(unsigned long size, int base) {	

	if (size == 0)
	{
		REPORT_ERROR("Called malloc align with size zero");	
		return NULL;
	}
	
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), itemsById->fill, totalfreed, (int)size);
		
	void* pointer = thread_malloc_align(size, base);
	unsigned long freedmemory = 0;
	int go = 0;
	list* cur = &allocatedID->head;
	
	//if (queue_count(allocatedID) != (itemsById->fill - itemsByPointer->fill))
	//	REPORT_ERROR("allocatedID and itemsById differ in size");
		
	
	while (pointer == NULL) {
		//printf(WHERESTR "Starting to free memory\n", WHEREARG);	
	
		while(freedmemory < size || go) {
			int id;
			
			go = 0;
			
			if (cur != NULL && (*cur) != allocatedID->tail)
				id = (GUID)(*cur)->element;
			else
				return thread_malloc_align(size, base);
		
			//printf(WHERESTR "Trying to clear id %i\n", WHEREARG, id);		
			if(ht_member(itemsById, (void*)id)) {
			
				dataObject object = ht_get(itemsById, (void*)id);
				if (object->count == 0 && !pendingContains(object))
				{
					unsubscribe(object);
					freedmemory += (object->size + sizeof(struct dataObjectStruct));
					ht_delete(itemsById, (void*)id);
					ht_delete(itemsByPointer, object->data);
	
					pendingInvalidateContains(object->id, 1);

					FREE_ALIGN(object->data);
					object->data = NULL;
					FREE(object);
					object = NULL;
					
					//printf(WHERESTR "Cleared id %i, count before %d\n", WHEREARG, id, queue_count(allocatedID));
					*cur = cdr_and_free(*cur);
					//printf(WHERESTR "Cleared id %i, count after %d\n", WHEREARG, id, queue_count(allocatedID));
					//sleep(1);
				}
				else
					cur = &((*cur)->next);
				//printf(WHERESTR "Cleared id %i\n", WHEREARG, id);
			} else {
				printf(WHERESTR "ID is %d (%d)\n", WHEREARG, id, queue_count(allocatedID));
				REPORT_ERROR("allocatedID not found in itemsById");
				sleep(10);
			}		
		}
		
		//printf(WHERESTR "callign malloc...\n", WHEREARG);		
		//printf(WHERESTR "Enough is free, trying malloc_align\n", WHEREARG);
		pointer = thread_malloc_align(size, base);
		//printf(WHERESTR "Enough is free, result %d\n", WHEREARG, (int)pointer);
		go = 1;
		//printf(WHERESTR "Dequeue...\n", WHEREARG);		
		
	}
	
	//printf(WHERESTR "Freed %i and allocated %i of memory\n", WHEREARG, (int)freedmemory, (int)size);
	//printf(WHERESTR "Alocated pointer %d\n", WHEREARG, (int)pointer);
	if ((unsigned int)pointer < 10000)
	{
		REPORT_ERROR("Pointer was broken!");
		sleep(10);
	}
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), itemsById->fill, totalfreed, (int)size);
	return pointer;
}

void sendMailbox(void* dataItem) {
	//printf(WHERESTR "Sending request with no %d\n", WHEREARG, ((struct createRequest*)dataItem)->requestID);
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
			spu_write_out_mbox(((struct releaseRequest*)dataItem)->mode);
			spu_write_out_mbox(((struct releaseRequest*)dataItem)->dataSize);
			spu_write_out_mbox((int)((struct releaseRequest*)dataItem)->data);			
			break;
		
		/*case PACKAGE_INVALIDATE_RESPONSE:
			spu_write_out_mbox(((struct invalidateResponse*)dataItem)->packageCode);
			spu_write_out_mbox(((struct invalidateResponse*)dataItem)->requestID);
			break;*/
		
		default:
			printf(WHERESTR "Unknown package code: %i\n", WHEREARG, ((struct releaseRequest*)dataItem)->packageCode);
	}
}

/*
void sendInvalidateResponse(struct invalidateRequest* item) {
	printf(WHERESTR "Sending invalidateResponse on data with id: %i\n", WHEREARG, item->dataItem);
	struct invalidateResponse* resp;
	
	if ((resp = MALLOC(sizeof(struct invalidateResponse))) == NULL)
		fprintf(stderr, WHERESTR "malloc error\n", WHEREARG);
	
	resp->packageCode = PACKAGE_INVALIDATE_RESPONSE;
	resp->requestID = item->requestID;
	
	sendMailbox(resp);
}*/

void invalidate(GUID id) {
	//printf(WHERESTR "Trying to invalidate data with id: %i\n", WHEREARG, item->dataItem);

	if(ht_member(itemsById, (void*)id)) {
		//printf(WHERESTR "Data with id: %i is allocated but has been released\n", WHEREARG, id);
		dataObject object = ht_get(itemsById, (void*)id);
		
		if (object->count == 0 && !pendingContains(object))
		{
			ht_delete(itemsById, (void*)id);
			ht_delete(itemsByPointer, object->data);
			removeAllocatedID(id);
			pendingInvalidateContains(id, 1);
			
			//printf(WHERESTR "Invalidated id %d\n", WHEREARG, object->id);
			FREE_ALIGN(object->data);
			object->data = NULL;
			FREE(object);		
			object = NULL;	
		}
		else
		{
			//printf(WHERESTR "Inserting a pending invalidate\n", WHEREARG);
			if (!pendingInvalidateContains(id, 0))
			{
				//printf(WHERESTR "Inserted a pending invalidate before: %d\n", WHEREARG, pendingInvalidateMap);
				pendingInvalidates[findFreeItem(0, MAX_PENDING_INVALIDATES, &pendingInvalidateMap)] = id;
				//printf(WHERESTR "Inserted a pending invalidate after: %d\n", WHEREARG, pendingInvalidateMap);
			}
		}
	}
	else	
	{
		printf(WHERESTR "Discarded invalidate message with id: %i\n", WHEREARG, id);
		pendingInvalidateContains(id, 1);
	}
	
}

void StartDMATransfer(struct acquireResponse* resp)
{
	unsigned int transfer_size;
	pendingRequest req = &pendingRequestBuffer[resp->requestID];

	if (!GETBIT(resp->requestID, MAX_PENDING_REQUESTS, pendingMap))
	{
		printf("Req id: %d\n", resp->requestID);
		REPORT_ERROR("Invalid request number");
	}

	if (spu_stat_in_mbox() > 0)
		readMailbox();	
	processPendingInvalidate(req->id);
	
	//printf(WHERESTR "Processing ACQUIRE package for %d, %d\n", WHEREARG, req->id, resp->requestID);
	
	if (ht_member(itemsById, (void*)(req->id))) {
		req->object = (dataObject)ht_get(itemsById, (void*)(req->id));
		removeAllocatedID(req->id);

		//Special case, this pending transfer is blocking the invalidate
		if (req->object->count == 0 && pendingInvalidateContains(req->id, 1)) {
			
			//printf(WHERESTR "Invalidate, last minute save :), %d - %d\n", WHEREARG, req->id, req->mode);
			ht_delete(itemsById, (void*)req->id);
			ht_delete(itemsByPointer, req->object->data);
			FREE_ALIGN(req->object->data);
			req->object->data = NULL;		
			FREE(req->object);
			req->object = NULL;		
			
		} else 	{
			//printf(WHERESTR "Removed id %d\n", WHEREARG, req->id);
			
			printf(WHERESTR "Re-used object %d in mode %d\n", WHEREARG, req->id, req->mode);
			
			if (req->mode == READ || req->object->count == 0)
			{
				//printf(WHERESTR "Item %d (%d) was known, returning local copy\n", WHEREARG, req->id, (int)req->object->data);
				req->object->count++;
				req->object->mode = req->mode;
				req->state = ASYNC_STATUS_COMPLETE;
			}
			else if (req->mode == WRITE)
			{
				printf(WHERESTR "Blocked object %d\n", WHEREARG, req->id);
				req->state = ASYNC_STATUS_BLOCKED;
				req->object->mode = BLOCKED;
			}
			else
			{
				REPORT_ERROR("Invalid mode detected");
				req->state = ASYNC_STATUS_ERROR;
			}
			
			return;
		}
	}

 	req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	req->state = ASYNC_STATUS_DMA_PENDING;
			
	//printf(WHERESTR "Allocation: %i\n", WHEREARG, (int)allocation);

	// Make datastructures for later use
	req->object = MALLOC(sizeof(struct dataObjectStruct));
	if (req->object == NULL)
		REPORT_ERROR("Failed to allocate memory on SPU");

	req->object->id = req->id;
	req->object->EA = resp->data;
	req->object->size = resp->dataSize;
	req->object->mode = req->mode;
	req->object->count = 1;
	
	req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	req->size = req->object->size;

	//printf(WHERESTR "Item %d was not known, starting DMA transfer\n", WHEREARG, req->id);
	transfer_size = ALIGNED_SIZE(resp->dataSize);

	if ((req->object->data = MALLOC_ALIGN(transfer_size, 7)) == NULL) {
		printf(WHERESTR "Pending invalidate (bitmap): %d, itemsByPointer: %d, itemsById: %d, allocatedId: %d\n", WHEREARG, pendingInvalidateMap, itemsByPointer->fill, itemsById->fill, queue_count(allocatedID));
		REPORT_ERROR("Failed to allocate memory on SPU");
		
		void* test1 = _malloc_align(transfer_size / 4, 7);
		void* test2 = _malloc_align(transfer_size / 4, 7);
		void* test3 = _malloc_align(transfer_size / 4, 7);
		void* test4 = _malloc_align(transfer_size / 4, 7);
		
		if (test1 != NULL)
			printf(WHERESTR "Test1 succes\n", WHEREARG);
		if (test2 != NULL)
			printf(WHERESTR "Test2 succes\n", WHEREARG);
		if (test3 != NULL)
			printf(WHERESTR "Test3 succes\n", WHEREARG);
		if (test4 != NULL)
			printf(WHERESTR "Test4 succes\n", WHEREARG);

		sleep(1000);		
	}

	if (ht_member(itemsByPointer, req->object->data)) {
		REPORT_ERROR("Newly created item already existed in table?");
	} else {
		ht_insert(itemsByPointer, req->object->data, req->object);
	}
	
	if (ht_member(itemsById, (void*)req->object->id)) {
		REPORT_ERROR("Allocated space for an already existing item");
	} else {
		ht_insert(itemsById, (void*)req->object->id, req->object);
	}
	//printf(WHERESTR "Registered dataobject with id: %d\n", WHEREARG, object->id);

	StartDMAReadTransfer(req->object->data, (unsigned int)resp->data, transfer_size, req->dmaNo);
}

void readMailbox() {
	unsigned int requestID;
	unsigned long datasize;
	GUID itemid;
	void* datapointer;
	int mode;
		
	struct acquireResponse* acqResp;
	struct releaseResponse* relResp;
	
	int packagetype = spu_read_in_mbox();
	switch(packagetype)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			//printf(WHERESTR "ACQUIRE package recieved\n", WHEREARG);
			
			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			mode = (int)spu_read_in_mbox();
			datasize = spu_read_in_mbox();
			datapointer = (void*)spu_read_in_mbox();

			if (!GETBIT(requestID, MAX_PENDING_REQUESTS, pendingMap)) {
				REPORT_ERROR("Recieved a request with an unexpected requestID\n");
			} else {
				
				//printf(WHERESTR "ID was %d\n", WHEREARG, requestID);
				acqResp = (struct acquireResponse*)&pendingRequestBuffer[requestID].request;
				
				acqResp->packageCode = packagetype;									
				acqResp->requestID = requestID; 
				acqResp->dataItem = itemid;
				acqResp->dataItem = mode;
				acqResp->dataSize = datasize;
				acqResp->data = datapointer;

				StartDMATransfer(acqResp);
			}
			
			//printf(WHERESTR "Done with ACQUIRE package\n", WHEREARG);			
			break;
		
		case PACKAGE_RELEASE_RESPONSE:
			//printf(WHERESTR "RELEASE package recieved\n", WHEREARG);
			requestID = spu_read_in_mbox();
			
			if (!GETBIT(requestID, MAX_PENDING_REQUESTS, pendingMap)) {
				REPORT_ERROR("Recieved a request with an unexpected requestID\n");
			} else {
				
				//printf(WHERESTR "ID was %d\n", WHEREARG, requestID);
				relResp = (struct releaseResponse*)&pendingRequestBuffer[requestID].request;
						
				relResp->packageCode = packagetype;									
				relResp->requestID = requestID; 

				if (pendingRequestBuffer[requestID].object->mode == WRITE)
				{
					pendingRequestBuffer[requestID].object->count--;
					if (pendingRequestBuffer[requestID].object->count == 0)
						queue_enq(allocatedID, (void*)pendingRequestBuffer[requestID].object->id);
				}
				pendingRequestBuffer[requestID].state = ASYNC_STATUS_COMPLETE;
			}
			
			break;
		
		case PACKAGE_INVALIDATE_REQUEST:			
			//printf(WHERESTR "INVALIDATE package recieved\n", WHEREARG);

			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			
			//printf(WHERESTR "INVALIDATE package read\n", WHEREARG);

			invalidate(itemid);
			break;
		
		default:
			fprintf(stderr, WHERESTR "Unknown package recieved: %i, message: %s", WHEREARG, packagetype, strerror(errno));
	};	
}

unsigned int beginCreate(GUID id, unsigned long size)
{
	pendingRequest req;
	unsigned int nextId = findFreeItem(&requestNo, MAX_PENDING_REQUESTS, &pendingMap);

	if (itemsById == NULL)
	{
		REPORT_ERROR("Initialize must be called");
		CLEARBIT(nextId, MAX_PENDING_REQUESTS, pendingMap);
		return 0;
	}

	req = &pendingRequestBuffer[nextId];
	struct createRequest* request = (struct createRequest*)&req->request;

	request->packageCode = PACKAGE_CREATE_REQUEST;
	request->requestID = nextId;
	request->dataSize = size == 0 ? 1 : size;
	request->dataItem = id;
	
	sendMailbox(request);
	
	req->id = id;
	req->object = NULL;
	req->state = ASYNC_STATUS_REQUEST_SENT;
	req->mode = WRITE;

	//printf(WHERESTR "Issued an create for %d, with req %d\n", WHEREARG, id, nextId); 
	
	return nextId;
}

unsigned int beginAcquire(GUID id, int type)
{
	pendingRequest req = NULL;
	struct acquireRequest* request = NULL;
	unsigned int nextId = findFreeItem(&requestNo, MAX_PENDING_REQUESTS, &pendingMap);
	
	if (itemsById == NULL)
	{
		REPORT_ERROR("Initialize must be called");
		CLEARBIT(nextId, MAX_PENDING_REQUESTS, pendingMap);
		return 0;
	}
	
	if (!GETBIT(nextId, MAX_PENDING_REQUESTS, pendingMap))
	{
		//printf(WHERESTR "Values %d, %d\n", WHEREARG, nextId, pendingMap);
		REPORT_ERROR("Setbit failed");
		sleep(5);
	}

	while (spu_stat_in_mbox() > 0)
		readMailbox();
		
	processPendingInvalidate(id);

	req = &pendingRequestBuffer[nextId];

	//printf(WHERESTR "request %i, req %i\n", WHEREARG, (int)request, (int)req);
	if (ht_member(itemsById, (void*)id))
	{
		dataObject object = ht_get(itemsById, (void*)id);
		if (type == READ && (object->count == 0 || object->mode == READ) && !pendingInvalidateContains(id, 0))
		{	
			//printf(WHERESTR "Reacquire for READ id: %i\n", WHEREARG, id);
	
			object->mode = type;
			object->count++;
			removeAllocatedID(id);
			//printf(WHERESTR "Removed id %d\n", WHEREARG, object->id);
			
			req->id = id;
			req->object = object;
			req->request.packageCode = PACKAGE_INVALID;
			req->size = object->size;
			req->state = ASYNC_STATUS_COMPLETE;
			req->mode = type;
			
			return nextId;
		}		
	}

	request = (struct acquireRequest*)&req->request;	
	
	if (type == WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	} else if (type == READ) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	} else {
		REPORT_ERROR("Starting acquiring in unknown mode");
		CLEARBIT(nextId, MAX_PENDING_REQUESTS, pendingMap);
		return 0;
	}
	request->requestID = nextId;
	request->dataItem = id;
	
	//printf(WHERESTR "request %i, req %i\n", WHEREARG, (int)request, (int)req);
	
	req->id = id;
	req->object = NULL;
	req->state = ASYNC_STATUS_REQUEST_SENT;
	req->mode = type;

	sendMailbox(request);
	
	return nextId;
}

unsigned int beginRelease(void* data)
{
	unsigned int nextId = findFreeItem(&requestNo, MAX_PENDING_REQUESTS, &pendingMap);
	unsigned int transfersize;
	dataObject object;
	pendingRequest req;
	size_t i;
	
	//printf(WHERESTR "Starting a release\n", WHEREARG); 
	
	if (itemsById == NULL)
	{
		REPORT_ERROR("Initialize must be called");
		return 0;
	}

	req = &pendingRequestBuffer[nextId];

	if (ht_member(itemsByPointer, data)) {
		object = ht_get(itemsByPointer, data);

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

			struct releaseRequest* request = (struct releaseRequest*)&req->request;
	
			request->packageCode = PACKAGE_RELEASE_REQUEST; 
			request->requestID = nextId;
			request->dataItem = object->id;
			request->mode = WRITE;
			request->dataSize = object->size;
			request->offset = 0;
			
		} else if (object->mode == READ || object->mode == BLOCKED) {
			//printf(WHERESTR "Starting a release for %d in read mode (ls: %d, data: %d)\n", WHEREARG, (int)object->id, (int)object->data, (int)data); 

			object->count--;
			
			if (object->mode == BLOCKED) {
				if (object->count == 0)
				{
					for(i = 0; i < MAX_PENDING_REQUESTS; i++)
						if (GETBIT(i, MAX_PENDING_REQUESTS, pendingMap) && pendingRequestBuffer[i].id == object->id)
						{
							pendingRequestBuffer[i].state = ASYNC_STATUS_COMPLETE;
							object->count++;
							object->mode = WRITE;
							break;
						}

					if (object->count == 0)
						REPORT_ERROR("Object state was blocked, but there was no pending requests for it");
				}
			} else {
				//printf(WHERESTR "Local release for %d in read mode\n", WHEREARG, object->id);
				//removeAllocatedID(object->id);
				if (object->count == 0) 
					queue_enq(allocatedID, (void*)object->id);
				//printf(WHERESTR "Inserted id %d\n", WHEREARG, object->id);
			}
			
			req->id = object->id;
			req->object = object;
			req->size = object->size;
			req->state = ASYNC_STATUS_COMPLETE;
			req->request.packageCode = PACKAGE_INVALID;
			req->mode = READ;
			
			if (object->count == 0 && pendingInvalidateContains(object->id, 1) >= 0)
			{
				clean(object->id);
				req->object = NULL;
			}
		}
		 
		return nextId;
	} else {
		REPORT_ERROR("Tried to release non allocated item");
		return 0;
	}		
}

void initialize(){
	itemsByPointer = ht_create(10, lessint, hashfc);
	itemsById = ht_create(10, lessint, hashfc);
	allocatedID = queue_create();
}

void terminate() {
	ht_destroy(itemsByPointer);
	ht_destroy(itemsById);
	queue_destroy(allocatedID);
	itemsByPointer = NULL;
	itemsById = NULL;
	allocatedID = NULL;
}


int getAsyncStatus(unsigned int requestNo)
{
	pendingRequest req;
	
	//printf(WHERESTR "In AsyncStatus for %d\n", WHEREARG, requestNo);
	
	if (!GETBIT(requestNo, MAX_PENDING_REQUESTS, pendingMap))
	{
		//printf(WHERESTR "Failed for %d\n", WHEREARG, requestNo);
		return ASYNC_STATUS_ERROR;
	}
	else
	{
		while (spu_stat_in_mbox() != 0)
		{
			//printf(WHERESTR "Processing pending mailbox messages, in %d\n", WHEREARG, requestNo);
			readMailbox();
		}
	
		req = &pendingRequestBuffer[requestNo];
		
		if (req->state == ASYNC_STATUS_DMA_PENDING)
		{
			if (IsDMATransferGroupCompleted(req->dmaNo))
			{
				req->state = ASYNC_STATUS_COMPLETE;
				if (req->request.packageCode == PACKAGE_RELEASE_REQUEST)
				{
					req->state = ASYNC_STATUS_REQUEST_SENT;
					((struct acquireRequest*)&req->request)->requestID = requestNo;
					
					sendMailbox(&req->request);
					//printf(WHERESTR "Handling release status for %d\n", WHEREARG, requestNo);
				}
			}		
		}	
		return req->state;
	}
}

void* endAsync(unsigned int requestNo, unsigned long* size)
{
	pendingRequest req = NULL;
	void* retval = NULL;
	
	if (getAsyncStatus(requestNo) == ASYNC_STATUS_ERROR)
	{
		REPORT_ERROR("RequestNo was not for a pending request");
		return NULL;
	}
	
	if (!GETBIT(requestNo, MAX_PENDING_REQUESTS, pendingMap))
	{
		REPORT_ERROR("Invalid request number");
		return NULL;
	}
	
	req = &pendingRequestBuffer[requestNo];
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
		else if (req->state == ASYNC_STATUS_BLOCKED)
		{
			if (IsThreaded())
				YieldThread();
			else
				REPORT_ERROR("It is not allowed to acquire the same object twice from the same thread");
		}

		getAsyncStatus(requestNo);

		//printf(WHERESTR "Status for %d is %d\n", WHEREARG, requestNo, req->state); 
	}

	if (req->object == NULL)
	{
		size = NULL;
		retval = NULL;
	}
	else
	{
		if (size != NULL)
			*size = req->object->size;
		retval = req->object->data;
		//printf(WHERESTR "In AsyncStatus for %d, obj id: %d, obj data: %d\n", WHEREARG, requestNo, req->object->id, (int)req->object->data);
	}
 	
	if (req->object != NULL)
	{
		if (!ht_member(itemsById, (void*)req->object->id))
		{
			printf(WHERESTR "Dataobject %d (%d) was not registered anymore\n", WHEREARG, req->id, (int)req->object->data); 
			removeAllocatedID(req->object->id);
			printf(WHERESTR "Removed id %d\n", WHEREARG, req->object->id);

			pendingInvalidateContains(req->object->id, 1);

			FREE_ALIGN(req->object->data);
			req->object->data = NULL;
			FREE(req->object);
			req->object = NULL;
		}
	}

	if (req->request.packageCode == PACKAGE_RELEASE_REQUEST)
		processPendingInvalidate(((struct acquireRequest*)&req->request)->dataItem);

	req->object = NULL;
	req->request.packageCode = PACKAGE_INVALID;		
	CLEARBIT(requestNo, MAX_PENDING_REQUESTS, pendingMap);
	
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
	return (void*)endAsync(req, NULL);
}


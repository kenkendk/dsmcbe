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

#define BLOCKED (READ + WRITE)

//#define REPORT_MALLOC(x) printf(WHERESTR "Malloc gave %d, balance: %d\n", WHEREARG, (int)x, ++balance);
//#define REPORT_FREE(x) printf(WHERESTR "Free'd %d, balance: %d\n", WHEREARG, (int)x, --balance);

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

void sendMailbox();
void invalidate(struct invalidateRequest* item);

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

//#define TESTHT testht(__LINE__);

static hashtableIterator test_ht_it = NULL;

//We cannot free items if they are in transit
int pendingContains(dataObject obj)
{
	hashtableIterator it;
	pendingRequest req;
	
	it = ht_iter_create(pending);
	while(ht_iter_next(it))
	{
		req = ht_iter_get_value(it);
		if (req->object == obj)
		{
			//printf(WHERESTR "Found invalidated item to be in progress: %d\n", WHEREARG, obj->id);
			return 1;
		}
	}
	ht_iter_destroy(it);
	return 0;
}

void processPendingInvalidate(GUID id)
{
	struct invalidateRequest* req;
	if (ht_member(pendingInvalidate, (void*)id))
	{
		req = ht_get(pendingInvalidate, (void*)id);
		ht_delete(pendingInvalidate, (void*)id);
		invalidate(req);
	}	
}


void testht(int lineno)
{
	//printf(WHERESTR "In tst, %d, %d\n", WHEREARG, lineno, (int)test_ht_it);
	if (pending == NULL)
	{
		printf("Pending is null, returning. Caller %d\n", lineno);
		return;
	}

	if (test_ht_it == NULL)
	{
		//printf(WHERESTR "In tst, %d, %d\n", WHEREARG, lineno, (int)test_ht_it);
		test_ht_it = ht_iter_create(pending);
		//printf(WHERESTR "In tst, %d, %d\n", WHEREARG, lineno, (int)test_ht_it);
	}
	
	//printf(WHERESTR "In tst, %d, %d\n", WHEREARG, lineno, (int)test_ht_it);
	test_ht_it->ht = pending;
	test_ht_it->index = -1;
	test_ht_it->kl = NULL;
	
	//printf(WHERESTR "In tst, %d, %d\n", WHEREARG, lineno, (int)test_ht_it);
	while(ht_iter_next(test_ht_it))
	{
		if ((unsigned int)ht_iter_get_key(test_ht_it) > requestNo)
		{
			printf(WHERESTR "In tst, %d, %d\n", WHEREARG, lineno, (int)test_ht_it);
			test_ht_it->ht = pending;
			test_ht_it->index = -1;
			test_ht_it->kl = NULL;
			printf(WHERESTR "Detected broken hashtable from line %d: ", WHEREARG, lineno);
			while(ht_iter_next(test_ht_it))
				printf("%d, ", (int)ht_iter_get_key(test_ht_it));
			printf("\n");
			sleep(10);
			break;
		}
	}

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
	
	//printf(WHERESTR "Removing item from allocated, count before: %d\n", WHEREARG, queue_count(allocatedID));
	while(tmp->next != NULL)
	{
		if ((GUID)tmp->next->element == id)
		{
			tmp->next = cdr_and_free(tmp->next);
			//printf(WHERESTR "Removed item from allocated, count after: %d\n", WHEREARG, queue_count(allocatedID));
			break;
		}
		
		tmp = tmp->next;
	}
}

void unsubscribe(dataObject object)
{
	/*struct releaseRequest* request;
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);
	
	if ((request = MALLOC(sizeof(struct releaseRequest))) == NULL)
		REPORT_ERROR("malloc error");

	request->packageCode = PACKAGE_RELEASE_REQUEST; 
	request->requestID = nextId;
	request->dataItem = object->id;
	request->mode = READ;
	request->dataSize = object->size;
	request->offset = 0;
	
	sendMailbox(request);
	
	FREE(request);*/
	
	//printf(WHERESTR "Unregistering item with id: %d\n", WHEREARG, object->id);
	spu_write_out_mbox(PACKAGE_RELEASE_REQUEST);
	spu_write_out_mbox(NEXT_SEQ_NO(requestNo, MAX_REQ_NO));
	spu_write_out_mbox(object->id);
	spu_write_out_mbox(READ);
	spu_write_out_mbox(object->size);
	spu_write_out_mbox((int)object->EA);
}

void clean(GUID id)
{
	dataObject object;
	if (ht_member(allocatedItemsOld, (void*)id))
	{
		object = ht_get(allocatedItemsOld, (void*)id);
		if (object->count == 0 && !pendingContains(object))
		{
			ht_delete(allocatedItemsOld, (void*)id);
			removeAllocatedID(id);
			unsubscribe(object);
			if (ht_member(pendingInvalidate, (void*)object->id))
			{
				FREE(ht_get(pendingInvalidate, (void*)object->id));
				ht_delete(pending, (void*)object->id);
			}
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
	
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), allocatedItemsOld->fill, totalfreed, (int)size);
		
	void* pointer = thread_malloc_align(size, base);
	unsigned long freedmemory = 0;
	int go = 0;
	list* cur = &allocatedID->head;
	
	if (queue_count(allocatedID) != (allocatedItemsOld->fill - allocatedItems->fill))
		REPORT_ERROR("allocatedID and allocatedItemsOld differ in size");
		
	
	while (pointer == NULL) {
		//printf(WHERESTR "Starting to free memory\n", WHEREARG);	
	
		while(freedmemory < size || go) {
			int id;
			if (cur != NULL)
				id = (GUID)(*cur)->element;
			else
				return thread_malloc_align(size, base);
		
			//printf(WHERESTR "Trying to clear id %i\n", WHEREARG, id);		
			if(ht_member(allocatedItemsOld, (void*)id)) {
			
				dataObject object = ht_get(allocatedItemsOld, (void*)id);
				if (object->count == 0 && !pendingContains(object))
				{
					if (ht_member(allocatedItems, object->data))
						fprintf(stderr, WHERESTR "Item had no lockers, but was allocated (%d, %d)?", WHEREARG, object->id, (int)object->data);

					unsubscribe(object);
					freedmemory += (object->size + sizeof(struct dataObjectStruct));
					ht_delete(allocatedItemsOld, (void*)id);
	
					if (ht_member(pendingInvalidate, (void*)object->id))
					{
						FREE(ht_get(pendingInvalidate, (void*)object->id));
						ht_delete(pending, (void*)object->id);
					}

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
				REPORT_ERROR("allocatedID not found in allocatedItemsOld");
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
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), allocatedItemsOld->fill, totalfreed, (int)size);
	return pointer;
}

void* clear(unsigned long size) {	
	
	if (size == 0)
	{
		REPORT_ERROR("Called malloc align with size zero");	
		return NULL;
	}
	
	void* pointer = thread_malloc(size);
	unsigned long freedmemory = 0;
	int go = 0;
	list* cur = &allocatedID->head;
	
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), allocatedItemsOld->fill, totalfreed, (int)size);
	
	while (pointer == NULL) {
		//printf(WHERESTR "Starting to free memory\n", WHEREARG);	
	
		while(freedmemory < size || go) {
			int id;
			if (cur != NULL)
				id = (GUID)(*cur)->element;
			else
				return thread_malloc(size);
		
			//printf(WHERESTR "Trying to clear id %i\n", WHEREARG, id);		
			if(ht_member(allocatedItemsOld, (void*)id)) {
				dataObject object = ht_get(allocatedItemsOld, (void*)id);
				if (object->count == 0 && !pendingContains(object))
				{
					if (ht_member(allocatedItems, object->data))
						fprintf(stderr, WHERESTR "Item had no lockers, but was allocated (%d, %d)?", WHEREARG, object->id, (int)object->data);
					unsubscribe(object);
					freedmemory += (object->size + sizeof(struct dataObjectStruct));
					ht_delete(allocatedItemsOld, (void*)id);
	
					if (ht_member(pendingInvalidate, (void*)object->id))
					{
						FREE(ht_get(pendingInvalidate, (void*)object->id));
						ht_delete(pending, (void*)object->id);
					}

					FREE_ALIGN(object->data);
					object->data = NULL;
					FREE(object);
					object = NULL;
					(*cur) = cdr_and_free(*cur);
				}
				else
					cur = &((*cur)->next);
				//printf(WHERESTR "Cleared id %i\n", WHEREARG, id);
			} else {
				REPORT_ERROR("allocatedID not found in allocatedItemsOld");
			}		
		}
		
		//printf(WHERESTR "callign malloc...\n", WHEREARG);		
		//printf(WHERESTR "Enough is free, trying malloc_align\n", WHEREARG);
		pointer = thread_malloc(size);
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
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), allocatedItemsOld->fill, totalfreed, (int)size);
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

void invalidate(struct invalidateRequest* item) {
	//printf(WHERESTR "Trying to invalidate data with id: %i\n", WHEREARG, item->dataItem);

	/*FREE(item);
	return;*/
	
	GUID id = item->dataItem;
	
	if(ht_member(allocatedItemsOld, (void*)id)) {
		//printf(WHERESTR "Data with id: %i is allocated but has been released\n", WHEREARG, id);
		dataObject object = ht_get(allocatedItemsOld, (void*)id);
		
		if (object->count == 0 && !pendingContains(object))
		{
			if (ht_member(allocatedItems, object->data))
				fprintf(stderr, WHERESTR "Item lock count was zero, but item was allocated? (%d, %d)\n", WHEREARG, object->id, (int)object->data);  
			ht_delete(allocatedItemsOld, (void*)id);
			removeAllocatedID(id);
			FREE_ALIGN(object->data);
			object->data = NULL;
			FREE(object);		
			object = NULL;	
			FREE(item);
			item = NULL;
		}
		else
		{
			if (!ht_member(pendingInvalidate, (void*)id))
			{
				//printf(WHERESTR "Data with id: %i is in use\n", WHEREARG, id);
				ht_insert(pendingInvalidate, (void*)id, item);
				item = NULL;
			}
			else
			{
				//REPORT_ERROR("Recieved another invalidateRequest for the same item");
				FREE(item);
				item = NULL;
			}
		}
	}
	else	
	{
		//printf(WHERESTR "Discarded invalidate message with id: %i\n", WHEREARG, id);
		FREE(item);
		item = NULL;
	}
	
}

void StartDMATransfer(struct acquireResponse* resp)
{
	unsigned int transfer_size;
	pendingRequest req = (pendingRequest)ht_get(pending, (void*)resp->requestID);
	
	//printf(WHERESTR "Processing ACQUIRE package for %d, %d\n", WHEREARG, req->id, resp->requestID);
	
	if (ht_member(allocatedItemsOld, (void*)(req->id))) {
		req->object = (dataObject)ht_get(allocatedItemsOld, (void*)(req->id));
		removeAllocatedID(req->id);
		
		if (req->object->count == 0)
		{
			if (ht_member(allocatedItems, req->object->data)) {
				REPORT_ERROR("Tried to re-insert an object into allocatedItems");
			} else {
				ht_insert(allocatedItems, req->object->data, req->object);
			}
		}
		
		if (req->mode == READ || req->object->count == 0)
			req->object->count++;
		else if (req->mode == WRITE)
		{
			req->state = ASYNC_STATUS_BLOCKED;
			req->object->mode = BLOCKED;
			return;
		}
		else
		{
			REPORT_ERROR("Invalid mode detected");
			req->state = ASYNC_STATUS_ERROR;
		}
					
		//printf(WHERESTR "Item %d (%d) was known, returning local copy\n", WHEREARG, req->id, (int)req->object->data);

		req->object->mode = req->mode;
		req->state = ASYNC_STATUS_COMPLETE;
		
		if (!ht_member(allocatedItems, req->object->data))
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
		printf(WHERESTR "Pending fill: %d, pending invalidate: %d, allocatedItems: %d, allocatedItemsOld: %d, allocatedId: %d\n", WHEREARG, pending->fill, pendingInvalidate->fill, allocatedItems->fill, allocatedItemsOld->fill, queue_count(allocatedID));
		REPORT_ERROR("Failed to allocate memory on SPU");
		
		sleep(10);
	}

	if (ht_member(allocatedItems, req->object->data)) {
		REPORT_ERROR("Newly created item already existed in table?");
	} else {
		ht_insert(allocatedItems, req->object->data, req->object);
	}
	
	if (ht_member(allocatedItemsOld, (void*)req->object->id)) {
		REPORT_ERROR("Allocated space for an already existing item");
	} else {
		ht_insert(allocatedItemsOld, (void*)req->object->id, req->object);
	}
	//printf(WHERESTR "Registered dataobject with id: %d\n", WHEREARG, object->id);

	StartDMAReadTransfer(req->object->data, (unsigned int)resp->data, transfer_size, req->dmaNo);
}

void readMailbox() {
	void* dataItem;
	unsigned int requestID;
	unsigned long datasize;
	GUID itemid;
	void* datapointer;
	pendingRequest req;
	int mode;
		
	int packagetype = spu_read_in_mbox();
	switch(packagetype)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			//printf(WHERESTR "ACQUIRE package recieved\n", WHEREARG);
			if ((dataItem = MALLOC(sizeof(struct acquireResponse))) == NULL)
				REPORT_ERROR("Failed to allocate memory on SPU");
			
			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			mode = (int)spu_read_in_mbox();
			datasize = spu_read_in_mbox();
			datapointer = (void*)spu_read_in_mbox();

			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 
			((struct acquireResponse*)dataItem)->dataItem = itemid;
			((struct acquireResponse*)dataItem)->dataItem = mode;
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
				REPORT_ERROR("Recieved a request with an unexpected requestID\n");
			
			StartDMATransfer((struct acquireResponse*)dataItem);
			//printf(WHERESTR "Done with ACQUIRE package\n", WHEREARG);			
			break;
		
		case PACKAGE_RELEASE_RESPONSE:
			//printf(WHERESTR "RELEASE package recieved\n", WHEREARG);
			if ((dataItem = MALLOC(sizeof(struct releaseResponse))) == NULL)
			{
				printf(WHERESTR "pending fill: %d, allocated: %d, allocatedOld: %d, queue: %d\n", WHEREARG, pending->fill, allocatedItems->fill, allocatedItemsOld->fill, queue_count(allocatedID));
				REPORT_ERROR("Failed to allocate memory on SPU");
			}

			requestID = spu_read_in_mbox();
			((struct acquireResponse*)dataItem)->packageCode = packagetype;									
			((struct acquireResponse*)dataItem)->requestID = requestID; 

			if (ht_member(pending, (void*)((struct acquireResponse*)dataItem)->requestID))
			{
				req = ht_get(pending, (void*)((struct acquireResponse*)dataItem)->requestID);
				if (req->request != NULL)
					FREE(req->request);
				req->object->count--;
				req->request = dataItem;
				req->state = ASYNC_STATUS_COMPLETE;
			}
			else
				REPORT_ERROR("Recieved a request with an unexpected requestID");

			break;
		
		case PACKAGE_INVALIDATE_REQUEST:			
			//printf(WHERESTR "INVALIDATE package recieved\n", WHEREARG);

			if ((dataItem = MALLOC(sizeof(struct invalidateRequest))) == NULL)
				REPORT_ERROR("Failed to allocate memory on SPU");
			
			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			
			((struct invalidateRequest*)dataItem)->packageCode = packagetype;									
			((struct invalidateRequest*)dataItem)->requestID = requestID;
			((struct invalidateRequest*)dataItem)->dataItem = itemid; 

			//printf(WHERESTR "INVALIDATE package read\n", WHEREARG);

			invalidate(dataItem);
			break;
		
		default:
			fprintf(stderr, WHERESTR "Unknown package recieved: %i, message: %s", WHEREARG, packagetype, strerror(errno));
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
		REPORT_ERROR("malloc error");
	
	request->packageCode = PACKAGE_CREATE_REQUEST;
	request->requestID = nextId;
	request->dataSize = size == 0 ? 1 : size;
	request->dataItem = id;
	
	sendMailbox(request);
	
	req->id = id;
	req->object = NULL;
	req->request = request;
	req->state = ASYNC_STATUS_REQUEST_SENT;
	req->mode = WRITE;

	//printf(WHERESTR "Issued an create for %d, with req %d\n", WHEREARG, id, nextId); 
	
	if (ht_member(pending, (void*)nextId))
		REPORT_ERROR("Re-used a request ID");
	ht_insert(pending, (void*)nextId, req);
	if (!ht_member(pending, (void*)nextId))
		REPORT_ERROR("Failed to insert item in pending list");
	
	return nextId;
}

unsigned int beginAcquire(GUID id, int type)
{
	pendingRequest req = NULL;
	struct acquireRequest* request = NULL;
	unsigned int nextId = NEXT_SEQ_NO(requestNo, MAX_REQ_NO);
	
	if (allocatedItemsOld == NULL)
	{
		printf(WHERESTR "Initialize must be called\n", WHEREARG);
		return 0;
	}

	while (spu_stat_in_mbox() > 0)
		readMailbox();
		
	processPendingInvalidate(id);

	if ((req = (pendingRequest)MALLOC(sizeof(struct pendingRequestStruct))) == NULL)
		REPORT_ERROR("malloc error");

	//printf(WHERESTR "request %i, req %i\n", WHEREARG, (int)request, (int)req);

	if (ht_member(allocatedItemsOld, (void*)id))
	{
		dataObject object = ht_get(allocatedItemsOld, (void*)id);
		if (type == READ && (object->count == 0 || object->mode == READ) && !ht_member(pendingInvalidate, (void*)id))
		{	
			//printf(WHERESTR "Reacquire for READ id: %i\n", WHEREARG, id);
	
			object->mode = type;
			object->count++;
			removeAllocatedID(id);
			
			if (!ht_member(allocatedItems, object->data))
				ht_insert(allocatedItems, object->data, object);
	
			req->id = id;
			req->object = object;
			req->request = NULL;
			req->size = object->size;
			req->state = ASYNC_STATUS_COMPLETE;
			req->mode = type;
			
			if (ht_member(pending, (void*)nextId))
				REPORT_ERROR("Re-used a request ID");
			ht_insert(pending, (void*)nextId, req);
			return nextId;
		}		
	}
	
	if ((request = MALLOC(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");
	
	if (type == WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	} else if (type == READ) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	} else {
		REPORT_ERROR("Starting acquiring in unknown mode");
		return 0;
	}
	request->requestID = nextId;
	request->dataItem = id;
	
	//printf(WHERESTR "request %i, req %i\n", WHEREARG, (int)request, (int)req);
	
	req->id = id;
	req->object = NULL;
	req->request = request;
	req->state = ASYNC_STATUS_REQUEST_SENT;
	req->mode = type;

	sendMailbox(request);

	if (ht_member(pending, (void*)nextId))
		REPORT_ERROR("Re-used a request ID");
	ht_insert(pending, (void*)nextId, req);
	if (!ht_member(pending, (void*)nextId))
		REPORT_ERROR("Failed to insert item in pending list");
	
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
			request->mode = WRITE;
			request->dataSize = object->size;
			request->offset = 0;

			req->request = request;
			
			ht_delete(allocatedItems, data);
			queue_enq(allocatedID, (void*)object->id);
		} else if (object->mode == READ || object->mode == BLOCKED) {
			//printf(WHERESTR "Starting a release for %d in read mode (ls: %d, data: %d)\n", WHEREARG, (int)object->id, (int)object->data, (int)data); 

			object->count--;
			
			if (object->mode == BLOCKED) {
				if (object->count == 0)
				{
					hashtableIterator it = ht_iter_create(pending);
					while(ht_iter_next(it))
					{
						if (ht_iter_get_key(it) == (void*)object->id)
						{
							((pendingRequest)ht_iter_get_value(it))->state = ASYNC_STATUS_COMPLETE;
							object->count++;
							object->mode = WRITE;
							break;
						}
					}
					ht_iter_destroy(it);
					if (object->count == 0)
						REPORT_ERROR("Object state was blocked, but there was no pending requests for it");
				}
			} else if (ht_member(pendingInvalidate, (void*)object->id) && object->count == 0) {
				struct invalidateRequest* item;
				item = ht_get(pendingInvalidate, (void*)object->id);
				FREE(item);
				item = NULL;
				ht_delete(pendingInvalidate, (void*)object->id);
				
			} else {
				//printf(WHERESTR "Local release for %d in read mode\n", WHEREARG, object->id);
				removeAllocatedID(object->id); 
				queue_enq(allocatedID, (void*)object->id);
				if (object->count == 0)
					ht_delete(allocatedItems, object->data);
			}
			
			req->id = object->id;
			req->object = object;
			req->size = object->size;
			req->state = ASYNC_STATUS_COMPLETE;
			req->request = NULL;
			req->mode = READ;
		}
		 
		if (ht_member(pending, (void*)nextId))
			REPORT_ERROR("Re-used a request ID");
		ht_insert(pending, (void*)nextId, req);
		if (!ht_member(pending, (void*)nextId))
			REPORT_ERROR("Failed to insert item in pending list");
		
		return nextId;
	} else {
		REPORT_ERROR("Tried to release non allocated item");
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
	allocatedItems = NULL;
	allocatedItemsOld = NULL;
	pending = NULL;
	pendingInvalidate = NULL;
	allocatedID = NULL;
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
	pendingRequest req = NULL;
	void* retval = NULL;
	
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
		else if (req->state == ASYNC_STATUS_BLOCKED)
		{
			if (IsThreaded())
				YieldThread();
			else
				REPORT_ERROR("It is not allowed to acquire the same object twice from the same thread");
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
		if (size != NULL)
			*size = req->object->size;
		retval = req->object->data;
		//printf(WHERESTR "In AsyncStatus for %d, obj id: %d, obj data: %d\n", WHEREARG, requestNo, req->object->id, (int)req->object->data);
	}
 	
	if (req->object != NULL)
	{
		if (!ht_member(allocatedItemsOld, (void*)req->id) && !ht_member(allocatedItems, req->object->data))
		{
			printf(WHERESTR "Dataobject %d (%d) was not registered anymore\n", WHEREARG, req->id, (int)req->object->data); 
			removeAllocatedID(req->object->id);

			if (ht_member(pendingInvalidate, (void*)req->object->id))
			{
				FREE(ht_get(pendingInvalidate, (void*)req->object->id));
				ht_delete(pending, (void*)req->object->id);
			}

			FREE_ALIGN(req->object->data);
			req->object->data = NULL;
			FREE(req->object);
			req->object = NULL;
		}
	}

	if (req->request != NULL)
	{
		if (((struct acquireRequest*)req->request)->packageCode == PACKAGE_RELEASE_REQUEST)
			processPendingInvalidate(((struct acquireRequest*)req->request)->dataItem);
		
		FREE(req->request);
		req->request = NULL;
	}

	FREE(req);
	req = NULL;

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


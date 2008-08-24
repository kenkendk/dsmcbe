//#define TIMER

#include <libmisc.h>
#include <spu_mfcio.h>
#include <unistd.h>

#ifdef TIMER  
#include <spu_timer.h>
#endif

//Switch to interrupt mailbox in order to enable the PPU eventhandler
#define SPU_WRITE_OUT_MBOX spu_write_out_intr_mbox
//#define SPU_WRITE_OUT_MBOX spu_write_out_mbox 

#include "../../dsmcbe_spu.h"
#include "../../common/datapackages.h"
#include "../../common/datastructures.h"
#include "../header files/DMATransfer.h"
#include "../../common/debug.h"
#include "../../PPU/header files/SPU_MemoryAllocator_Shared.h"

//#define DEBUG_PACKAGES

//#define FENCED_NOTIFY

#define ASYNC_STATUS_ERROR -1
#define ASYNC_STATUS_REQUEST_SENT 1
#define ASYNC_STATUS_DMA_PENDING 2
#define ASYNC_STATUS_BLOCKED 3
#define ASYNC_STATUS_COMPLETE 4
#define ASYNC_STATUS_WRITE_READY 5
#define TRUE 1
#define FALSE 0

extern unsigned long long speID;

#ifdef TIMER
unsigned long long real_clock = 0;

unsigned long long start = 0;
unsigned long long beforeDMA = 0;
unsigned long long afterDMA = 0;
unsigned long long end = 0;

float acquireReadCount = 0.0;
unsigned int acquireRead = 0;
unsigned int beforeDMAReadRead = 0;
unsigned int dmaReadRead = 0;
unsigned int afterDMAReadRead = 0;

float acquireWriteCount = 0.0;
unsigned int acquireWrite = 0;
unsigned int beforeDMAReadWrite = 0;
unsigned int dmaReadWrite = 0;
unsigned int afterDMAReadWrite = 0;

float releaseReadCount = 0.0;
unsigned int releaseRead = 0;

float releaseWriteCount = 0.0;
unsigned int releaseWrite = 0;
unsigned int beforeDMAWrite = 0;
unsigned int dmaWrite = 0;
unsigned int afterDMAWrite = 0;

volatile unsigned int READWRITE = 0;
volatile unsigned int StatType = 1;

unsigned long long ReadClock()
{
	unsigned long long temp = spu_clock_read();
	if(temp < real_clock)
		real_clock += (UINT_MAX - real_clock) + temp;
	else
		real_clock += temp - real_clock;
				
	return real_clock;
}
#endif

void spu_dsmcbe_memory_setup();

/*extern unsigned int thread_id;
extern int thread_no;*/

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

//The termination status
static unsigned int terminated = 0;

//The list of items requested for invalidate

struct pendingInvalidateObject {
	GUID guid;
	unsigned int requestID;
};
static struct pendingInvalidateObject pendingInvalidates[MAX_PENDING_INVALIDATES];

//The map of used pending invalidates
static unsigned int pendingInvalidateMap = 0; 

#define SETBIT(seqNo,maxNo,map) (map |= (1 << (seqNo = (seqNo % maxNo))))
#define GETBIT(seqNo,maxNo,map) ((map & (1 << (seqNo = (seqNo % maxNo)))) != 0) 
#define CLEARBIT(seqNo,maxNo,map) map = (map & (~(1 << (seqNo = (seqNo % maxNo))))) 

void sendMailbox();
void readMailbox();
void invalidate(GUID id, unsigned int requestID);

typedef struct dataObjectStruct *dataObject;


struct dataObjectStruct{	
	GUID id;
	void* EA;
	void* data;
	unsigned long size;
	int mode;	
	int ready;
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
unsigned int findFreeItem(unsigned int* seq, unsigned int max, unsigned int* map)
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
		if (GETBIT(i, MAX_PENDING_INVALIDATES, pendingInvalidateMap) && pendingInvalidates[i].guid == id)
		{
			//printf(WHERESTR "Found a pending invalidate\n", WHEREARG);
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
	//printf(WHERESTR "Processing pending invalidates for id %d (%d:%d)\n", WHEREARG, id, thread_id, thread_no);	
	int index = pendingInvalidateContains(id, 1);
	if (index >= 0)
	{
		//printf(WHERESTR "Found a pending invalidate for %d (%d:%d)\n", WHEREARG, id, thread_id, thread_no);	
		invalidate(id, pendingInvalidates[index].requestID);
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
	SPU_WRITE_OUT_MBOX(PACKAGE_RELEASE_REQUEST);
	SPU_WRITE_OUT_MBOX(MAX_PENDING_REQUESTS + 1); //Invalid number
	SPU_WRITE_OUT_MBOX(object->id);
	SPU_WRITE_OUT_MBOX(ACQUIRE_MODE_READ);
	SPU_WRITE_OUT_MBOX(object->size);
	SPU_WRITE_OUT_MBOX((int)object->EA);
}

void cleanup()
{
	dataObject object;
	hashtableIterator iter = ht_iter_create(itemsById);
	while(ht_iter_next(iter))
	{
		object = ht_iter_get_value(iter);
		FREE_ALIGN(object->data);
		object->data = NULL;
		FREE(object);
		object = NULL;			
	}
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

#define thread_malloc_align(x,y) spu_dsmcbe_memory_malloc(x)

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
				REPORT_ERROR("allocatedID not found in itemsById");
				fprintf(stderr, WHERESTR "ID is %d (%d, %i/%i)\n", WHEREARG, id, queue_count(allocatedID), itemsById->fill, itemsById->count);
				fprintf(stderr, WHERESTR "Requested size was %d, base was %d\n", WHEREARG, (int)size, base);
				

				/*ht_iter_reset(itx);
				fprintf(stderr, "Key list: ");
				while(ht_iter_next(itx))
					fprintf(stderr, "%d, ", (int)ht_iter_get_key(itx));
				fprintf(stderr, "\n");*/
				
				*cur = cdr_and_free(*cur);
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
	}
	//printf("Trying to free memory: (queue: %i), (hash: %i), (freed: %i of %i)\n", queue_count(allocatedID), itemsById->fill, totalfreed, (int)size);
	return pointer;
}
#undef thread_malloc_align

void sendMailbox(void* dataItem) {
#ifdef DEBUG_PACKAGES
	printf(WHERESTR "Sending request type %s (%d) with reqId: %d, possible id: %d\n", WHEREARG, PACKAGE_NAME(((struct createRequest*)dataItem)->packageCode), ((struct createRequest*)dataItem)->packageCode, ((struct createRequest*)dataItem)->requestID, ((struct createRequest*)dataItem)->dataItem);
#endif
	switch(((struct releaseRequest*)dataItem)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			SPU_WRITE_OUT_MBOX(((struct createRequest*)dataItem)->packageCode);
			SPU_WRITE_OUT_MBOX(((struct createRequest*)dataItem)->requestID);
			SPU_WRITE_OUT_MBOX(((struct createRequest*)dataItem)->dataItem);
			SPU_WRITE_OUT_MBOX(((struct createRequest*)dataItem)->dataSize);
			break;			
		
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			SPU_WRITE_OUT_MBOX(((struct acquireRequest*)dataItem)->packageCode);
			SPU_WRITE_OUT_MBOX(((struct acquireRequest*)dataItem)->requestID);
			SPU_WRITE_OUT_MBOX(((struct acquireRequest*)dataItem)->dataItem);
			break;
		
		case PACKAGE_RELEASE_REQUEST:
			SPU_WRITE_OUT_MBOX(((struct releaseRequest*)dataItem)->packageCode);
			SPU_WRITE_OUT_MBOX(((struct releaseRequest*)dataItem)->requestID);
			SPU_WRITE_OUT_MBOX(((struct releaseRequest*)dataItem)->dataItem);
			SPU_WRITE_OUT_MBOX(((struct releaseRequest*)dataItem)->mode);
			SPU_WRITE_OUT_MBOX(((struct releaseRequest*)dataItem)->dataSize);
			SPU_WRITE_OUT_MBOX((int)((struct releaseRequest*)dataItem)->data);			
			break;
		
		default:
			fprintf(stderr, WHERESTR "Unknown package code: %i\n", WHEREARG, ((struct releaseRequest*)dataItem)->packageCode);
	}
#ifdef DEBUG_PACKAGES
	printf(WHERESTR "Sent package with type %s (%d) with reqId: %d, possible id: %d\n", WHEREARG, PACKAGE_NAME(((struct createRequest*)dataItem)->packageCode), ((struct createRequest*)dataItem)->packageCode, ((struct createRequest*)dataItem)->requestID, ((struct createRequest*)dataItem)->dataItem);
#endif
}

/*
void sendInvalidateResponse(unsigned int requestID) {
	//printf(WHERESTR "Sending invalidateResponse for requestid: %i\n", WHEREARG, requestID);
#ifdef DEBUG_PACKAGES
	printf(WHERESTR "Sending request type InvalidateResponse with reqId: %d\n", WHEREARG, requestID);
#endif
	
	SPU_WRITE_OUT_MBOX(PACKAGE_INVALIDATE_RESPONSE);
	SPU_WRITE_OUT_MBOX(requestID);
}
*/

void invalidate(GUID id, unsigned int requestID) {
	size_t i;
	int inTransit = 0;
	//printf(WHERESTR "Trying to invalidate data with id: %i (%d:%d)\n", WHEREARG, id, thread_id, thread_no);

	if(ht_member(itemsById, (void*)id)) {
		dataObject object = ht_get(itemsById, (void*)id);
		//printf(WHERESTR "Data with id: %i is allocated but has been released, count: %d\n", WHEREARG, id, object->count);
		
			
	for(i = 0; i < MAX_PENDING_REQUESTS; i++)
		if (GETBIT(i, MAX_PENDING_REQUESTS, pendingMap) && pendingRequestBuffer[i].object == object)
			if (pendingRequestBuffer[i].state == ASYNC_STATUS_DMA_PENDING || pendingRequestBuffer[i].state == ASYNC_STATUS_WRITE_READY)
			{
				inTransit = 1;
				break;
			}
	
	
		
		if (object->count == 0 && !inTransit)
		{
			ht_delete(itemsById, (void*)id);
			ht_delete(itemsByPointer, object->data);
			removeAllocatedID(id);
			
			for(i = 0; i < MAX_PENDING_REQUESTS; i++)
				if (GETBIT(i, MAX_PENDING_REQUESTS, pendingMap) && pendingRequestBuffer[i].object == object)
					pendingRequestBuffer[i].object = NULL;
				
			
			//printf(WHERESTR "Invalidated id %d (%d:%d)\n", WHEREARG, object->id, thread_id, thread_no);
			FREE_ALIGN(object->data);
			object->data = NULL;
			FREE(object);		
			object = NULL;
			
			//sendInvalidateResponse(requestID);
			
			pendingInvalidateContains(id, 1);
			//if (index > 0)
				//sendInvalidateResponse(pendingInvalidates[index].requestID);
		}
		else
		{
			//printf(WHERESTR "Inserting a pending invalidate for id: %d (%d:%d)\n", WHEREARG, id, thread_id, thread_no);
			if (!pendingInvalidateContains(id, 0))
			{
				unsigned int pendingIndex = findFreeItem(0, MAX_PENDING_INVALIDATES, &pendingInvalidateMap);
				//printf(WHERESTR "Inserted a pending invalidate before: %d (%d:%d)\n", WHEREARG, pendingInvalidateMap, thread_id, thread_no);
				pendingInvalidates[pendingIndex].guid = id;
				pendingInvalidates[pendingIndex].requestID = requestID;
				//printf(WHERESTR "Inserted a pending invalidate after: %d\n", WHEREARG, pendingInvalidateMap);
			}
		}
	}
	else	
	{
		//printf(WHERESTR "Discarded invalidate message with id: %i (%d:%d)\n", WHEREARG, id, thread_id, thread_no);
		pendingInvalidateContains(id, 1);
		//sendInvalidateResponse(requestID);
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

	processPendingInvalidate(req->id);
	
	//printf(WHERESTR "Processing ACQUIRE package for %d, %d, mode: %d\n", WHEREARG, req->id, resp->requestID, resp->mode);
	
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
			
			//printf(WHERESTR "Re-used object %d in mode %d\n", WHEREARG, req->id, req->mode);
			
			if (req->mode == ACQUIRE_MODE_READ || req->object->count == 0 )
			{
				//printf(WHERESTR "Item %d (%d) was known, returning local copy\n", WHEREARG, req->id, (int)req->object->data);
				req->object->count++;
				req->object->mode = req->mode;
				req->state = ASYNC_STATUS_COMPLETE;
			}
			else if (req->mode == ACQUIRE_MODE_WRITE || req->mode == ACQUIRE_MODE_WRITE_OK)
			{
				//printf(WHERESTR "Blocked object %d\n", WHEREARG, req->id);
				req->state = ASYNC_STATUS_BLOCKED;
				req->object->mode = ACQUIRE_MODE_BLOCKED;

				if (req->mode == ACQUIRE_MODE_WRITE)
					req->object->ready = FALSE;					
				else 
				{
					req->object->ready = TRUE;
					req->mode = ACQUIRE_MODE_WRITE;
				}
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


	// TEST AREA
	void* temp = NULL;
	int i;
	transfer_size = ALIGNED_SIZE(resp->dataSize);

	if ((temp = MALLOC_ALIGN(transfer_size, 7)) == NULL) {
		printf(WHERESTR "Pending invalidate (bitmap): %d, itemsByPointer: %d, itemsById: %d, allocatedId: %d, transfersize %i\n", WHEREARG, pendingInvalidateMap, itemsByPointer->fill, itemsById->fill, queue_count(allocatedID), transfer_size);
/*
  
		unsigned int size = transfer_size;
		unsigned int totSize = 0;
		unsigned int segments = 0;
		void* ptr;
		while(totSize != transfer_size && size != 0)
		{
			size = transfer_size;
			while(size != 0 && (ptr = MALLOC_ALIGN(size, 7)) == NULL)
				size--;
	
			if(size > 0)
			{		
				printf("Allocated %u at %u to %u\n", size, (unsigned int)ptr, (unsigned int)ptr + size);
				segments++;
				totSize += size;
			}
		}
			
		printf("Could allocate %i bytes in %i segments (%i bytes)\n", totSize, segments, transfer_size); 
*/
		
		/*for(i = 0; i < 5000; i++)
			printf("Force print\n");*/

		REPORT_ERROR("Failed to allocate memory on SPU");
	}
	
	
	// Make datastructures for later use
	req->object = MALLOC(sizeof(struct dataObjectStruct));
	if (req->object == NULL)
		REPORT_ERROR("Failed to allocate memory on SPU");

	req->object->id = req->id;
	req->object->EA = resp->data;
	req->object->size = resp->dataSize;
	if (resp->mode == ACQUIRE_MODE_CREATE || resp->mode == ACQUIRE_MODE_WRITE_OK) {
		//printf(WHERESTR "Object was in create mode %d\n", WHEREARG, req->id);
		req->object->mode = ACQUIRE_MODE_WRITE;	
		req->object->ready = TRUE;
		
	} else {
		//printf(WHERESTR "Object was in read or write mode %d\n", WHEREARG, req->id);
		req->object->mode = resp->mode;
		req->object->ready = FALSE;
	}
	req->object->count = 1;
	
	req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
	req->size = req->object->size;

	req->object->data = temp;
	temp = NULL;

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
	
	//printf(WHERESTR "Registered dataobject with id: %d and pointer %d\n", WHEREARG, req->object->id, (int)req->object->data);
#ifdef TIMER
	beforeDMA = ReadClock();
	if (READWRITE == ACQUIRE_MODE_READ)
		beforeDMAReadRead = (((acquireReadCount - 1) / acquireReadCount) * beforeDMAReadRead) + ((1 / acquireReadCount) * (beforeDMA - start));	
	else if (READWRITE == ACQUIRE_MODE_WRITE)
		beforeDMAReadWrite = (((acquireWriteCount - 1) / acquireWriteCount) * beforeDMAReadWrite) + ((1 / acquireWriteCount) * (beforeDMA - start));	
#endif

#ifdef FENCED_NOTIFY
	StartDMAReadTransfer(req->object->data, (unsigned int)resp->data, transfer_size, req->dmaNo, (unsigned int)resp->dmaComplete);
#else
	StartDMAReadTransfer(req->object->data, (unsigned int)resp->data, transfer_size, req->dmaNo, 0);
#endif

#ifdef TIMER
	afterDMA = ReadClock();

	if (READWRITE == ACQUIRE_MODE_READ)
		dmaReadRead = (((acquireReadCount - 1) / acquireReadCount) * dmaReadRead) + ((1 / acquireReadCount) * (afterDMA - beforeDMA));	
	else if (READWRITE == ACQUIRE_MODE_WRITE)
		dmaReadWrite = (((acquireWriteCount - 1) / acquireWriteCount) * dmaReadWrite) + ((1 / acquireWriteCount) * (afterDMA - beforeDMA));
#endif

}

void readMailbox() {
	unsigned int requestID;
	unsigned long datasize;
	GUID itemid;
	void* datapointer;
	void* dmaComplete;
	int mode;
		
	struct acquireResponse* acqResp;
	struct releaseResponse* relResp;
	
	int packagetype = spu_read_in_mbox();
	
#ifdef DEBUG_PACKAGES
	printf(WHERESTR "Read package with type %s (%d)\n", WHEREARG, PACKAGE_NAME(packagetype), packagetype);
#endif
	
	switch(packagetype)
	{
		case PACKAGE_TERMINATE_RESPONSE:
			terminated = 1;
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			//printf(WHERESTR "ACQUIRE package recieved\n", WHEREARG);
			
			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();
			mode = (int)spu_read_in_mbox();
			datasize = spu_read_in_mbox();
			datapointer = (void*)spu_read_in_mbox();
		
			if (mode == ACQUIRE_MODE_READ)
				dmaComplete = (void*)spu_read_in_mbox();
			else
				dmaComplete = NULL;
				
			if (!GETBIT(requestID, MAX_PENDING_REQUESTS, pendingMap)) {
				REPORT_ERROR("Recieved a request with an unexpected requestID\n");
			} else {
				
				//printf(WHERESTR "ID was %d, mode was: %d\n", WHEREARG, itemid, mode);
				acqResp = (struct acquireResponse*)&pendingRequestBuffer[requestID].request;
				
				acqResp->packageCode = packagetype;									
				acqResp->requestID = requestID; 
				acqResp->dataItem = itemid;
				acqResp->mode = mode;
				acqResp->dataSize = datasize;
				acqResp->data = datapointer;
				acqResp->dmaComplete = dmaComplete;

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

				if (pendingRequestBuffer[requestID].object->mode == ACQUIRE_MODE_WRITE)
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
			
			//printf(WHERESTR "INVALIDATE package read, id: %d, requestID: %d\n", WHEREARG, itemid, requestID);

			invalidate(itemid, requestID);
			break;
		case PACKAGE_WRITEBUFFER_READY:
			//printf(WHERESTR "WRITEBUGGER_READY package recieved\n", WHEREARG);
			requestID = spu_read_in_mbox();
			itemid = spu_read_in_mbox();

			if (ht_member(itemsById, (void*)itemid)) {
				dataObject object = ht_get(itemsById, (void*)itemid);
				object->ready = TRUE;
			}
			else {
				REPORT_ERROR("Writebuffer signal recieved but package was unknown");
			}
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

	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		CLEARBIT(nextId, MAX_PENDING_REQUESTS, pendingMap);
		return 0;
	}

	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exeeds PAGE_TABLE_SIZE");
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
	req->mode = ACQUIRE_MODE_WRITE;

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
	
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		CLEARBIT(nextId, MAX_PENDING_REQUESTS, pendingMap);
		return 0;
	}

	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exeeds PAGE_TABLE_SIZE");
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
		if (type == ACQUIRE_MODE_READ && (object->count == 0 || object->mode == ACQUIRE_MODE_READ) && !pendingInvalidateContains(id, 0))
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
	
	if (type == ACQUIRE_MODE_WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		request->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	} else if (type == ACQUIRE_MODE_READ) {
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

void startWriteDMATransfer(pendingRequest req, unsigned int nextId) {
	
	unsigned int transfersize;
	req->state = ASYNC_STATUS_DMA_PENDING;
	
	transfersize = ALIGNED_SIZE(req->object->size);
#ifdef TIMER	
	beforeDMA = ReadClock();
	
	beforeDMAWrite = (((releaseWriteCount - 1) / releaseWriteCount) * beforeDMAWrite) + ((1 / releaseWriteCount) * (beforeDMA - start));	
#endif	

	StartDMAWriteTransfer(req->object->data, (int)req->object->EA, transfersize, req->dmaNo);

/*
#ifdef TIMER
	afterDMA = ReadClock();
	dmaWrite = (((releaseWriteCount - 1) / releaseWriteCount) * dmaWrite) + ((1 / releaseWriteCount) * (afterDMA - beforeDMA));
#endif	
*/	
	//printf(WHERESTR "DMA release for %d in write mode\n", WHEREARG, req->id); 

	struct releaseRequest* request = (struct releaseRequest*)&req->request;

	request->packageCode = PACKAGE_RELEASE_REQUEST;
	request->data = req->object->EA; 
	request->requestID = nextId;
	request->dataItem = req->object->id;
	request->mode = ACQUIRE_MODE_WRITE;
	request->dataSize = req->object->size;
	request->offset = 0;
}

/*
void readSignal()
{
	int itemid;
	//printf(WHERESTR "Checking signal status\n", WHEREARG);
	int temp = spu_stat_signal1();
	//printf(WHERESTR "Signal status was %i\n", WHEREARG, temp); 
	if (temp == 1)
	{
		//printf(WHERESTR "Reading signal\n", WHEREARG);
		itemid = spu_read_signal1();
		//printf(WHERESTR "Signal was: %i\n", WHEREARG, itemid);
		if (ht_member(itemsById, (void*)itemid)) {
			dataObject object = ht_get(itemsById, (void*)itemid);
			object->ready = TRUE;
		}
		else {
			REPORT_ERROR("Writebuffer signal recieved but package was unknown");
		}	
	}
}
*/

unsigned int beginRelease(void* data)
{
	unsigned int nextId = findFreeItem(&requestNo, MAX_PENDING_REQUESTS, &pendingMap);

	dataObject object;
	pendingRequest req;
	size_t i;
	
	//printf(WHERESTR "Starting a release\n", WHEREARG); 
	//printf(WHERESTR "Releasing pointer: %d\n", WHEREARG, data);
	
	if (itemsById == NULL)
	{
		REPORT_ERROR("Initialize must be called");
		return 0;
	}
	req = &pendingRequestBuffer[nextId];

	if (ht_member(itemsByPointer, data)) {
		object = ht_get(itemsByPointer, data);

		if (object->id == PAGE_TABLE_ID)
		{
			REPORT_ERROR("cannot request pagetable");
			CLEARBIT(nextId, MAX_PENDING_REQUESTS, pendingMap);
			return 0;
		}
	
		//printf(WHERESTR "Starting a release for id: %d (%d:%d)\n", WHEREARG, object->id, thread_id, thread_no); 


		if (object->mode == ACQUIRE_MODE_WRITE) {
#ifdef TIMER
			READWRITE = ACQUIRE_MODE_WRITE;
			releaseWriteCount++;
#endif			
			//printf(WHERESTR "Starting a release for %d in write mode (ls: %d, data: %d)\n", WHEREARG, (int)object->id, (int)object->data, (int)data); 
			req->id = object->id;
			req->dmaNo = NEXT_SEQ_NO(DMAGroupNo, MAX_DMA_GROUPS);
			req->object = object;
			req->size = object->size;

			// If we use signaling instead of mailbox messages.			
			//readSignal();

			if(object->ready == FALSE) {
				//printf(WHERESTR "Starting a release for %d in write mode, awaiting write signal\n", WHEREARG, (int)object->id);
				req->state = ASYNC_STATUS_WRITE_READY;				
				return nextId;
			}
			
			startWriteDMATransfer(req, nextId);
							
		} else if (object->mode == ACQUIRE_MODE_READ || object->mode == ACQUIRE_MODE_BLOCKED) {
#ifdef TIMER			
			READWRITE = ACQUIRE_MODE_READ;
			releaseReadCount++;
#endif			
			//printf(WHERESTR "Starting a release for %d in read mode (ls: %d, data: %d)\n", WHEREARG, (int)object->id, (int)object->data, (int)data); 

			object->count--;
			
			if (object->mode == ACQUIRE_MODE_BLOCKED) {
				if (object->count == 0)
				{
					for(i = 0; i < MAX_PENDING_REQUESTS; i++)
						if (GETBIT(i, MAX_PENDING_REQUESTS, pendingMap) && pendingRequestBuffer[i].id == object->id)
						{
							pendingRequestBuffer[i].state = ASYNC_STATUS_COMPLETE;
							object->count++;
							object->mode = ACQUIRE_MODE_WRITE;
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
			req->mode = ACQUIRE_MODE_READ;
			
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

void initialize()
{
/*	
	unsigned int transfer_size = 200*1024;
	unsigned int size = transfer_size;
	unsigned int totSize = 0;
	unsigned int segments = 0;
	void* ptr;
	while(totSize != transfer_size && size != 0)
	{
		size = transfer_size;
		while(size != 0 && (ptr = MALLOC_ALIGN(size, 7)) == NULL)
			size = size - 1024;

		if(size > 0)
		{		
			printf("Allocated %u at %u to %u\n", size, (unsigned int)ptr, (unsigned int)ptr + size);
			segments++;
			totSize += size;
		}
	}
		
	printf("Could allocate %i bytes in %i segments (%i bytes)\n", totSize, segments, transfer_size);
	sleep(2);
*/
#ifdef TIMER
	spu_clock_start();
#endif	
	//printf(WHERESTR "Setting up malloc\n", WHEREARG);
	spu_dsmcbe_memory_setup();
	
	terminated = 0;
	itemsByPointer = ht_create(60, lessint, hashfc);
	itemsById = ht_create(60, lessint, hashfc);
	allocatedID = queue_create();
	//itx = ht_iter_create(itemsById);
}

void terminate() {
	SPU_WRITE_OUT_MBOX(PACKAGE_TERMINATE_REQUEST);
	while(!terminated)
		readMailbox();
		
	cleanup();
	
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
							
				#ifndef FENCED_NOTIFY 
				if (req->mode == ACQUIRE_MODE_READ)
				{
					//printf(WHERESTR "Sending DMA complete flag %d\n", WHEREARG, ((struct acquireRequest*)(&(req->request)))->dataItem);
					SPU_WRITE_OUT_MBOX(PACKAGE_DMA_TRANSFER_COMPLETE);
					SPU_WRITE_OUT_MBOX(((struct acquireRequest*)(&(req->request)))->dataItem);
				}
				#endif
			}		
		} else if (req->state == ASYNC_STATUS_WRITE_READY) {
			//readSignal();
			if (req->object->ready == TRUE) {				
				startWriteDMATransfer(req, requestNo);	
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

#ifdef TIMER
	afterDMA = ReadClock();

	if (StatType == 1)
	{
		if (READWRITE == ACQUIRE_MODE_READ)
			dmaReadRead = (((acquireReadCount - 1) / acquireReadCount) * dmaReadRead) + ((1 / acquireReadCount) * (afterDMA - beforeDMA));	
		else if (READWRITE == ACQUIRE_MODE_WRITE)
			dmaReadWrite = (((acquireWriteCount - 1) / acquireWriteCount) * dmaReadWrite) + ((1 / acquireWriteCount) * (afterDMA - beforeDMA));
	}
	else if (StatType == 2 && READWRITE == ACQUIRE_MODE_WRITE)
	{
		dmaWrite = (((releaseWriteCount - 1) / releaseWriteCount) * dmaWrite) + ((1 / releaseWriteCount) * (afterDMA - beforeDMA));		
	}
#endif


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
			//printf(WHERESTR "Dataobject %d (%d) was not registered anymore\n", WHEREARG, req->id, (int)req->object->data); 
			removeAllocatedID(req->object->id);
			//printf(WHERESTR "Removed id %d\n", WHEREARG, req->object->id);

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
#ifdef TIMER	
	READWRITE = type;

	if (READWRITE == ACQUIRE_MODE_READ)
		acquireReadCount++;
	else if (READWRITE == ACQUIRE_MODE_WRITE)
		acquireWriteCount++;
		
	start = ReadClock();
	StatType = 1;
#endif

	void* temp = endAsync(beginAcquire(id, type), size);		

#ifdef TIMER
	StatType = 5;
	end = ReadClock();
	if (READWRITE == ACQUIRE_MODE_READ)
	{
		afterDMAReadRead = (((acquireReadCount - 1) / acquireReadCount) * afterDMAReadRead) + ((1 / acquireReadCount) * (end - afterDMA));
		acquireRead = (((acquireReadCount - 1) / acquireReadCount) * acquireRead) + ((1 / acquireReadCount) * (end - start));
	}
	else if (READWRITE == ACQUIRE_MODE_WRITE)
	{
		afterDMAReadWrite = (((acquireWriteCount - 1) / acquireWriteCount) * afterDMAReadWrite) + ((1 / acquireWriteCount) * (end - afterDMA));
		acquireWrite = (((acquireWriteCount - 1) / acquireWriteCount) * acquireWrite) + ((1 / acquireWriteCount) * (end - start));
	}
#endif	
	return temp;	
}

void release(void* data){
#ifdef TIMER
	start = ReadClock();
	StatType = 2;
#endif
	
	endAsync(beginRelease(data), NULL);

#ifdef TIMER
	StatType = 5;	
	end = ReadClock();
	if (READWRITE == ACQUIRE_MODE_READ)
	{
		releaseRead = (((releaseReadCount - 1) / releaseReadCount) * releaseRead) + ((1 / releaseReadCount) * (end - start));	
	}
	else if (READWRITE == ACQUIRE_MODE_WRITE)
	{
		afterDMAWrite = (((releaseWriteCount - 1) / releaseWriteCount) * afterDMAWrite) + ((1 / releaseWriteCount) * (end - afterDMA));
		releaseWrite = (((releaseWriteCount - 1) / releaseWriteCount) * releaseWrite) + ((1 / releaseWriteCount) * (end - start));

		if (releaseWrite < dmaWrite)
			printf("ERROR2\n");			
	}
#endif	
}

#ifdef TIMER
void getStats()
{
	printf("SPE: %llu - total %llu\n", speID, ReadClock());
	printf("SPE: %llu - acquireReadCount %f\n", speID, acquireReadCount);
	printf("SPE: %llu - acquireRead %u\n", speID, acquireRead);
	printf("SPE: %llu - beforeDMAReadRead %u\n", speID, beforeDMAReadRead);
	printf("SPE: %llu - dmaReadRead %u\n", speID, dmaReadRead);
	printf("SPE: %llu - afterDMAReadRead %u\n", speID, afterDMAReadRead);
	
	printf("SPE: %llu - acquireWriteCount %f\n", speID, acquireWriteCount);
	printf("SPE: %llu - acquireWrite %u\n", speID, acquireWrite);
	printf("SPE: %llu - beforeDMAReadWrite %u\n", speID, beforeDMAReadWrite);
	printf("SPE: %llu - dmaReadWrite %u\n", speID, dmaReadWrite);
	printf("SPE: %llu - afterDMAReadWrite %u\n", speID, afterDMAReadWrite);
	
	printf("SPE: %llu - releaseReadCount %f\n", speID, releaseReadCount);
	printf("SPE: %llu - releaseRead %u\n", speID, releaseRead);
	
	printf("SPE: %llu - releaseWriteCount %f\n", speID, releaseWriteCount);
	printf("SPE: %llu - releaseWrite %u\n", speID, releaseWrite);
	printf("SPE: %llu - beforeDMAWrite %u\n", speID, beforeDMAWrite);
	printf("SPE: %llu - dmaWrite %u\n", speID, dmaWrite);
	printf("SPE: %llu - afterDMAWrite %u\n", speID, afterDMAWrite);	
}
#endif

void* create(GUID id, unsigned long size)
{
	return endAsync(beginCreate(id, size), NULL);
}

int hasMemoryInitialized = 0;

void* spu_dsmcbe_memory_malloc(unsigned long size)
{
	void* data;
	
	if (hasMemoryInitialized == 0)
	{
		REPORT_ERROR("malloc called before initialize");
		spu_dsmcbe_memory_setup();
	}

	//printf(WHERESTR "Calling malloc for %d bytes\n", WHEREARG, size);
	spu_write_out_intr_mbox(PACKAGE_SPU_MEMORY_MALLOC);
	spu_write_out_intr_mbox(size);
	data = (void*)spu_read_signal1();
	//data = (void*)spu_read_in_mbox();
	//printf(WHERESTR "Malloc for %d bytes gave %d\n", WHEREARG, size, (unsigned int)data);
	return data;
}

void spu_dsmcbe_memory_free(void* data)
{
	if (hasMemoryInitialized == 0)
	{
		REPORT_ERROR("malloc called before initialize");
		spu_dsmcbe_memory_setup();
	}
	spu_write_out_intr_mbox(PACKAGE_SPU_MEMORY_FREE);
	spu_write_out_intr_mbox((unsigned int)data);
}

extern int _end;



void spu_dsmcbe_memory_setup()
{
	void* start;
	unsigned int size;
	
	//REPORT_ERROR("IN INITIALIZE");

	if (hasMemoryInitialized)
		REPORT_ERROR("initialize called twice");
	
	register volatile unsigned int *r1 asm("1");
	
	//printf(WHERESTR "SBRK(0): %d, &_end: %d, r1:%d\n", WHEREARG, (unsigned int)sbrk(0), (unsigned int)&_end, *r1);

	size = *r1 - ((unsigned int)sbrk(0));
	size -= 8 * 1024; //Reserve space for stack
	start = malloc_align(size, 7);

	//printf(WHERESTR "SPU Reports having %d bytes free memory, at %d\n", WHEREARG, size, (unsigned int)start);
	if (start == NULL)
		REPORT_ERROR("Failed to allocated the desired amount of bytes");
	
	spu_write_out_intr_mbox(PACKAGE_SPU_MEMORY_SETUP);
	spu_write_out_intr_mbox((unsigned int)start);
	spu_write_out_intr_mbox(size);
	
	hasMemoryInitialized = 1;
}

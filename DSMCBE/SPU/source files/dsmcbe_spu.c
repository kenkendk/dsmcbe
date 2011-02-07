#include <libmisc.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <SPUEventHandler_extrapackages.h>
#include <dsmcbe_spu_internal.h>
#include <SPUThreads.h>
#include <spu_mfcio.h>
#include <malloc_align.h>

//#define DEBUG_COMMUNICATION
//Activating this makes the code use polling rather than just waiting for a mailbox message.
//The current Cell BE implementation seems to use busy waiting in either case though.
//Disable this if there are long wait periods in the program.
//#define TIMEOUT_DETECTION

int dsmcbe_thread_count;

//The stack space allocated for the main thread
#define MAIN_THREAD_STACKSPACE (1024 * 6)

#include "../header files/dsmcbe_spu.h"
#include "../header files/datapackages.h"
#include "../header files/debug.h"

unsigned int spu_dsmcbe_nextRequestNo = 0;
unsigned int spu_dsmcbe_initialized = FALSE;

struct spu_dsmcbe_mailbox_args {
	unsigned int next;
	unsigned int dummy1;
	unsigned int dummy2;
	unsigned int dummy3;

	unsigned int packageCode;
	unsigned int requestId;
	unsigned int data;
	unsigned int size;
	unsigned int id;
	unsigned int mode;
	unsigned int channels;
	unsigned int channelId;
} __attribute__ ((aligned (16)));

struct spu_dsmcbe_mailbox_args spu_dsmcbe_mbox_buffer[MAILBOX_BUFFER_SIZE * sizeof(struct spu_dsmcbe_mailbox_args)];
volatile unsigned int spu_dsmcbe_mbox_buffer_full = FALSE;

unsigned int spu_dsmcbe_mbox_next = 0;

void spu_dsmcbe_sendMboxMessage(unsigned int packageCode, unsigned int requestId, unsigned int data, unsigned int size, unsigned int id, unsigned int mode, unsigned int channels, unsigned int channelId) {

	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	//printf(WHERESTR "In sendMbox\n", WHEREARG);

	//If the buffer is currently full, just wait until there is space left
	if (spu_dsmcbe_mbox_buffer_full == TRUE) {
		printf(WHERESTR "Mailbox buffer is full, waiting\n", WHEREARG);

		while(spu_dsmcbe_mbox_buffer_full == TRUE)
			dsmcbe_thread_yield();
	}

	//printf(WHERESTR "Buffer is not full\n", WHEREARG);

	int next = (spu_dsmcbe_mbox_next + 1) % MAILBOX_BUFFER_SIZE;

	struct spu_dsmcbe_mailbox_args* curEl = &spu_dsmcbe_mbox_buffer[spu_dsmcbe_mbox_next];
	struct spu_dsmcbe_mailbox_args* nextEl = &spu_dsmcbe_mbox_buffer[next];

	//If there is no space for this entry, wait until there is
	if (curEl->next != 0 || nextEl->next != 0) {
		printf(WHERESTR "Waiting for entry to become free, curEl @%d, curEl->next @%d, nextEl @%d, nextEl->next @%d\n", WHEREARG, (unsigned int)curEl, curEl->next, (unsigned int)nextEl, nextEl->next);

		spu_dsmcbe_mbox_buffer_full = TRUE;
		while (curEl->next != 0 || nextEl->next != 0)
			dsmcbe_thread_yield();

		spu_dsmcbe_mbox_buffer_full = FALSE;
	}

	//printf(WHERESTR "Setting up mailbox entry %d in ls @%d, next is %d @%d\n", WHEREARG, spu_dsmcbe_mbox_next, (unsigned int)curEl, next, (unsigned int)nextEl);

	curEl->packageCode = packageCode;
	curEl->requestId = requestId;
	curEl->data = data;
	curEl->size = size;
	curEl->id = id;
	curEl->mode = mode;
	curEl->channels = channels;
	curEl->channelId = channelId;

	//Important: Because this is essentially dirty write, the "busy" state must be written last
	curEl->next = (unsigned int)nextEl;

	//Signal PPE
	if (SPU_STAT_OUT_MBOX() != 0) {
		//printf(WHERESTR "Signaling SPE with address @%d\n", WHEREARG, (unsigned int)curEl);
		SPU_WRITE_OUT_MBOX((unsigned int)curEl);
	}

	spu_dsmcbe_mbox_next = next;
}

//This function gets the next available request number, and sets the response flag to "not ready"
unsigned int spu_dsmcbe_getNextReqNo(unsigned int requestCode)
{
	//SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	size_t i;
	unsigned int value;
	for(i = 0; i < MAX_PENDING_REQUESTS; i++) {

		if (spu_dsmcbe_pendingRequests[spu_dsmcbe_nextRequestNo % MAX_PENDING_REQUESTS].requestCode == 0)
		{
			value = spu_dsmcbe_nextRequestNo % MAX_PENDING_REQUESTS;
			spu_dsmcbe_pendingRequests[value].requestCode = requestCode;
			spu_dsmcbe_pendingRequests[value].responseCode = 0;
			spu_dsmcbe_nextRequestNo = (value + 1) % MAX_PENDING_REQUESTS;
			return value;
		}
		else
		{
			spu_dsmcbe_nextRequestNo++;
		}
	}
	REPORT_ERROR("No available request slots found, consider raising the MAX_PENDING_REQUESTS");
	return UINT_MAX;
}

void spu_dsmcbe_dumpState(unsigned int state) {

	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	fprintf(stderr, WHERESTR "* DEADLOCK * dumping SPU %d pending requests\n", WHEREARG, state);
	size_t i;
	for(i = 0; i < MAX_PENDING_REQUESTS; i++) {
		if (spu_dsmcbe_pendingRequests[i].requestCode != 0) {
			fprintf(stderr, " * DEADLOCK * " WHERESTR "%d spu_preq found\n\tRequestId: %d\n\tRequestCode: %s (%d)\n\tResponseCode: %s (%d)\n", WHEREARG, state, (unsigned int)i, PACKAGE_NAME(spu_dsmcbe_pendingRequests[i].requestCode), spu_dsmcbe_pendingRequests[i].requestCode, PACKAGE_NAME(spu_dsmcbe_pendingRequests[i].responseCode), spu_dsmcbe_pendingRequests[i].responseCode);
		}
	}
}

//Reads mailbox messages, blocking
void spu_dsmcbe_readMailbox() {

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Reading mailbox, blocking\n", WHEREARG);
#endif

	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);
	unsigned int requestID = spu_read_in_mbox();
	
	//UINT_MAX is used to signal non-spe initiated messages
	if (requestID == UINT_MAX)
	{
		//printf(WHERESTR "Got a UINT_MAX request\n", WHEREARG);
		//Using "requestID" to store packageCode
		requestID = spu_read_in_mbox();
		//printf(WHERESTR "Got a %s (%d) request\n", WHEREARG, PACKAGE_NAME(requestID), requestID);
		switch(requestID /*actually package code*/)
		{
			case PACKAGE_SPU_CSP_CHANNEL_POISON_DIRECT:
				//Extract the channel ID
				requestID = spu_read_in_mbox();
				//printf(WHERESTR "Got a poison request for channel %d\n", WHEREARG, requestID);
				dsmcbe_csp_channel_poison_internal(requestID);
				break;
			case PACKAGE_SPU_CSP_DUMP_STATE:
				//Extract the context pointer
				requestID = spu_read_in_mbox();
				spu_dsmcbe_dumpState(requestID);
				break;
		}
		return;
	}

	if (requestID > MAX_PENDING_REQUESTS)
	{
		REPORT_ERROR2("Invalid request id detected: %d", requestID);
		return;
	}
	
	if (spu_dsmcbe_pendingRequests[requestID].requestCode == 0)
		REPORT_ERROR2("Request not found in PendingRequests: %d", requestID)

	dsmcbe_thread_set_ready_by_requestId(requestID);

#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Read package %s (%d) with requestId %d\n", WHEREARG, PACKAGE_NAME(spu_dsmcbe_pendingRequests[requestID].requestCode), spu_dsmcbe_pendingRequests[requestID].requestCode, requestID);
#endif

	switch(spu_dsmcbe_pendingRequests[requestID].requestCode)
	{
		case PACKAGE_TERMINATE_REQUEST:
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "TERMINATE package received\n", WHEREARG);
#endif
			spu_dsmcbe_initialized = FALSE;
			spu_dsmcbe_pendingRequests[requestID].responseCode = PACKAGE_TERMINATE_RESPONSE;
			break;
			
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "ACQUIRE package received\n", WHEREARG);
#endif
			
			spu_dsmcbe_pendingRequests[requestID].responseCode = PACKAGE_ACQUIRE_RESPONSE;
			spu_dsmcbe_pendingRequests[requestID].pointer = (void*)spu_read_in_mbox(); 
			spu_dsmcbe_pendingRequests[requestID].size = spu_read_in_mbox();
			break;			
	
		case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
#ifdef DEBUG_COMMUNICATION	
			printf(WHERESTR "MALLOC package received\n", WHEREARG);
#endif
			spu_dsmcbe_pendingRequests[requestID].responseCode = PACKAGE_SPU_MEMORY_MALLOC_RESPONSE;
			spu_dsmcbe_pendingRequests[requestID].pointer = (void*)spu_read_in_mbox();
			break;			
		
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			spu_dsmcbe_pendingRequests[requestID].responseCode = PACKAGE_ACQUIRE_BARRIER_RESPONSE;
			spu_dsmcbe_pendingRequests[requestID].pointer = NULL;
			break;

		case PACKAGE_CSP_CHANNEL_CREATE_REQUEST:
		case PACKAGE_CSP_CHANNEL_POISON_REQUEST:
		case PACKAGE_CSP_CHANNEL_READ_REQUEST:
		case PACKAGE_CSP_CHANNEL_WRITE_REQUEST:
		case PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST:
		case PACKAGE_SPU_CSP_ITEM_FREE_REQUEST:
		case PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST:
		case PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST:
			spu_dsmcbe_pendingRequests[requestID].responseCode = spu_read_in_mbox();

			switch(spu_dsmcbe_pendingRequests[requestID].responseCode)
			{
				case PACKAGE_SPU_CSP_CHANNEL_SETUP_DIRECT:
					//Extract channelId
					spu_dsmcbe_pendingRequests[requestID].responseCode = spu_read_in_mbox();
					//Extract the buffer pointer
					spu_dsmcbe_pendingRequests[requestID].size = spu_read_in_mbox();
					dsmcbe_csp_setupDirectChannel(requestID, spu_dsmcbe_pendingRequests[requestID].responseCode, (void*)spu_dsmcbe_pendingRequests[requestID].size);
					break;
				case PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE:
					spu_dsmcbe_pendingRequests[requestID].size = spu_read_in_mbox();
					break;
				case PACKAGE_CSP_CHANNEL_READ_RESPONSE:
				case PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE:
				case PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE:
					spu_dsmcbe_pendingRequests[requestID].pointer = (void*)spu_read_in_mbox();
					spu_dsmcbe_pendingRequests[requestID].size = spu_read_in_mbox();
					break;
				case PACKAGE_CSP_CHANNEL_WRITE_REQUEST:
					spu_dsmcbe_pendingRequests[requestID].pointer = (void*)spu_read_in_mbox();
					spu_dsmcbe_pendingRequests[requestID].size = spu_read_in_mbox();
					dsmcbe_csp_handleCrossWrite(requestID);
					break;
				case PACKAGE_NACK:
					break;
			}
			break;

		default:
			//fprintf(stderr, WHERESTR "Unknown package received: %i, requestId: %d, message: %s\n", WHEREARG, spu_dsmcbe_pendingRequests[requestID].requestCode, requestID, strerror(errno));
			printf(WHERESTR "Unknown package received: %i, requestId: %d, message: %s\n", WHEREARG, spu_dsmcbe_pendingRequests[requestID].requestCode, requestID, strerror(errno));
	};	
}

//Initiates a create operation
unsigned int spu_dsmcbe_create_begin(GUID id, unsigned long size)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("Cannot request object table");
		return UINT_MAX;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		printf("ID was %u\n", id);
		REPORT_ERROR("ID was larger than object table size");
		return UINT_MAX;
	}
	
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return UINT_MAX;
	}
	
	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CREATE_REQUEST);
	if (nextId == UINT_MAX)
		return UINT_MAX;

	spu_dsmcbe_sendMboxMessage(spu_dsmcbe_pendingRequests[nextId].requestCode, nextId, 0, size, id, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "CREATE package sent\n", WHEREARG);
#endif
	
	return nextId;
}

unsigned int spu_dsmcbe_acquire_barrier_begin(GUID id)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("Cannot request object table");
		return UINT_MAX;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR("ID was larger than object table size");
		return UINT_MAX;
	}
	
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return UINT_MAX;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_ACQUIRE_BARRIER_REQUEST);
	if (nextId == UINT_MAX)
		return UINT_MAX;

	spu_dsmcbe_sendMboxMessage(spu_dsmcbe_pendingRequests[nextId].requestCode, nextId, 0, 0, id, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "ACQUIRE_BARRIRER package sent, id: %d\n", WHEREARG, nextId);
#endif

	return nextId;
	
}

//Initiates an acquire operation
unsigned int spu_dsmcbe_acquire_begin(GUID id, int type)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("Cannot request object table");
		return UINT_MAX;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR2("ID was larger than object table size: %d", id);
		return UINT_MAX;
	}
	
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return UINT_MAX;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(type == ACQUIRE_MODE_READ ? PACKAGE_ACQUIRE_REQUEST_READ : PACKAGE_ACQUIRE_REQUEST_WRITE);
	if (nextId == UINT_MAX)
		return UINT_MAX;

	spu_dsmcbe_sendMboxMessage(spu_dsmcbe_pendingRequests[nextId].requestCode, nextId, 0, 0, id, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "ACQUIRE package sent, id: %d\n", WHEREARG, nextId);
#endif

	return nextId;
}

//Initiates a release operation
void spu_dsmcbe_release_begin(void* data)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	spu_dsmcbe_sendMboxMessage(PACKAGE_RELEASE_REQUEST, (unsigned int)data, 0, 0, 0, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "RELEASE package sent @: %d\n", WHEREARG, (unsigned int)data);
#endif
}

//Initiates a malloc operation
unsigned int spu_dsmcbe_memory_malloc_begin(unsigned int size)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return UINT_MAX;
	}
	
	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_MEMORY_MALLOC_REQUEST);
	if (nextId == UINT_MAX)
		return UINT_MAX;

	spu_dsmcbe_sendMboxMessage(spu_dsmcbe_pendingRequests[nextId].requestCode, nextId, 0, size, 0, 0, 0, 0);
	
	return nextId;
}

//Initiates a free operation
void spu_dsmcbe_memory_free_begin(void* data)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	spu_dsmcbe_sendMboxMessage(PACKAGE_SPU_MEMORY_FREE, 0, (unsigned int)data, 0, 0, 0, 0, 0);
}



//Cleanly terminates the DSMCBE system
void dsmcbe_terminate()
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	unsigned int reqid = spu_dsmcbe_getNextReqNo(PACKAGE_TERMINATE_REQUEST);
	spu_dsmcbe_sendMboxMessage(PACKAGE_TERMINATE_REQUEST, reqid, 0, 0, 0, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "TERMINATE package sent\n", WHEREARG);
#endif

	
	while(spu_dsmcbe_initialized)
		spu_dsmcbe_getAsyncStatus(reqid);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "TERMINATE response read\n", WHEREARG);
#endif

}

//Returns a SPU_DSMCBE_ASYNC_* value indicating the state of the operation
unsigned int spu_dsmcbe_getAsyncStatus(unsigned int requestNo)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	//Process any pending messages
	while (spu_stat_in_mbox() != 0)
		spu_dsmcbe_readMailbox();
	
	if (requestNo > MAX_PENDING_REQUESTS || spu_dsmcbe_pendingRequests[requestNo].requestCode == 0)
		return SPU_DSMCBE_ASYNC_ERROR;
		
	return spu_dsmcbe_pendingRequests[requestNo].responseCode == 0 ? SPU_DSMCBE_ASYNC_BUSY : SPU_DSMCBE_ASYNC_READY;
}

//Ends an async operation. Blocking if the operation is not complete on entry
void* spu_dsmcbe_endAsync(unsigned int requestNo, unsigned long* size)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	unsigned int status;
	
#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Thread %d is ending async operation for %s (%d) with request id %d\n", WHEREARG, dsmcbe_thread_current_id(), PACKAGE_NAME(spu_dsmcbe_pendingRequests[requestNo].requestCode), spu_dsmcbe_pendingRequests[requestNo].requestCode, requestNo);
#endif
	
	//Process any pending messages
	while (spu_stat_in_mbox() != 0)
		spu_dsmcbe_readMailbox();

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Done reading mailbox\n", WHEREARG);
#endif

	if (spu_dsmcbe_pendingRequests == NULL)
		REPORT_ERROR("PendingRequests is NULL\n");

	if (&spu_dsmcbe_pendingRequests[requestNo] == NULL)
		REPORT_ERROR("Request NOT found in pendingRequest\n");

	if (requestNo > MAX_PENDING_REQUESTS || spu_dsmcbe_pendingRequests[requestNo].requestCode == 0)
	{
		REPORT_ERROR2("endAsync called on non-existing operation: %d", requestNo);
		return NULL;
	}

#ifdef TIMEOUT_DETECTION
	unsigned int i = 0;
#endif

	//Non-threaded is only active during startup
	if (!dsmcbe_thread_is_threaded())
	{
		while ((status = spu_dsmcbe_getAsyncStatus(requestNo)) == SPU_DSMCBE_ASYNC_BUSY)
			spu_dsmcbe_readMailbox();
	}
	else
	{
		while ((status = spu_dsmcbe_getAsyncStatus(requestNo)) == SPU_DSMCBE_ASYNC_BUSY)
		{
			dsmcbe_thread_set_status(dsmcbe_thread_current_id(), requestNo);
	#ifdef TIMEOUT_DETECTION
			if (i++ == 1000000U)
			{
				REPORT_ERROR2("Detected timeout for request id %d", requestNo);
				spu_dsmcbe_sendMboxMessage(PACKAGE_DEBUG_PRINT_STATUS, requestNo, 0, 0, 0, 0, 0, 0);
				i = 0;
			}

			dsmcbe_thread_yield_ready();
	#else
			dsmcbe_thread_yield();
	#endif
		}
	}

	//printf(WHERESTR "Done waiting\n", WHEREARG);

	if (status == SPU_DSMCBE_ASYNC_ERROR)
	{
		REPORT_ERROR("Request ID was accepted, but async status gave error");
		return NULL;
	}

#ifdef DEBUG_COMMUNICATION
	printf(WHERESTR "Got result %d\n", WHEREARG, (unsigned int)spu_dsmcbe_pendingRequests[requestNo].pointer);
#endif

	if (size != NULL)
	{
		if (spu_dsmcbe_pendingRequests[requestNo].responseCode == PACKAGE_ACQUIRE_RESPONSE ||
			spu_dsmcbe_pendingRequests[requestNo].responseCode == PACKAGE_CSP_CHANNEL_READ_RESPONSE ||
			spu_dsmcbe_pendingRequests[requestNo].responseCode == PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE)

			*size = spu_dsmcbe_pendingRequests[requestNo].size;
		else
			size = 0;
	}
	
	spu_dsmcbe_pendingRequests[requestNo].requestCode = 0;
	spu_dsmcbe_pendingRequests[requestNo].channelId = 0;
	return spu_dsmcbe_pendingRequests[requestNo].pointer;
}

void spu_dsmcbe_enqueueStreamAcquire(GUID id, int type)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("Cannot request object table");
		return;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR2("ID was larger than object table size: %d", id);
		return;
	}
	
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	spu_dsmcbe_sendMboxMessage(PACKAGE_ENQUEUE_STREAM_JOB, id, type, 0, 0, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "Enqueue ACQUIRE stream package sent, id: %d\n", WHEREARG, id);
#endif
}

void* spu_dsmcbe_dequeueStreamAcquire(GUID* id, unsigned long* size)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("Cannot request object table");
		return (void*)UINT_MAX;
	}
	
	if ((int)id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR2("ID was larger than object table size: %d", (unsigned int)id);
		return (void*)UINT_MAX;
	}
	
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return (void*)UINT_MAX;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_DEQUEUE_STREAM_JOB);
	if (nextId == UINT_MAX)
		return (void*)UINT_MAX;

	spu_dsmcbe_sendMboxMessage(spu_dsmcbe_pendingRequests[nextId].requestCode, nextId, (unsigned int)&id, 0, 0, 0, 0, 0);

#ifdef DEBUG_COMMUNICATION	
	printf(WHERESTR "DEQUEUE stream package sent, id: %d\n", WHEREARG, nextId);
#endif

	return spu_dsmcbe_endAsync(nextId, size);
}



void dsmcbe_enqueueStreamAcquire(GUID id, int type)
{
	spu_dsmcbe_enqueueStreamAcquire(id, type);
}

void* dsmcbe_dequeueStreamAcquire(GUID* id, unsigned long* size)
{
	return spu_dsmcbe_dequeueStreamAcquire(id, size);
}

void dsmcbe_acquireBarrier(GUID id) {
	spu_dsmcbe_endAsync(spu_dsmcbe_acquire_barrier_begin(id), NULL);		
}
 
void* dsmcbe_acquire(GUID id, unsigned long* size, int type) {
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	return spu_dsmcbe_endAsync(spu_dsmcbe_acquire_begin(id, type), size);
}

void dsmcbe_release(void* data) {
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	spu_dsmcbe_release_begin(data);
}

void* dsmcbe_create(GUID id, unsigned long size) {
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	return spu_dsmcbe_endAsync(spu_dsmcbe_create_begin(id, size), NULL);
}

void* spu_dsmcbe_memory_malloc(unsigned long size) {
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	return spu_dsmcbe_endAsync(spu_dsmcbe_memory_malloc_begin(size), NULL);;
}
void spu_dsmcbe_memory_free(void* data) {
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	spu_dsmcbe_memory_free_begin(data);
}

void spu_dsmcbe_memory_setup(unsigned int reservedMemory)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	void* start;
	unsigned int size;
	
	register volatile unsigned int *r1 asm("1");
	
	//printf(WHERESTR "SBRK(0): %d, &_end: %d, r1:%d\n", WHEREARG, (unsigned int)sbrk(0), (unsigned int)&_end, *r1);

	size = *r1 - ((unsigned int)sbrk(0));
	size -= MAIN_THREAD_STACKSPACE + reservedMemory; //Reserve space for stack and user needs
	start = _malloc_align(size, 7);

	//printf(WHERESTR "SPU Reports having %d bytes free memory, at %d\n", WHEREARG, size, (unsigned int)start);
	if (start == NULL)
		REPORT_ERROR("Failed to allocated the desired amount of bytes, consider increasing MAIN_THREAD_STACKSPACE or reservedMemory");
	
	spu_dsmcbe_sendMboxMessage(PACKAGE_SPU_MEMORY_SETUP, (unsigned int)start, 0, size, 0, 0, 0, 0);
}

//Initializes the DSMCBE system
void dsmcbe_initializeReserved(unsigned int reservedMemory)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	//Clear the mailbox buffer
	memset(spu_dsmcbe_mbox_buffer, MAILBOX_BUFFER_SIZE * sizeof(struct spu_dsmcbe_mailbox_args), 0);

	//printf(WHERESTR "Setting up malloc\n", WHEREARG);
	spu_dsmcbe_memory_setup(reservedMemory);
	memset(spu_dsmcbe_pendingRequests, 0, MAX_PENDING_REQUESTS * sizeof(struct spu_dsmcbe_pendingRequestStruct));
	memset(spu_dsmcbe_directChannels, 0, MAX_DIRECT_CHANNELS * sizeof(struct spu_dsmcbe_directChannelStruct));
	spu_dsmcbe_initialized = TRUE;
}

//Initializes the DSMCBE system
void dsmcbe_initialize()
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	dsmcbe_initializeReserved(0);
}

//Creates a new barrier
void dsmcbe_createBarrier(GUID id, unsigned int count)
{
	SET_CURRENT_FUNCTION(FILE_DSMCBE_SPU);

	unsigned int* tmp = dsmcbe_create(id, sizeof(unsigned int) * 2);
	if (tmp == NULL)
	{
		REPORT_ERROR("Failed to create barrier");
		return;
	}
	
	tmp[0] = count;
	tmp[1] = 0;
	dsmcbe_release(tmp);
}

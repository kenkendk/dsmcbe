#include <libmisc.h>
#include <spu_mfcio.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

//Switch to interrupt mailbox in order to enable the PPU eventhandler
#define SPU_WRITE_OUT_MBOX spu_write_out_intr_mbox
//#define SPU_WRITE_OUT_MBOX spu_write_out_mbox 

#include "../../dsmcbe_spu.h"
#include "../../common/datapackages.h"
#include "../../common/debug.h"
#include "../../PPU/header files/SPU_MemoryAllocator_Shared.h"

#define TRUE 1
#define FALSE 0

struct spu_dsmcbe_pendingRequestStruct
{
	//The code the package was created with.
	//Zero means avalible
	unsigned int requestCode;
	
	//The pointer result
	void* pointer;
	//The resulting size
	unsigned int size;
};

//The total number of pending requests possible
#define MAX_PENDING_REQUESTS 16

//The statically allocated buffer for the pending requests
struct spu_dsmcbe_pendingRequestStruct spu_dsmcbe_pendingRequests[MAX_PENDING_REQUESTS];

//The next avalible request number to use. 
//Having this number avoids having to search through the spu_dsmcbe_pendingRequests 
unsigned int spu_dsmcbe_nextRequestNo = 0;

//A flag indicating if initialize has been called.
unsigned int spu_dsmcbe_initialized = FALSE; 

//This function gets the next avalible request number, and sets the response flag to "not ready"
unsigned int spu_dsmcbe_getNextReqNo(unsigned int requestCode)
{
	size_t i;
	for(i = 0; i < MAX_PENDING_REQUESTS; i++)
		if (spu_dsmcbe_pendingRequests[(i + spu_dsmcbe_nextRequestNo) % MAX_PENDING_REQUESTS].requestCode == 0)
		{
			spu_dsmcbe_nextRequestNo = (i + spu_dsmcbe_nextRequestNo + 1) % MAX_PENDING_REQUESTS;
			spu_dsmcbe_pendingRequests[(i + spu_dsmcbe_nextRequestNo) % MAX_PENDING_REQUESTS].requestCode = requestCode;
			return (i + spu_dsmcbe_nextRequestNo) % MAX_PENDING_REQUESTS;
		}
		
	REPORT_ERROR("No avalible request slots found, consider raising the MAX_PENDING_REQUESTS");
	return UINT_MAX;
}

//Reads mailbox messages, blocking
void spu_dsmcbe_readMailbox() {
	
	printf(WHERESTR "Actually reading mailbox\n", WHEREARG);
	unsigned int requestID = spu_read_in_mbox();

	printf(WHERESTR "Reading mailbox, requestId: %d\n", WHEREARG, requestID);

	if (requestID > MAX_PENDING_REQUESTS)
	{
		REPORT_ERROR("Invalid request id detected");
		return;
	}
	
	switch(spu_dsmcbe_pendingRequests[requestID].requestCode)
	{
		case PACKAGE_TERMINATE_REQUEST:
			printf(WHERESTR "TERMINATE package recieved\n", WHEREARG);
			spu_dsmcbe_initialized = FALSE;
			break;
			
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			printf(WHERESTR "ACQUIRE package recieved\n", WHEREARG);
			
			spu_dsmcbe_pendingRequests[requestID].requestCode = PACKAGE_ACQUIRE_RESPONSE;
			spu_dsmcbe_pendingRequests[requestID].pointer = (void*)spu_read_in_mbox();
			spu_dsmcbe_pendingRequests[requestID].size = spu_read_in_mbox();
			break;			
	
		case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
			printf(WHERESTR "MALLOC package recieved\n", WHEREARG);
			spu_dsmcbe_pendingRequests[requestID].requestCode = PACKAGE_SPU_MEMORY_MALLOC_RESPONSE;
			spu_dsmcbe_pendingRequests[requestID].pointer = (void*)spu_read_in_mbox();
			break;			
		
		default:
			fprintf(stderr, WHERESTR "Unknown package recieved: %i, message: %s", WHEREARG, spu_dsmcbe_pendingRequests[requestID].requestCode, strerror(errno));
	};	
}

//Initiates a create operation
unsigned int spu_dsmcbe_create_begin(GUID id, unsigned long size)
{
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("Cannot request page table");
		return UINT_MAX;
	}
	
	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("ID was larger than page table size");
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

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(id);
	SPU_WRITE_OUT_MBOX(size);

	printf(WHERESTR "CREATE package sent\n", WHEREARG);
	
	return nextId;
}

//Initiates an acquire operation
unsigned int spu_dsmcbe_acquire_begin(GUID id, int type)
{
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("Cannot request page table");
		return UINT_MAX;
	}
	
	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("ID was larger than page table size");
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

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(id);

	printf(WHERESTR "ACQUIRE package sent, id: %d\n", WHEREARG, nextId);

	return nextId;
}

//Initiates a release operation
void spu_dsmcbe_release_begin(void* data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	SPU_WRITE_OUT_MBOX(PACKAGE_RELEASE_REQUEST);
	SPU_WRITE_OUT_MBOX((unsigned int)data);	

	printf(WHERESTR "RELEASE package sent @: %d\n", WHEREARG, (unsigned int)data);
}

//Initiates a malloc operation
unsigned int spu_dsmcbe_memory_malloc_begin(unsigned int size)
{
    printf(WHERESTR "SPU is calling malloc for %d\n", WHEREARG, size);

	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return UINT_MAX;
	}
	
	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_MEMORY_MALLOC_REQUEST);
	if (nextId == UINT_MAX)
		return UINT_MAX;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(size);
	
	printf(WHERESTR "MALLOC package sent\n", WHEREARG);

	return nextId;
}

//Initiates a free operation
void spu_dsmcbe_memory_free_begin(void* data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	SPU_WRITE_OUT_MBOX(PACKAGE_SPU_MEMORY_FREE);
	SPU_WRITE_OUT_MBOX((unsigned int)data);	

	printf(WHERESTR "FREE package sent\n", WHEREARG);
}



//Cleanly terminates the DSMCBE system
void terminate() 
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return;
	}

	spu_dsmcbe_getNextReqNo(PACKAGE_TERMINATE_REQUEST);
	SPU_WRITE_OUT_MBOX(PACKAGE_TERMINATE_REQUEST);

	printf(WHERESTR "TERMINATE package sent\n", WHEREARG);

	while(spu_dsmcbe_initialized)
		spu_dsmcbe_readMailbox();

	printf(WHERESTR "TERMINATE response read\n", WHEREARG);

}


//Returns a SPU_DSMCBE_ASYNC_* value indicating the state of the operation
unsigned int spu_dsmcbe_getAsyncStatus(unsigned int requestNo)
{
	//Process any pending messages
	while (spu_stat_in_mbox() != 0)
		spu_dsmcbe_readMailbox();

	if (requestNo > MAX_PENDING_REQUESTS || spu_dsmcbe_pendingRequests[requestNo].requestCode == 0)
		return SPU_DSMCBE_ASYNC_ERROR;
		
	switch(spu_dsmcbe_pendingRequests[requestNo].requestCode)
	{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_SPU_MEMORY_MALLOC_REQUEST:
			return SPU_DSMCBE_ASYNC_BUSY;
		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_SPU_MEMORY_MALLOC_RESPONSE:
			return SPU_DSMCBE_ASYNC_READY;
		default:
			return SPU_DSMCBE_ASYNC_ERROR;
	}
}

//Ends an async operation. Blocking if the operation is not complete on entry
void* spu_dsmcbe_endAsync(unsigned int requestNo, unsigned long* size)
{
	unsigned int status;
	
	printf(WHERESTR "Reading mailbox\n", WHEREARG);
	
	//Process any pending messages
	while (spu_stat_in_mbox() != 0)
		spu_dsmcbe_readMailbox();

	printf(WHERESTR "Done reading mailbox\n", WHEREARG);

	if (requestNo > MAX_PENDING_REQUESTS || spu_dsmcbe_pendingRequests[requestNo].requestCode == 0)
	{
		REPORT_ERROR("endAsync called on non-existing operation");
		return NULL;
	}
	
	while ((status = spu_dsmcbe_getAsyncStatus(requestNo)) == SPU_DSMCBE_ASYNC_BUSY)
	{
		if (IsThreaded())
			YieldThread();
		else
			spu_dsmcbe_readMailbox();
	}
	
	if (status == SPU_DSMCBE_ASYNC_ERROR)
	{
		REPORT_ERROR("Request ID was accepted, but async status gave error");
		return NULL;
	}

	printf(WHERESTR "Got result %d\n", WHEREARG, (unsigned int)spu_dsmcbe_pendingRequests[requestNo].pointer);

	if (size != NULL)
	{
		if (spu_dsmcbe_pendingRequests[requestNo].requestCode == PACKAGE_ACQUIRE_RESPONSE)
			*size = spu_dsmcbe_pendingRequests[requestNo].size;
		else
			size = 0;
	}
	
	spu_dsmcbe_pendingRequests[requestNo].requestCode = 0;
	return spu_dsmcbe_pendingRequests[requestNo].pointer;
}
 
void* acquire(GUID id, unsigned long* size, int type) {
	return spu_dsmcbe_endAsync(spu_dsmcbe_acquire_begin(id, type), size);		
}

void release(void* data) {
	spu_dsmcbe_release_begin(data);
}

void* create(GUID id, unsigned long size) {
	return spu_dsmcbe_endAsync(spu_dsmcbe_create_begin(id, size), NULL);
}

void* spu_dsmcbe_memory_malloc(unsigned long size) {
	printf(WHERESTR "Called malloc for %d\n", WHEREARG, (unsigned int)size);	
	return spu_dsmcbe_endAsync(spu_dsmcbe_memory_malloc_begin(size), NULL);;
}
void spu_dsmcbe_memory_free(void* data) {
	spu_dsmcbe_memory_free_begin(data);
}

void spu_dsmcbe_memory_setup(unsigned int reservedMemory)
{
	void* start;
	unsigned int size;
	
	register volatile unsigned int *r1 asm("1");
	
	//printf(WHERESTR "SBRK(0): %d, &_end: %d, r1:%d\n", WHEREARG, (unsigned int)sbrk(0), (unsigned int)&_end, *r1);

	size = *r1 - ((unsigned int)sbrk(0));
	size -= (8 * 1024) + reservedMemory; //Reserve space for stack and user needs
	start = thread_malloc_align(size, 7);

	//printf(WHERESTR "SPU Reports having %d bytes free memory, at %d\n", WHEREARG, size, (unsigned int)start);
	if (start == NULL)
		REPORT_ERROR("Failed to allocated the desired amount of bytes");
	
	spu_write_out_intr_mbox(PACKAGE_SPU_MEMORY_SETUP);
	spu_write_out_intr_mbox((unsigned int)start);
	spu_write_out_intr_mbox(size);
}

//Initializes the DSMCBE system
void initializeReserved(unsigned int reservedMemory)
{
	//printf(WHERESTR "Setting up malloc\n", WHEREARG);
	spu_dsmcbe_memory_setup(reservedMemory);
	memset(spu_dsmcbe_pendingRequests, 0, MAX_PENDING_REQUESTS * sizeof(unsigned int));
	spu_dsmcbe_initialized = TRUE;
}

//Initializes the DSMCBE system
void initialize()
{
	initializeReserved(0);
}

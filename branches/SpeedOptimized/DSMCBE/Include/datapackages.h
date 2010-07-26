/*
 * 
 * This file contains definitions for all the available
 * transferable data packages in DSMCBE
 * 
 */

#ifndef DATAPACKAGES_H_
#define DATAPACKAGES_H_

#include "dsmcbe.h"
#include <pthread.h>

#define ACQUIRE_MODE_CREATE (ACQUIRE_MODE_WRITE + 2)
#define ACQUIRE_MODE_BLOCKED (ACQUIRE_MODE_CREATE + 1)

#define NEXT_SEQ_NO(current, max) ((current) = ((current)+1) % (max)) 

#define PACKAGE_INVALID 0
#define PACKAGE_CREATE_REQUEST 1
#define PACKAGE_ACQUIRE_REQUEST_READ 2
#define PACKAGE_ACQUIRE_REQUEST_WRITE 3
#define PACKAGE_ACQUIRE_RESPONSE 4
#define PACKAGE_WRITEBUFFER_READY 5
#define PACKAGE_MIGRATION_RESPONSE 6
#define PACKAGE_RELEASE_REQUEST 7
#define PACKAGE_NACK 9
#define PACKAGE_INVALIDATE_REQUEST 10
#define PACKAGE_INVALIDATE_RESPONSE 11

#define PACKAGE_UPDATE 12

#define PACKAGE_TERMINATE_REQUEST 14
#define PACKAGE_TERMINATE_RESPONSE 15

#define PACKAGE_ACQUIRE_BARRIER_REQUEST 16
#define PACKAGE_ACQUIRE_BARRIER_RESPONSE 17

#define PACKAGE_MIGRATION_REQUEST 18

#define PACKAGE_DEBUG_PRINT_STATUS 30

#define PACKAGE_ENQUEUE_STREAM_JOB 40
#define PACKAGE_DEQUEUE_STREAM_JOB 41

#define PACKAGE_CSP_CHANNEL_CREATE_REQUEST 50
#define PACKAGE_CSP_CHANNEL_CREATE_RESPONSE 51
#define PACKAGE_CSP_CHANNEL_POISON_REQUEST 52
#define PACKAGE_CSP_CHANNEL_POISON_RESPONSE 53
#define PACKAGE_CSP_CHANNEL_READ_REQUEST 54
#define PACKAGE_CSP_CHANNEL_READ_RESPONSE 55
#define PACKAGE_CSP_CHANNEL_WRITE_REQUEST 56
#define PACKAGE_CSP_CHANNEL_WRITE_RESPONSE 57
#define PACKAGE_CSP_CHANNEL_POISONED_RESPONSE 58
#define PACKAGE_CSP_CHANNEL_SKIP_RESPONSE 59

#define PACKAGE_FREE_REQUEST 60
#define PACKAGE_TRANSFER_REQUEST 61
#define PACKAGE_TRANSFER_RESPONSE 62

#define PACKAGE_DMA_COMPLETE 70

#define STREAM_STATUS_QUEUED 0
#define STREAM_STATUS_REQUEST_SENT 1
#define STREAM_STATUS_RESPONSE_RECIEVED 2
#define STREAM_STATUS_DMA_INITIATED 3
#define STREAM_STATUS_DMA_COMPLETED 4

/*
 * All structs have the same initial three fields:
 *   packageCode
 *   requestID
 *   dataItem
 *
 * This simplifies handling a large number of different packages,
 * similar to what an interface in OO code
 *
 * All structs except the updateRequest also have the same following three fields
 *   originator
 *   originalRecipient
 *   originalRequestID
 *
 * These fields are used to trace a package, allowing for simple migration.
 * Any following fields are package specific
 *
 */

struct dsmcbe_QueueableItemStruct;

struct dsmcbe_createRequest
{
	unsigned int packageCode; // = 1
	unsigned int requestID;
	GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	unsigned long dataSize;
};

struct dsmcbe_acquireRequest
{
    unsigned int packageCode; // = 2
    unsigned int requestID;
    GUID dataItem;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	unsigned int mode;
};

struct dsmcbe_acquireResponse
{
    unsigned int packageCode; // = 4
    unsigned int requestID;
    GUID dataItem; 

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

    int mode;
    unsigned int writeBufferReady;

    unsigned long dataSize;
    void* data;
};

struct dsmcbe_writebufferReady
{
    unsigned int packageCode; // = 5
    unsigned int requestID;
    GUID dataItem; 

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_migrationResponse
{
    unsigned int packageCode; // = 6
    unsigned int requestID;
	GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	int mode;
    unsigned long dataSize;
    void* data;
};

struct dsmcbe_releaseRequest
{
    unsigned int packageCode; // = 7
    unsigned int requestID;
    GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	int mode;
    unsigned long dataSize;
    unsigned long offset;
    void* data;
};

struct dsmcbe_NACK
{
    unsigned int packageCode; // = 9
    unsigned int requestID;
    GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_invalidateRequest
{
    unsigned int packageCode; // = 10
    unsigned int requestID;
    GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_invalidateResponse
{
    unsigned int packageCode; // = 11
    unsigned int requestID;
    GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_updateRequest
{
	unsigned int packageCode; // = 12
	unsigned int requestID;
	GUID dataItem;

	unsigned int offset;
    unsigned long dataSize;
    void* data;		
};

struct dsmcbe_acquireBarrierRequest
{
	unsigned int packageCode; // = 16
	unsigned int requestID;
	GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_acquireBarrierResponse
{
	unsigned int packageCode; // = 17
	unsigned int requestID;
	GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_migrationRequest
{
	unsigned int packageCode; // = 18
	unsigned int requestID;
	GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	unsigned int targetMachine;
};

struct dsmcbe_cspChannelCreateRequest
{
    unsigned int packageCode; // = 50
	unsigned int requestID;
	GUID channelId;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	unsigned int type;
	unsigned int bufferSize;
};

struct dsmcbe_cspChannelCreateResponse
{
    unsigned int packageCode; // = 51
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_cspChannelPoisonRequest
{
    unsigned int packageCode; // = 52
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_cspChannelPoisonResponse
{
    unsigned int packageCode; // = 53
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};


struct dsmcbe_cspChannelReadRequest
{
    unsigned int packageCode; // = 54
    unsigned int requestID;
    GUID channelId;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	//These are set if the request is an ALT request
	unsigned int mode;
	unsigned int channelcount;
	GUID* channels;
};

struct dsmcbe_cspChannelReadResponse
{
    unsigned int packageCode; // = 55
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	struct dsmcbe_QueueableItemStruct* transferManager;

	unsigned int speId;
	unsigned long size;
	void* data;
};

struct dsmcbe_cspChannelWriteRequest
{
    unsigned int packageCode; // = 56
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;

	struct dsmcbe_QueueableItemStruct* transferManager;

	//These are set if the request is an ALT request
	unsigned int mode;
	unsigned int channelcount;
	GUID* channels;

	unsigned int speId;
	unsigned long size;
	void* data;
};

struct dsmcbe_cspChannelWriteResponse
{
    unsigned int packageCode; // = 57
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_cspChannelPoisonedResponse
{
    unsigned int packageCode; // = 58
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_cspChannelSkipResponse
{
    unsigned int packageCode; // = 59
    unsigned int requestID;
    GUID channelId;

    unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct dsmcbe_freeRequest
{
    unsigned int packageCode; // = 60
    unsigned int requestID;
    GUID dataItem;

    void* data;
};

struct dsmcbe_transferRequest
{
    unsigned int packageCode; // = 61
    unsigned int requestID;
    GUID dataItem;

    void* mutex;
    void* cond;
    void* queue;
    void* from;
    void* to;
};

struct dsmcbe_transferResponse
{
    unsigned int packageCode; // = 62
    unsigned int requestID;
    GUID dataItem;

    void* from;
    void* to;
};

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

/*
/ * Does not work because gcc cannot figure out that the value is actually constant after macro unroll :(
# define _DSMCBE_MACRO_RECURSIVE_SIZE(x) (MAX(PACKAGE_SIZE(x), ((x) == 0 ? 0 : _DSMCBE_MACRO_RECURSIVE_SIZE((x) - 1))))
//# define MAX_PACKAGE_SIZE _DSMCBE_MACRO_RECURSIVE_SIZE(50)
*/

//This is divided into 3 because GCC uses +2gb mem if it is written as one line...
#define MAX_PACKAGE_SIZE_A \
	MAX(sizeof(struct dsmcbe_createRequest), \
	MAX(sizeof(struct dsmcbe_acquireRequest), \
	MAX(sizeof(struct dsmcbe_acquireResponse), \
	MAX(sizeof(struct dsmcbe_writebufferReady), \
	MAX(sizeof(struct dsmcbe_releaseRequest), \
	MAX(sizeof(struct dsmcbe_NACK), \
	MAX(sizeof(struct dsmcbe_invalidateRequest), \
	MAX(sizeof(struct dsmcbe_invalidateResponse), \
	MAX(sizeof(struct dsmcbe_updateRequest), \
	MAX(sizeof(struct dsmcbe_acquireBarrierRequest), \
	MAX(sizeof(struct dsmcbe_acquireBarrierResponse), \
		0)))))))))))
#define MAX_PACKAGE_SIZE_B \
	MAX(sizeof(struct dsmcbe_migrationRequest), \
	MAX(sizeof(struct dsmcbe_migrationResponse), \
	MAX(sizeof(struct dsmcbe_cspChannelCreateRequest), \
	MAX(sizeof(struct dsmcbe_cspChannelCreateResponse), \
	MAX(sizeof(struct dsmcbe_cspChannelPoisonRequest), \
	MAX(sizeof(struct dsmcbe_cspChannelPoisonResponse), \
	MAX(sizeof(struct dsmcbe_cspChannelReadRequest), \
	MAX(sizeof(struct dsmcbe_cspChannelReadResponse), \
	MAX(sizeof(struct dsmcbe_cspChannelWriteResponse), \
	MAX(sizeof(struct dsmcbe_cspChannelWriteRequest), \
	MAX(sizeof(struct dsmcbe_cspChannelPoisonedResponse), \
	MAX(sizeof(struct dsmcbe_cspChannelSkipResponse), \
		0))))))))))))
#define MAX_PACKAGE_SIZE_C \
		MAX(sizeof(struct dsmcbe_transferRequest), \
		MAX(sizeof(struct dsmcbe_transferResponse), \
		MAX(sizeof(struct dsmcbe_freeRequest), \
			0)))

#define MAX_PACKAGE_SIZE MAX(MAX(MAX_PACKAGE_SIZE_A, MAX_PACKAGE_SIZE_B), MAX_PACKAGE_SIZE_C)

#define MAX_PACKAGE_ID 100

#define PACKAGE_SIZE(x) (x == 0 ? 0 : \
		( x == PACKAGE_CREATE_REQUEST ? sizeof(struct dsmcbe_createRequest) : \
		( x == PACKAGE_ACQUIRE_REQUEST_READ ? sizeof(struct dsmcbe_acquireRequest) : \
		( x == PACKAGE_ACQUIRE_REQUEST_WRITE ? sizeof(struct dsmcbe_acquireRequest) : \
		( x == PACKAGE_ACQUIRE_RESPONSE ? sizeof(struct dsmcbe_acquireResponse) : \
		( x == PACKAGE_WRITEBUFFER_READY ? sizeof(struct dsmcbe_writebufferReady) : \
		( x == PACKAGE_RELEASE_REQUEST ? sizeof(struct dsmcbe_releaseRequest) : \
		( x == PACKAGE_NACK ? sizeof(struct dsmcbe_NACK) : \
		( x == PACKAGE_INVALIDATE_REQUEST ? sizeof(struct dsmcbe_invalidateRequest) : \
		( x == PACKAGE_INVALIDATE_RESPONSE ? sizeof(struct dsmcbe_invalidateResponse) : \
		( x == PACKAGE_UPDATE ? sizeof(struct dsmcbe_updateRequest) : \
		( x == PACKAGE_ACQUIRE_BARRIER_REQUEST ? sizeof(struct dsmcbe_acquireBarrierRequest) : \
		( x == PACKAGE_ACQUIRE_BARRIER_RESPONSE ? sizeof(struct dsmcbe_acquireBarrierResponse) : \
		( x == PACKAGE_MIGRATION_REQUEST ? sizeof(struct dsmcbe_migrationRequest) : \
		( x == PACKAGE_MIGRATION_RESPONSE ? sizeof(struct dsmcbe_migrationResponse) : \
		( x == PACKAGE_CSP_CHANNEL_CREATE_REQUEST ? sizeof(struct dsmcbe_cspChannelCreateRequest) : \
		( x == PACKAGE_CSP_CHANNEL_CREATE_RESPONSE ? sizeof(struct dsmcbe_cspChannelCreateResponse) : \
		( x == PACKAGE_CSP_CHANNEL_POISON_REQUEST ? sizeof(struct dsmcbe_cspChannelPoisonRequest) : \
		( x == PACKAGE_CSP_CHANNEL_POISON_RESPONSE ? sizeof(struct dsmcbe_cspChannelPoisonResponse) : \
		( x == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE ? sizeof(struct dsmcbe_cspChannelPoisonedResponse) : \
		( x == PACKAGE_CSP_CHANNEL_READ_REQUEST ? sizeof(struct dsmcbe_cspChannelReadRequest) : \
		( x == PACKAGE_CSP_CHANNEL_READ_RESPONSE ? sizeof(struct dsmcbe_cspChannelReadResponse) : \
		( x == PACKAGE_CSP_CHANNEL_WRITE_REQUEST ? sizeof(struct dsmcbe_cspChannelWriteRequest) : \
		( x == PACKAGE_CSP_CHANNEL_WRITE_RESPONSE ? sizeof(struct dsmcbe_cspChannelWriteResponse) : \
		( x == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE ? sizeof(struct dsmcbe_cspChannelSkipResponse) : \
		( x == PACKAGE_TRANSFER_REQUEST ? sizeof(struct dsmcbe_transferRequest) : \
		( x == PACKAGE_TRANSFER_RESPONSE ? sizeof(struct dsmcbe_transferResponse) : \
		( x == PACKAGE_FREE_REQUEST ? sizeof(struct dsmcbe_freeRequest) : \
				0))))) ))))) ))))) ))))) ))))) )))

struct packageBuffer
{
	unsigned int packageCode;
	unsigned char buffer[MAX_PACKAGE_SIZE - sizeof(unsigned int)];
};

#define ALIGNED_SIZE(x) ((x) + ((16 - ((x) % 16)) % 16))

#ifdef DSMCBE_SPU

  //#define SPU_TRACE_MEM
  /*extern void* clear(unsigned long size);
  extern void* clearAlign(unsigned long size, int base);*/
  
  #ifdef SPU_TRACE_MEM
    extern unsigned int m_balance;
    extern void* __m_malloc(unsigned int x, char* s1, int s2);
    extern void __m_free(void* x, char* s1, int s2);
    extern void* __m_malloc_align(unsigned int x, int y, char* s1, int s2);
    extern void __m_free_align(void* x, char* s1, int s2);

    #define MALLOC(x) __m_malloc(x, __FILE__, __LINE__)
    #define FREE(x) __m_free(x, __FILE__, __LINE__)
    #define MALLOC_ALIGN(x,y) __m_malloc_align(x,y, __FILE__, __LINE__)
    #define FREE_ALIGN(x) __m_free_align(x, __FILE__, __LINE__)
  #else
  	
    #define MALLOC(x) spu_dsmcbe_memory_malloc(x)
    #define MALLOC_ALIGN(x,y) spu_dsmcbe_memory_malloc(x)
    
    //#define MALLOC(x) thread_malloc(x)
    //#define MALLOC_ALIGN(x,y) thread_malloc_align(x,y)
    
    #define FREE(x) spu_dsmcbe_memory_free(x)
    #define FREE_ALIGN(x) spu_dsmcbe_memory_free(x)
  #endif /*SPU_TRACE_MEM*/

#else

#ifndef MALLOC

  #define MALLOC(size) malloc(size)
  #define FREE(x) free(x)
  #define MALLOC_ALIGN(size, power) _malloc_align(size, power)
  #define FREE_ALIGN(x) _free_align(x)

#endif

#endif /* DSMCBE_SPU */

#endif /*DATAPACKAGES_H_*/

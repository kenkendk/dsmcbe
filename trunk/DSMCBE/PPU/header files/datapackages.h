/*
 * 
 * This file contains definitions for all the avalible
 * transferable data packages in DSMCBE
 * 
 */

#ifndef DATAPACKAGES_H_
#define DATAPACKAGES_H_

#include "../../common/dsmcbe.h"

#define ACQUIRE_MODE_CREATE (ACQUIRE_MODE_DELETE + 2)
#define ACQUIRE_MODE_BLOCKED (ACQUIRE_MODE_CREATE + 1)
#define ACQUIRE_MODE_GET (ACQUIRE_MODE_BLOCKED + 1)

#define NEXT_SEQ_NO(current, max) ((current) = ((current)+1) % (max)) 

#define PACKAGE_INVALID 0
#define PACKAGE_CREATE_REQUEST 1
#define PACKAGE_ACQUIRE_REQUEST 2
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

#define PACKAGE_PUT_REQUEST 50
#define PACKAGE_PUT_RESPONSE 51
#define PACKAGE_GET_REQUEST 52
#define PACKAGE_GET_RESPONSE 53

#define PACKAGE_MALLOC_REQUEST 60
#define PACKAGE_TRANSFER_REQUEST 61

#define STREAM_STATUS_QUEUED 0
#define STREAM_STATUS_REQUEST_SENT 1
#define STREAM_STATUS_RESPONSE_RECIEVED 2
#define STREAM_STATUS_DMA_INITIATED 3
#define STREAM_STATUS_DMA_COMPLETED 4

struct createRequest
{
	unsigned int packageCode; // = 1
	unsigned int requestID;
	GUID dataItem;
	unsigned long dataSize;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
	unsigned int mode;
};

struct acquireRequest
{
    unsigned int packageCode; // = 2
    unsigned int requestID;
    unsigned int mode;
    GUID dataItem;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct acquireResponse
{
    unsigned int packageCode; // = 4
    unsigned int requestID;
    GUID dataItem; 
    int mode;
    unsigned int writeBufferReady;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
    unsigned long dataSize;
    void* data;
};

struct writebufferReady
{
    unsigned int packageCode; // = 5
    unsigned int requestID;
    GUID dataItem; 
};

struct migrationResponse
{
    unsigned int packageCode; // = 6
    unsigned int requestID;
	GUID dataItem;
    int mode;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
    unsigned long dataSize;
    void* data;
};

struct releaseRequest
{
    unsigned int packageCode; // = 7
    unsigned int requestID;
    GUID dataItem;
    int mode;
    unsigned long dataSize;
    unsigned long offset;
    void* data;
};

struct NACK
{
    unsigned int packageCode; // = 9
    unsigned int requestID;
    unsigned int hint;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct invalidateRequest
{
    unsigned int packageCode; // = 10
    unsigned int requestID;
    GUID dataItem;
};

struct invalidateResponse
{
    unsigned int packageCode; // = 11
    unsigned int requestID;
};

struct updateRequest
{
	unsigned int packageCode;
	unsigned int requestID;
	GUID dataItem;
	unsigned int offset;
    unsigned long dataSize;
    void* data;		
};

struct acquireBarrierRequest
{
	unsigned int packageCode;
	unsigned int requestID;
	GUID dataItem;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct acquireBarrierResponse
{
	unsigned int packageCode;
	unsigned int requestID;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct migrationRequest
{
	unsigned int packageCode;
	unsigned int requestID;
	GUID dataItem;
	unsigned int targetMachine;

	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct putRequest
{
    unsigned int packageCode; // = 50
	unsigned int requestID;
	GUID dataItem;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
	unsigned long dataSize;
	void* data;
	unsigned int isLS;
};

struct putResponse
{
    unsigned int packageCode; // = 51
    unsigned int requestID;
};

struct getRequest
{
    unsigned int packageCode; // = 52
    unsigned int requestID;
    GUID dataItem;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
};

struct getResponse
{
    unsigned int packageCode; // = 53
    unsigned int requestID;
    GUID dataItem;
	unsigned long size;
	void* target;
	void* source;
	unsigned int isTransfered;
};

struct mallocRequest
{
    unsigned int packageCode; // = 60
    unsigned int requestID;
    void* callback;
};

struct transferRequest
{
    unsigned int packageCode; // = 61
    unsigned int requestID;
    GUID dataItem;
	unsigned int originator;
	unsigned int originalRecipient;
	unsigned int originalRequestID;
	unsigned long size;
	void* target;
	void* source;
	void* callback;
};

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define MAX_PACKAGE_SIZE MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(sizeof(struct createRequest), sizeof(struct acquireRequest)), sizeof(struct acquireResponse)), sizeof(struct writebufferReady)), sizeof(struct migrationResponse)), sizeof(struct releaseRequest)), sizeof(struct NACK)), sizeof(struct invalidateRequest)), sizeof(struct invalidateResponse)), sizeof(struct acquireBarrierRequest)), sizeof(struct acquireBarrierResponse))

struct packageBuffer
{
	unsigned int packageCode;
	unsigned char buffer[MAX_PACKAGE_SIZE - sizeof(unsigned int)];
};

#define PACKAGE_SIZE(x) (x == 0 ? 0 : (x == 1 ? sizeof(struct createRequest) : ( x == 2 ? sizeof(struct acquireRequest) : ( x == 3 ? sizeof(struct acquireRequest) : ( x == 4 ? sizeof(struct acquireResponse) : ( x == 5  ? sizeof(struct writebufferReady) : ( x == 6 ? sizeof(struct migrationResponse) : ( x == 7 ? sizeof(struct releaseRequest) : ( x == 9 ? sizeof(struct NACK) : ( x == 10 ? sizeof(struct invalidateRequest) : ( x == 11 ? sizeof(struct invalidateResponse) : ( x == 16 ? sizeof(struct acquireBarrierRequest) : ( x == 17 ? sizeof(struct acquireBarrierResponse) : ( x == 18 ? sizeof(struct migrationRequest) : (x = 12 ? sizeof(struct updateRequest) : 0)))))))))))))))

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
  

/*#define malloc(x) spu_dsmcbe_memory_malloc(x)
#define malloc_align(x, y) spu_dsmcbe_memory_malloc(x)
#define free(x) spu_dsmcbe_memory_free(x)
#define free_align(x) spu_dsmcbe_memory_free(x)*/
  
#else
/*
  #define MALLOC(x) (((x) == 0) ? NULL : fbmMalloc(x, WHEREARG))
  #define FREE(x) fbmFree(x, WHEREARG)
  #define MALLOC_ALIGN(x,y) (((x) == 0) ? NULL : fbmMallocAlign((x) + 1024, y, WHEREARG))
  #define FREE_ALIGN(x) fbmFreeAlign(x, WHEREARG)
*/
/*
  #define MALLOC(x) (((x) == 0) ? NULL : malloc((x)+1024))
  #define FREE(x) free(x)
  #define MALLOC_ALIGN(x,y) (((x) == 0) ? NULL : _malloc_align((x)+1024, y))
  #define FREE_ALIGN(x) _free_align(x)
*/

#ifndef MALLOC

  void* __malloc_w_check(unsigned int size, char* file, int line);
  void* __malloc_align_w_check(unsigned int size, unsigned int power, char* file, int line);

  #define MALLOC(size) __malloc_w_check(size, __FILE__,__LINE__);
  #define FREE(x) free(x)
  #define MALLOC_ALIGN(size, power)  __malloc_align_w_check(size, power, __FILE__,__LINE__);
  #define FREE_ALIGN(x) _free_align(x)

#endif

#endif /* DSMCBE_SPU */

#endif /*DATAPACKAGES_H_*/

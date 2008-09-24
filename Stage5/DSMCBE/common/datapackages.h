/*
 * 
 * This file contains definitions for all the avalible
 * transferable data packages in DSMCBE
 * 
 */

#ifndef DATAPACKAGES_H_
#define DATAPACKAGES_H_

#include "../dsmcbe.h"

#define ACQUIRE_MODE_WRITE_OK (ACQUIRE_MODE_WRITE + 1)
#define ACQUIRE_MODE_CREATE (ACQUIRE_MODE_WRITE_OK + 1)
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
#define PACKAGE_RELEASE_RESPONSE 8
#define PACKAGE_NACK 9
#define PACKAGE_INVALIDATE_REQUEST 10
#define PACKAGE_INVALIDATE_RESPONSE 11

#define PACKAGE_TERMINATE_REQUEST 14
#define PACKAGE_TERMINATE_RESPONSE 15

#define PACKAGE_ACQUIRE_BARRIER_REQUEST 16
#define PACKAGE_ACQUIRE_BARRIER_RESPONSE 17

struct createRequest
{
	unsigned int packageCode; // = 1
	unsigned int requestID;
	GUID dataItem;
	unsigned long dataSize;
};

struct acquireRequest
{
    //2 for read, 3 for write
    unsigned int packageCode; // = 2
    unsigned int requestID;
    GUID dataItem;
    //void* spe;
};

struct acquireResponse
{
    unsigned int packageCode; // = 4
    unsigned int requestID;
    GUID dataItem; 
    int mode;
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
    unsigned long dataSize;
    unsigned long waitListSize;
    void* data;
    void* waitList;
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

struct releaseResponse
{
    unsigned int packageCode; // = 8
    unsigned int requestID;
};

struct NACK
{
    unsigned int packageCode; // = 9
    unsigned int requestID;
    unsigned int hint;
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

struct acquireBarrierRequest
{
	unsigned int packageCode;
	unsigned int requestID;
	GUID dataItem;
};

struct acquireBarrierResponse
{
	unsigned int packageCode;
	unsigned int requestID;
};


#ifndef MAX
#define MAX(a,b) (a > b ? a : b)
#endif

#define MAX_PACKAGE_SIZE MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(sizeof(struct createRequest), sizeof(struct acquireRequest)), sizeof(struct acquireResponse)), sizeof(struct writebufferReady)), sizeof(struct migrationResponse)), sizeof(struct releaseRequest)), sizeof(struct releaseResponse)), sizeof(struct NACK)), sizeof(struct invalidateRequest)), sizeof(struct invalidateResponse)), sizeof(struct acquireBarrierRequest)), sizeof(struct acquireBarrierResponse))

struct packageBuffer
{
	unsigned int packageCode;
	unsigned char buffer[MAX_PACKAGE_SIZE - sizeof(unsigned int)];
};

#define PACKAGE_SIZE(x) (x == 0 ? 0 : (x == 1 ? sizeof(struct createRequest) : ( x == 2 ? sizeof(struct acquireRequest) : ( x == 3 ? sizeof(struct acquireRequest) : ( x == 4 ? sizeof(struct acquireResponse) : ( x == 5  ? sizeof(struct writebufferReady) : ( x == 6 ? sizeof(struct migrationResponse) : ( x == 7 ? sizeof(struct releaseRequest) : ( x == 8 ? sizeof(struct releaseResponse) : ( x == 9 ? sizeof(struct NACK) : ( x == 10 ? sizeof(struct invalidateRequest) : ( x == 11 ? sizeof(struct invalidateResponse) : ( x == 16 ? sizeof(struct acquireBarrierRequest) : ( x == 17 ? sizeof(struct acquireBarrierResponse) : 0))))))))))))))

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
  #define MALLOC(x) (((x) == 0) ? NULL : malloc(x))
  #define FREE(x) free(x)
  #define MALLOC_ALIGN(x,y) (((x) == 0) ? NULL : _malloc_align(x,y))
  #define FREE_ALIGN(x) _free_align(x)
#endif /* DSMCBE_SPU */

#endif /*DATAPACKAGES_H_*/

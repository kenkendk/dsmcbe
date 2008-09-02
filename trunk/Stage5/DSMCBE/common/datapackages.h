/*
 * 
 * This file contains definitions for all the avalible
 * transferable data packages in DSMCBE
 * 
 */

#ifndef DATAPACKAGES_H_
#define DATAPACKAGES_H_

#include "../dsmcbe.h"
#include "datastructures.h"

#define ACQUIRE_MODE_WRITE_OK (ACQUIRE_MODE_WRITE + 1)
#define ACQUIRE_MODE_CREATE (ACQUIRE_MODE_WRITE_OK + 1)
#define ACQUIRE_MODE_BLOCKED (ACQUIRE_MODE_CREATE + 1)

#define NEXT_SEQ_NO(current, max) (current = (current+1) % (max)) 

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

#define PACKAGE_DMA_TRANSFER_COMPLETE 12

#define PACKAGE_TERMINATE_REQUEST 14
#define PACKAGE_TERMINATE_RESPONSE 15

#define SPU_DMA_LS_TO_EA 100
#define SPU_DMA_EA_TO_LS 101
#define SPU_DMA_COMPLETE 102

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
    void* dmaComplete;
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

#ifndef MAX
#define MAX(a,b) (a > b ? a : b)
#endif

#define MAX_PACKAGE_SIZE MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(sizeof(struct createRequest), sizeof(struct acquireRequest)), sizeof(struct acquireResponse)), sizeof(struct writebufferReady)), sizeof(struct migrationResponse)), sizeof(struct releaseRequest)), sizeof(struct releaseResponse)), sizeof(struct NACK)), sizeof(struct invalidateRequest)), sizeof(struct invalidateResponse))

struct packageBuffer
{
	unsigned int packageCode;
	unsigned char buffer[MAX_PACKAGE_SIZE - sizeof(unsigned int)];
};

#define PACKAGE_SIZE(x) (x == 0 ? 0 : (x == 1 ? sizeof(struct createRequest) : ( x == 2 ? sizeof(struct acquireRequest) : ( x == 3 ? sizeof(struct acquireRequest) : ( x == 4 ? sizeof(struct acquireResponse) : ( x == 5  ? sizeof(struct writebufferReady) : ( x == 6 ? sizeof(struct migrationResponse) : ( x == 7 ? sizeof(struct releaseRequest) : ( x == 8 ? sizeof(struct releaseResponse) : ( x == 9 ? sizeof(struct NACK) : ( x == 10 ? sizeof(struct invalidateRequest) : ( x == 11 ? sizeof(struct invalidateResponse) : 0))))))))))))

#endif /*DATAPACKAGES_H_*/

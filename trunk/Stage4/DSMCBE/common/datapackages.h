/*
 * 
 * This file contains definitions for all the avalible
 * transferable data packages in DSMCBE
 * 
 */

#ifndef DATAPACKAGES_H_
#define DATAPACKAGES_H_

#include "../dsmcbe.h"

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

struct createRequest
{
	unsigned char packageCode; // = 1
	unsigned int requestID;
	GUID dataItem;
	unsigned long dataSize;
};

struct acquireRequest
{
    //2 for read, 3 for write
    unsigned char packageCode; // = 2
    unsigned int requestID;
    GUID dataItem;
};

struct acquireResponse
{
    unsigned char packageCode; // = 4
    unsigned int requestID;
    GUID dataItem; 
    int mode;
    unsigned long dataSize;
    void* data;
};

struct writebufferReady
{
	unsigned char packageCode; // = 5
    unsigned int requestID;
    GUID dataItem;
};

struct migrationResponse
{
    unsigned char packageCode; // = 6
    unsigned int requestID;
    unsigned long dataSize;
    unsigned long waitListSize;
    void* data;
    void* waitList;
};

struct releaseRequest
{
    unsigned char packageCode; // = 7
    unsigned int requestID;
    GUID dataItem;
    int mode;
    unsigned long dataSize;
    unsigned long offset;
    void* data;
};

struct releaseResponse
{
    unsigned char packageCode; // = 8
    unsigned int requestID;
};

struct NACK
{
    unsigned char packageCode; // = 9
    unsigned int requestID;
    unsigned int hint;
};

struct invalidateRequest
{
    unsigned char packageCode; // = 10
    unsigned int requestID;
    GUID dataItem;
};

struct invalidateResponse
{
    unsigned char packageCode; // = 11
    unsigned int requestID;
};

#ifndef MAX
#define MAX(a,b) (a > b ? a : b)
#endif

#define MAX_PACKAGE_SIZE MAX(MAX(MAX(MAX(MAX(MAX(MAX(MAX(sizeof(struct createRequest), sizeof(struct acquireRequest)), sizeof(struct acquireResponse)), sizeof(struct migrationResponse)), sizeof(struct releaseRequest)), sizeof(struct releaseResponse)), sizeof(struct NACK)), sizeof(struct invalidateRequest)), sizeof(struct invalidateResponse))

struct packageBuffer
{
	unsigned char packageCode;
	unsigned char buffer[MAX_PACKAGE_SIZE - sizeof(unsigned char)];
};

#endif /*DATAPACKAGES_H_*/

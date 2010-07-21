/*
 * This file contains initializers for various packages used in DSMCBE
 */

#include <dsmcbe_initializers.h>
#include <stdlib.h>
#include <limits.h>
#include <glib.h>

extern OBJECT_TABLE_ENTRY_TYPE dsmcbe_host_number;

#define SETUP_ORIGINATOR(x) \
	(x)->originator = dsmcbe_host_number; \
	(x)->originalRecipient = UINT_MAX; \
	(x)->originalRequestID = UINT_MAX;

#define COMMON_SETUP(x, code) \
	(x)->packageCode = code;\
	(x)->dataItem = id;\
	(x)->requestID = requestId;


struct dsmcbe_createRequest* dsmcbe_new_createRequest(GUID id, unsigned int requestId, unsigned long size)
{
	struct dsmcbe_createRequest* res = (struct dsmcbe_createRequest*)MALLOC(sizeof(struct dsmcbe_createRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_CREATE_REQUEST);

	res->dataSize = size;

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_acquireRequest* dsmcbe_new_acquireRequest(GUID id, unsigned int requestId, unsigned int mode)
{
	struct dsmcbe_acquireRequest* res = (struct dsmcbe_acquireRequest*)MALLOC(sizeof(struct dsmcbe_acquireRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, mode == ACQUIRE_MODE_READ ? PACKAGE_ACQUIRE_REQUEST_READ : PACKAGE_ACQUIRE_REQUEST_WRITE);

	res->mode = mode;

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_acquireResponse* dsmcbe_new_acquireResponse(GUID id, unsigned int requestId, int mode, unsigned int writeBufferReady, unsigned long size, void* data)
{
	struct dsmcbe_acquireResponse* res = (struct dsmcbe_acquireResponse*)MALLOC(sizeof(struct dsmcbe_acquireResponse));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_ACQUIRE_RESPONSE);

	res->mode = mode;
	res->writeBufferReady = writeBufferReady;
	res->dataSize = size;
	res->data = data;

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_writebufferReady* dsmcbe_new_writeBufferReady(GUID id, unsigned int requestId)
{
	struct dsmcbe_writebufferReady* res = (struct dsmcbe_writebufferReady*)MALLOC(sizeof(struct dsmcbe_writebufferReady));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_WRITEBUFFER_READY);

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_releaseRequest* dsmcbe_new_releaseRequest(GUID id, unsigned int requestId, int mode, unsigned int size, unsigned int offset, void* data)
{
	struct dsmcbe_releaseRequest* res = (struct dsmcbe_releaseRequest*)MALLOC(sizeof(struct dsmcbe_releaseRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_RELEASE_REQUEST);

	res->mode = mode;
	res->dataSize = size;
	res->offset = offset;
	res->data = data;

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_NACK* dsmcbe_new_NACK(GUID id, unsigned int requestId)
{
	struct dsmcbe_NACK* res = (struct dsmcbe_NACK*)MALLOC(sizeof(struct dsmcbe_NACK));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_NACK);

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_invalidateRequest* dsmcbe_new_invalidateRequest(GUID id, unsigned int requestId)
{
	struct dsmcbe_invalidateRequest* res = (struct dsmcbe_invalidateRequest*)MALLOC(sizeof(struct dsmcbe_invalidateRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_INVALIDATE_REQUEST);

	SETUP_ORIGINATOR(res);

	return res;
}


struct dsmcbe_invalidateResponse* dsmcbe_new_invalidateResponse(GUID id, unsigned int requestId)
{
	struct dsmcbe_invalidateResponse* res = (struct dsmcbe_invalidateResponse*)MALLOC(sizeof(struct dsmcbe_invalidateResponse));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_INVALIDATE_RESPONSE);

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_updateRequest* dsmcbe_new_updateRequest(GUID id, unsigned int requestId, unsigned long size, unsigned long offset, void* data)
{
	struct dsmcbe_updateRequest* res = (struct dsmcbe_updateRequest*)MALLOC(sizeof(struct dsmcbe_updateRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_UPDATE);

	res->dataSize = size;
	res->offset = offset;
	res->data = data;

	//No originator info on the updateRequest
	//SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_acquireBarrierRequest* dsmcbe_new_acquireBarrierRequest(GUID id, unsigned int requestId)
{
	struct dsmcbe_acquireBarrierRequest* res = (struct dsmcbe_acquireBarrierRequest*)MALLOC(sizeof(struct dsmcbe_acquireBarrierRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_ACQUIRE_BARRIER_REQUEST);

	SETUP_ORIGINATOR(res);

	return res;
}


struct dsmcbe_acquireBarrierResponse* dsmcbe_new_acquireBarrierResponse(GUID id, unsigned int requestId)
{
	struct dsmcbe_acquireBarrierResponse* res = (struct dsmcbe_acquireBarrierResponse*)MALLOC(sizeof(struct dsmcbe_acquireBarrierResponse));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_ACQUIRE_BARRIER_RESPONSE);

	SETUP_ORIGINATOR(res);

	return res;
}


struct dsmcbe_migrationRequest* dsmcbe_new_migrationRequest(GUID id, unsigned int requestId, unsigned int targetMachine)
{
	struct dsmcbe_migrationRequest* res = (struct dsmcbe_migrationRequest*)MALLOC(sizeof(struct dsmcbe_migrationRequest));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_MIGRATION_REQUEST);

	res->targetMachine = targetMachine;

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_migrationResponse* dsmcbe_new_migrationResponse(GUID id, unsigned int requestId, int mode, unsigned long size, void* data)
{
	struct dsmcbe_migrationResponse* res = (struct dsmcbe_migrationResponse*)MALLOC(sizeof(struct dsmcbe_migrationResponse));

	if (res == NULL)
		return NULL;

	COMMON_SETUP(res, PACKAGE_MIGRATION_RESPONSE);

	res->mode = mode;
	res->dataSize = size;
	res->data = data;

	SETUP_ORIGINATOR(res);

	return res;
}

struct dsmcbe_transferRequest* dsmcbe_new_transferRequest(unsigned int requestId, pthread_mutex_t* mutex, pthread_cond_t* cond, GQueue** queue, void* from, void* to)
{
	struct dsmcbe_transferRequest* res = (struct dsmcbe_transferRequest*)MALLOC(sizeof(struct dsmcbe_transferRequest));

	if (res == NULL)
		return NULL;

	GUID id = 0;
	COMMON_SETUP(res, PACKAGE_TRANSFER_REQUEST);

	res->mutex = mutex;
	res->cond = cond;
	res->from = from;
	res->to = to;
	res->queue = queue;

	return res;

}

struct dsmcbe_transferResponse* dsmcbe_new_transferResponse(unsigned int requestId, void* from, void* to)
{
	struct dsmcbe_transferResponse* res = (struct dsmcbe_transferResponse*)MALLOC(sizeof(struct dsmcbe_transferResponse));

	if (res == NULL)
		return NULL;

	GUID id = 0;
	COMMON_SETUP(res, PACKAGE_TRANSFER_RESPONSE);

	res->from = from;
	res->to = to;

	return res;
}

struct dsmcbe_freeRequest* dsmcbe_new_freeRequest(void* data)
{
	struct dsmcbe_freeRequest* res = (struct dsmcbe_freeRequest*)MALLOC(sizeof(struct dsmcbe_freeRequest));

	if (res == NULL)
		return NULL;

	GUID id = 0;
	unsigned int requestId = 0;
	COMMON_SETUP(res, PACKAGE_FREE_REQUEST);

	res->data = data;

	return res;
}

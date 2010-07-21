#ifndef DSMCBE_INITIALIZERS_H_
#define DSMCBE_INITIALIZERS_H_

/*
 * This file contains initializers for various packages used in DSMCBE
 */

#include <datapackages.h>
#include <pthread.h>
#include <glib.h>

struct dsmcbe_createRequest* dsmcbe_new_createRequest(GUID id, unsigned int requestId, unsigned long size);
struct dsmcbe_acquireRequest* dsmcbe_new_acquireRequest(GUID id, unsigned int requestId, unsigned int mode);
struct dsmcbe_acquireResponse* dsmcbe_new_acquireResponse(GUID id, unsigned int requestId, int mode, unsigned int writeBufferReady, unsigned long size, void* data);
struct dsmcbe_writebufferReady* dsmcbe_new_writeBufferReady(GUID id, unsigned int requestId);
struct dsmcbe_releaseRequest* dsmcbe_new_releaseRequest(GUID id, unsigned int requestId, int mode, unsigned int size, unsigned int offset, void* data);
struct dsmcbe_NACK* dsmcbe_new_NACK(GUID id, unsigned int requestId);
struct dsmcbe_invalidateRequest* dsmcbe_new_invalidateRequest(GUID id, unsigned int requestId);
struct dsmcbe_invalidateResponse* dsmcbe_new_invalidateResponse(GUID id, unsigned int requestId);
struct dsmcbe_updateRequest* dsmcbe_new_updateRequest(GUID id, unsigned int requestId, unsigned long size, unsigned long offset, void* data);
struct dsmcbe_acquireBarrierRequest* dsmcbe_new_acquireBarrierRequest(GUID id, unsigned int requestId);
struct dsmcbe_acquireBarrierResponse* dsmcbe_new_acquireBarrierResponse(GUID id, unsigned int requestId);
struct dsmcbe_migrationRequest* dsmcbe_new_migrationRequest(GUID id, unsigned int requestId, unsigned int targetMachine);
struct dsmcbe_migrationResponse* dsmcbe_new_migrationResponse(GUID id, unsigned int requestId, int mode, unsigned long size, void* data);
struct dsmcbe_transferRequest* dsmcbe_new_transferRequest(unsigned int requestId, pthread_mutex_t* mutex, pthread_cond_t* cond, GQueue** queue, void* from, void* to);
struct dsmcbe_transferResponse* dsmcbe_new_transferResponse(unsigned int requestId, void* from, void* to);
struct dsmcbe_freeRequest* dsmcbe_new_freeRequest(void* data);

#endif /* DSMCBE_INITIALIZERS_H_ */

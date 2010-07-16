#ifndef DSMCBE_SPU_ASYNC_H_
#define DSMCBE_SPU_ASYNC_H_

//Return value from spu_dsmcbe_getAsyncStatus that indicates a pending operation
#define SPU_DSMCBE_ASYNC_BUSY 0
//Return value from spu_dsmcbe_getAsyncStatus that indicates a completed operation
#define SPU_DSMCBE_ASYNC_READY 1
//Return value from spu_dsmcbe_getAsyncStatus that indicates a failed operation
#define SPU_DSMCBE_ASYNC_ERROR 2

#define dsmcbe_getAsyncStatus(requestNo) (spu_dsmcbe_getAsyncStatus(requestNo))
#define dsmcbe_endAsync(requestNo, size) (spu_dsmcbe_endAsync(requestNo, size))
#define dsmcbe_beginRelease(data) (spu_dsmcbe_release_begin(data))
#define dsmcbe_beginAcquire(id, type) (spu_dsmcbe_acquire_begin(id, type))
#define dsmcbe_beginCreate(id, size) (spu_dsmcbe_create_begin(id, size))

#endif /*DSMCBE_SPU_ASYNC_H_*/


#ifndef DSMCBE_SPU_ASYNC_H_
#define DSMCBE_SPU_ASYNC_H_

#define SPU_DSMCBE_ASYNC_BUSY 0
#define SPU_DSMCBE_ASYNC_READY 1
#define SPU_DSMCBE_ASYNC_ERROR 2

#define getAsyncStatus(requestNo) spu_dsmcbe_getAsyncStatus(requestNo)
#define endAsync(requestNo, size) spu_dsmcbe_endAsync(requestNo, size)
#define beginRelease(data) spu_dsmcbe_beginRelease(data)
#define beginAcquire(id, type) spu_dsmcbe_beginAcquire(id, type)
#define beginCreate(id, size) spu_dsmcbe_beginCreate(id, size)

#endif /*DSMCBE_SPU_ASYNC_H_*/


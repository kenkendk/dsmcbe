#ifndef DSMCBE_SPU_ASYNC_H_
#define DSMCBE_SPU_ASYNC_H_

extern void* endAsync(unsigned int requestNo, unsigned long* size);
extern unsigned int beginRelease(void* data);
extern unsigned int beginAcquire(GUID id, int type);
unsigned int beginCreate(GUID id, unsigned long size);
int getAsyncStatus(unsigned int requestNo);

#endif /*DSMCBE_SPU_ASYNC_H_*/


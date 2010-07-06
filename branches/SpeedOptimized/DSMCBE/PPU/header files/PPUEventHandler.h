#ifndef PPUEVENTHANDLER_H_
#define PPUEVENTHANDLER_H_

#include "../../common/dsmcbe.h"
#include "datapackages.h"

extern void* threadCreate(GUID id, unsigned long size, int mode);
extern void* threadAcquire(GUID id, unsigned long* size, int type);
extern void threadRelease(void* data);
extern void threadAcquireBarrier(GUID id);
extern void InitializePPUHandler();

extern void* threadGet(GUID id, unsigned long* size);
extern void threadPut(GUID id, void* data);
extern void* threadMalloc(unsigned long size);

#endif /*PPUEVENTHANDLER_H_*/

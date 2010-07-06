#ifndef PPUEVENTHANDLER_H_
#define PPUEVENTHANDLER_H_

#include "dsmcbe.h"

extern void* threadCreate(GUID id, unsigned long size);
extern void* threadAcquire(GUID id, unsigned long* size, int type);
extern void threadRelease(void* data);
extern void threadAcquireBarrier(GUID id);
extern void InitializePPUHandler();

#endif /*PPUEVENTHANDLER_H_*/

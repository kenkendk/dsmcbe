#ifndef PPUEVENTHANDLER_H_
#define PPUEVENTHANDLER_H_

#include "../dsmcbe_ppu.h"
#include "../datastructures.h"
#include "datapackages.h"

extern void* threadCreate(GUID id, unsigned long size);
extern void* threadAcquire(GUID id, unsigned long* size);
extern void threadRelease(void* data);
extern void InitializePPUHandler();

#endif /*PPUEVENTHANDLER_H_*/

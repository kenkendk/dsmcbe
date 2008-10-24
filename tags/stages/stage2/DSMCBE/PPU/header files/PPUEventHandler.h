#ifndef PPUEVENTHANDLER_H_
#define PPUEVENTHANDLER_H_

#include "../../dsmcbe.h"
#include "../../common/datastructures.h"
#include "../../common/datapackages.h"

extern void* threadCreate(GUID id, unsigned long size);
extern void* threadAcquire(GUID id, unsigned long* size);
extern void threadRelease(void* data);
extern void InitializePPUHandler();
extern void TerminatePPUHandler();

#endif /*PPUEVENTHANDLER_H_*/

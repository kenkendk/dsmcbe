#include "../PPU/ThreadbasedHandler/datapackages.h"

#ifndef DSMCBE_SPU_H_
#define DSMCBE_SPU_H_

extern void* acquire(GUID id, unsigned long* size);
extern void release(void* data);

#endif /*DSMCBE_SPU_H_*/

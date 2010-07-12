#ifndef PPUEVENTHANDLER_H_
#define PPUEVENTHANDLER_H_

#include "dsmcbe.h"

extern void* dsmcbe_ppu_create(GUID id, unsigned long size);
extern void* dsmcbe_ppu_acquire(GUID id, unsigned long* size, int type);
extern void dsmcbe_ppu_release(void* data);
extern void dsmcbe_ppu_acquireBarrier(GUID id);
extern void dsmcbe_ppu_initialize();

#endif /*PPUEVENTHANDLER_H_*/

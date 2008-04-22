#include "datastructures.h"

#ifndef DSMCBE_PPU_H_
#define DSMCBE_PPU_H_

static hashtable allocatedItems;

typedef unsigned int GUID;
extern void* create(GUID id, unsigned long size);
extern void* acquire(GUID id, unsigned long* size);
extern void release(void* data);

#endif /*DSMCBE_PPU_H_*/



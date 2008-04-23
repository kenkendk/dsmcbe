#include "datastructures.h"
#include <libspe2.h>
#include <pthread.h>

#ifndef DSMCBE_PPU_H_
#define DSMCBE_PPU_H_

typedef unsigned int GUID;

static hashtable allocatedItems;
extern void* create(GUID id, unsigned long size);
extern void* acquire(GUID id, int requestID);
extern void release(GUID id, int requestID);
extern void* ppu_pthread_com_function(void* arg);
extern void setup();

#endif /*DSMCBE_PPU_H_*/



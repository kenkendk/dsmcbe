#ifndef DSMCBE_PPU_H_
#define DSMCBE_PPU_H_

typedef unsigned int GUID;

#include <libspe2.h>
#include <pthread.h>

extern void* create(GUID id, unsigned long size);
extern void* acquire(GUID id, unsigned long* size);
extern void release(void* data);
extern void initialize(spe_context_ptr_t* threads, unsigned int thread_count);

#endif /*DSMCBE_PPU_H_*/



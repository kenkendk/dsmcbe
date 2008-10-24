#ifndef DSMCBE_PPU_H_
#define DSMCBE_PPU_H_

#include "./dsmcbe.h"
#include <libspe2.h>
#include <pthread.h>

extern void initialize(spe_context_ptr_t* threads, unsigned int thread_count);
extern pthread_t* simpleInitialize(unsigned int thread_count);
extern void terminate();

#endif /*DSMCBE_PPU_H_*/



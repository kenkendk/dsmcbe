#ifndef DSMCBE_PPU_H_
#define DSMCBE_PPU_H_

#include "./dsmcbe.h"
#include <libspe2.h>
#include <pthread.h>

extern void initialize(spe_context_ptr_t* threads, unsigned int thread_count, int* sockets, unsigned int socketsCount);
extern pthread_t* simpleInitialize(unsigned int id, char* file, unsigned int thread_count);
extern unsigned int DSMCBE_MachineCount();

#endif /*DSMCBE_PPU_H_*/



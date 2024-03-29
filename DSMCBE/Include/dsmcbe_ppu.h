#ifndef DSMCBE_PPU_H_
#define DSMCBE_PPU_H_

#include "./dsmcbe.h"
#include <libspe2.h>
#include <pthread.h>

extern void dsmcbe_initialize(spe_context_ptr_t* threads, unsigned int thread_count, int* sockets, unsigned int socketsCount);
extern void dsmcbe_terminate();
extern pthread_t* dsmcbe_simpleInitialize(unsigned int id, char* file, unsigned int thread_count);
extern unsigned int dsmcbe_MachineCount();
extern void dsmcbe_display_network_startup(int value);
extern void dsmcbe_SetHardwareThreads(unsigned int count);

#endif /*DSMCBE_PPU_H_*/



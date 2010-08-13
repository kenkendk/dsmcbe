#ifndef DSMCBE_SPU_H_
#define DSMCBE_SPU_H_

#include "./dsmcbe.h"
extern void dsmcbe_initialize();
extern void dsmcbe_terminate();
extern void* spu_dsmcbe_memory_malloc(unsigned long size);
extern void spu_dsmcbe_memory_free(void* data);
extern unsigned int spu_dsmcbe_acquire_begin(GUID id, int type);
extern void spu_dsmcbe_release_begin(void* data);

extern unsigned int spu_dsmcbe_getAsyncStatus(unsigned int requestNo);
extern void* spu_dsmcbe_endAsync(unsigned int requestNo, unsigned long* size);

//The main function entry point
extern int dsmcbe_main(unsigned long long speid, unsigned int machineid, unsigned int threadid);

#define MALLOC(x) spu_dsmcbe_memory_malloc(x)
#define MALLOC_ALIGN(x,y) spu_dsmcbe_memory_malloc(x)
#define FREE(x) spu_dsmcbe_memory_free(x)
#define FREE_ALIGN(x) spu_dsmcbe_memory_free(x)


#include "dsmcbe_spu_threads.h"
#include "dsmcbe_spu_async.h"


#endif /*DSMCBE_SPU_H_*/

#ifndef DSMCBE_SPU_H_
#define DSMCBE_SPU_H_

#include "./dsmcbe.h"
extern void initialize();
extern void terminate();
extern void clean(GUID id);
extern void getStats();
extern void* spu_dsmcbe_memory_malloc(unsigned long size);
extern void spu_dsmcbe_memory_free(void* data);

#define MALLOC(x) spu_dsmcbe_memory_malloc(x)
#define MALLOC_ALIGN(x,y) spu_dsmcbe_memory_malloc(x)
#define FREE(x) spu_dsmcbe_memory_free(x)
#define FREE_ALIGN(x) spu_dsmcbe_memory_free(x)


#include "dsmcbe_spu_threads.h"
#include "dsmcbe_spu_async.h"


#endif /*DSMCBE_SPU_H_*/

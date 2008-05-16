#ifndef DSMCBE_SPU_H_
#define DSMCBE_SPU_H_

#include "./dsmcbe.h"
extern void initialize();
extern void terminate();
extern void clean(GUID id);

#include "dsmcbe_spu_threads.h"
#include "dsmcbe_spu_async.h"


#endif /*DSMCBE_SPU_H_*/

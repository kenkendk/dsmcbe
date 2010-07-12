/*
 * 
 * This file contains declarations for communicating with the SPU Event Handler
 * 
 */

#ifndef SPUEVENTHANDLER_H_
#define SPUEVENTHANDLER_H_

#include <malloc.h>
#include <libspe2.h>
#include <dsmcbe.h>

extern void dsmcbe_spu_initialize(spe_context_ptr_t* threads, unsigned int thread_count);
extern void dsmcbe_spu_terminate(int force);

#endif /*SPUEVENTHANDLER_H_*/

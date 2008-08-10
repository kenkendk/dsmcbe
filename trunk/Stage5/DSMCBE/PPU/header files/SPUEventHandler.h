/*
 * 
 * This file contains declarations for communcating with the SPU Event Handler
 * 
 */

#ifndef SPUEVENTHANDLER_H_
#define SPUEVENTHANDLER_H_

#include <malloc.h>
#include <libspe2.h>
#include "../../dsmcbe.h"
#include "SPU_MemoryAllocator.h"
#include "SPU_MemoryAllocator_Shared.h"

extern void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count);
extern void TerminateSPUHandler(int force);

#endif /*SPUEVENTHANDLER_H_*/

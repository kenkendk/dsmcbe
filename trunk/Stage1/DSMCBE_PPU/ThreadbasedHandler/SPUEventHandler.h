/*
 * 
 * This file contains declarations for communcating with the SPU Event Handler
 * 
 */

#ifndef SPUEVENTHANDLER_H_
#define SPUEVENTHANDLER_H_

#include <malloc.h>
#include <libspe2.h>
#include "../datastructures.h"
#include "../../Common/dsmcbe.h"

//The number of messages the hardware allows in the MBOX queue
#define MBOX_HW_QUEUE_SIZE 4

extern void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count);
extern void TerminateSPUHandler(int force);

#endif /*SPUEVENTHANDLER_H_*/

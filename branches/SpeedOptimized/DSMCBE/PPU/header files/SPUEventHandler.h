/*
 * 
 * This file contains declarations for communcating with the SPU Event Handler
 * 
 */

#ifndef SPUEVENTHANDLER_H_
#define SPUEVENTHANDLER_H_

#include <malloc.h>
#include <libspe2.h>
#include "../../common/dsmcbe.h"

extern void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count);
extern void TerminateSPUHandler(int force);

//Warning: Do not change the structure layout as it is used to send data to the SPU's
struct internalMboxArgs
{
	unsigned int packageCode;
	unsigned int requestId;
	unsigned int data;
	unsigned int size;
	GUID id;
	int mode;
};

#endif /*SPUEVENTHANDLER_H_*/

#include <free_align.h>
#include <malloc_align.h>
#include <stdio.h>
#include "dsmcbe.h"
#include "ThreadbasedHandler/PPUEventHandler.h"
#include "ThreadbasedHandler/SPUEventHandler.h"
#include "ThreadbasedHandler/RequestCoordinator.h"

void initialize(spe_context_ptr_t* threads, unsigned int thread_count)
{
	InitializeCoordinator();
	InitializePPUHandler();
	InitializeSPUHandler(threads, thread_count);	
}

void* create(GUID id, unsigned long size){
	return threadCreate(id, size);
}

void* acquire(GUID id, unsigned long* size){
	return threadAcquire(id, size);	
}

void release(void* data){
	threadRelease(data);
}

#include <free_align.h>
#include <malloc_align.h>
#include <stdio.h>
#include "dsmcbe.h"
#include "ThreadbasedHandler/PPUEventHandler.h"
#include "ThreadbasedHandler/SPUEventHandler.h"
#include "ThreadbasedHandler/RequestCoordinator.h"
#include <pthread.h>
#include "dsmcbe_ppu.h"
#include "../Common/debug.h"

static int mustrelease_spe_id = 0;
extern spe_program_handle_t SPU;

void* ppu_pthread_function(void* arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	printf(WHERESTR "Starting SPU\n", WHEREARG);
	if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0)
	{
		perror ("Failed running context");
		return NULL;
	}
	printf(WHERESTR "Terminated SPU\n", WHEREARG);
	pthread_exit(NULL);
}


pthread_t* simpleInitialize(unsigned int thread_count)
{
	size_t i;
	spe_context_ptr_t* spe_ids;
	pthread_t* spu_threads;
	
	spe_ids = (spe_context_ptr_t*)malloc(thread_count * sizeof(spe_context_ptr_t));
	spu_threads = (pthread_t*)malloc(thread_count * sizeof(pthread_t));

	mustrelease_spe_id = 1;
	
	// Create several SPE-threads to execute 'SPU'.
	for(i = 0; i < thread_count; i++){
		// Create context
		if ((spe_ids[i] = spe_context_create (0, NULL)) == NULL) 
		{
			perror ("Failed creating context");
			return NULL;
		}

		// Load program into context
		if (spe_program_load (spe_ids[i], &SPU)) 
		{
			perror ("Failed loading program");
			return NULL;
		}

		printf(WHERESTR "Starting SPU thread\n", WHEREARG);
		// Create thread for each SPE context
		if (pthread_create (&spu_threads[i], NULL,	&ppu_pthread_function, &spe_ids[i])) 
		{
			perror ("Failed creating thread");
			return NULL;
		}
	}
	
	initialize(spe_ids, thread_count);
	
	return spu_threads;
}

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

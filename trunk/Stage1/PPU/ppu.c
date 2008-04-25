#include "dsmcbe.h"
#include <stdio.h>
#include "guids.h"

extern spe_program_handle_t SPU;

#define SPU_THREADS 1

void* ppu_pthread_function(void* arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0)
	{
		perror ("Failed running context");
		return NULL;
	}
	pthread_exit(NULL);
}

int main(int argc, char **argv) {
	int i;

	printf("ppu.c: Starting\n");

	unsigned int cont[1] = {0x1};
	int total_reads = SPU_THREADS;
	spe_context_ptr_t spe_ids[SPU_THREADS];
	pthread_t spu_threads[SPU_THREADS];
	pthread_t com_threads[SPU_THREADS];
	
	setup();
	
	printf("ppu.c: Creating\n");
	int* data = create(ETTAL, sizeof(int));
	(*data) = 928;
	
	printf("ppu.c: Data location is %i\n", data);
			
	// Create several SPE-threads to execute 'SPU'.
	for(i = 0; i < SPU_THREADS; i++){
		// Create context
		if ((spe_ids[i] = spe_context_create (0, NULL)) == NULL) 
		{
			perror ("ppu.c: Failed creating context");
			return -1;
		}

		// Load program into context
		if (spe_program_load (spe_ids[i], &SPU)) 
		{
			perror ("Failed loading program");
			return -1;
		}

		printf("ppu.c: Starting SPU thread\n");
		// Create thread for each SPE context
		if (pthread_create (&spu_threads[i], NULL,	&ppu_pthread_function, &spe_ids[i])) 
		{
			perror ("Failed creating thread");
			return -1;
		}
	}
	
	printf("ppu.c: Going to sleep\n");
	sleep(1);
	
	printf("ppu.c: Releasing\n");
	release(ETTAL, 1);	
	
	
	initialize(spe_ids, SPU_THREADS);
	
	GUID id = 1;
	
	printf("ppu.c: Creating\n");
	int* data = create(id, sizeof(int));
	(*data) = 928;
	
	printf("ppu.c: Data location is %i\n", data);
	
	printf("ppu.c: Releasing\n");
	release(data);
		
	pthread_join(spu_threads[0], NULL);
	
	return 0;
}

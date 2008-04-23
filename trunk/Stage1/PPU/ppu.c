#include "dsmcbe_ppu.h"
#include <stdio.h>
#include <spu_intrinsics.h>

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
	
	GUID id = 1;
	
	printf("ppu.c: Creating\n");
	int* data = create(id, sizeof(int));
	(*data) = 928;
	
	printf("ppu.c: Data location is %i\n", data);
	
	printf("ppu.c: Releasing\n");
	release(id, 1);
		
	// Create several SPE-threads to execute 'SPU'.
	for(i = 0; i < SPU_THREADS; i++){
		// Create context
		if ((spe_ids[i] = spe_context_create (0, NULL)) == NULL) 
		{
			perror ("ppu.c: Failed creating context\n");
			return -1;
		}

		// Create communication thread for each SPE context
		printf("ppu.c: Starting communication thread\n");
		pthread_create(&com_threads[i], NULL, &ppu_pthread_com_function, &spe_ids[i]);	
		printf("ppu.c: Started communication thread\n");
		
		// Load program into context
		if (spe_program_load (spe_ids[i], &SPU)) 
		{
			perror ("Failed loading program\n");
			return -1;
		}

		printf("ppu.c: Starting SPU thread\n");
		// Create thread for each SPE context
		if (pthread_create (&spu_threads[i], NULL,	&ppu_pthread_function, &spe_ids[i])) 
		{
			perror ("Failed creating thread\n");
			return -1;
		}
		printf("ppu.c: Started SPU thread\n");
		
		printf("ppu.c: Sending start signal to SPU\n");
		spe_in_mbox_write(spe_ids[i], cont, 1, SPE_MBOX_ALL_BLOCKING);
	}
	
	while (total_reads > 0)
	{		
		for(i = 0; i < SPU_THREADS; i++)
		{
			if (spe_out_mbox_status(spe_ids[i]) != 0)
			{
				printf("ppu.c: Recieved signal from SPU\n");
				void* data;
				spe_out_mbox_read(spe_ids[i], data, sizeof(int));
				if((int)data == 1) {
					printf("ppu.c: Recieved data: %i from SPU\n", (int)data);
					total_reads--;
				}				
			}
		}
	}
	return 0;
}

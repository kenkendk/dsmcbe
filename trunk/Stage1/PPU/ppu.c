#include "../Common/dsmcbe.h"
#include "dsmcbe_ppu.h"
#include <stdio.h>
#include "../Common/guids.h"
#define SPU_THREADS 1

int main(int argc, char **argv) {

	printf("ppu.c: Starting\n");

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	/*printf("ppu.c: Going to sleep\n");
	sleep(1);*/
	
	printf("ppu.c: Creating\n");
	int* data = create(ETTAL, sizeof(int));
	(*data) = 928;
	
	printf("ppu.c: Data location is %i\n", (unsigned int)data);
	
	printf("ppu.c: Releasing\n");
	release(data);
	
	printf("ppu.c: Released, waiting for SPU to complete\n");
	pthread_join(spu_threads[SPU_THREADS - 1], NULL);
	
	printf("All done, exiting cleanly\n");
	
	return 0;
}

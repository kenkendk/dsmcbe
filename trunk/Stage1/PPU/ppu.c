#include "../Common/dsmcbe.h"
#include "dsmcbe_ppu.h"
#include <stdio.h>
#include "../Common/guids.h"
#define SPU_THREADS 1
#include "../Common/debug.h"

int main(int argc, char **argv) {

	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	/*printf("ppu.c: Going to sleep\n");
	sleep(1);*/
	
	printf(WHERESTR "Creating\n", WHEREARG);
	int* data = create(ETTAL, sizeof(int));
	(*data) = 928;
	
	printf(WHERESTR "Data location is %i\n", WHEREARG, (unsigned int)data);
	
	printf(WHERESTR "Releasing\n");
	release(data);
	
	printf(WHERESTR "Released, waiting for SPU to complete\n", WHEREARG);
	pthread_join(spu_threads[SPU_THREADS - 1], NULL);
	
	printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	
	return 0;
}

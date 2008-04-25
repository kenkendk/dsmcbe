#include "../DSMCBE_PPU/dsmcbe_ppu.h"
#include <stdio.h>
#include "../Common/guids.h"
#include "../Common/debug.h"

#define SPU_THREADS 1

int main(int argc, char **argv) {
	
	unsigned long size;

	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	/*printf("ppu.c: Going to sleep\n");
	sleep(1);*/
	
	printf(WHERESTR "Creating\n", WHEREARG);
	int* data = create(ETTAL, sizeof(int));
	(*data) = 928;
	
	printf(WHERESTR "Data location is %i\n", WHEREARG, (unsigned int)data);
	
	printf(WHERESTR "Releasing\n", WHEREARG);
	release(data);
	
	printf(WHERESTR "Released, waiting for SPU to complete\n", WHEREARG);
	pthread_join(spu_threads[SPU_THREADS - 1], NULL);
	
	data = acquire(ETTAL, &size);
	printf(WHERESTR "ppu.c: Data location is %i\n", WHEREARG, (unsigned int)data);
	printf(WHERESTR "ppu.c: Value is %i\n", WHEREARG, *data);
	
	release(data);
	
	printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	
	return 0;
}

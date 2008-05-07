#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>

#define SPU_THREADS 2

int main(int argc, char **argv) {
	
	int i, j, k;
	unsigned char* data;
	int size = 200;
	
	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	for(i = 0; i < 1; i++) {
		printf(WHERESTR "Creating\n", WHEREARG);
		data = create(ETTAL + i, size * size * sizeof(unsigned char));
		for(j = 0; j < size; j++)
			for(k = 0; k < size; k++)
				data[(j*size)+k] = 1;
				
		printf(WHERESTR "Releasing\n", WHEREARG);
		release(data);
	}
	
	for (i = 0; i < SPU_THREADS; i++)
		pthread_join(spu_threads[i], NULL);
	
	printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	
	return 0;
}

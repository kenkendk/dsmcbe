#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>

#define SPU_THREADS 2

int main(int argc, char **argv) {
	
	int i;
	int* data;
	unsigned long size;
	
	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	printf(WHERESTR "Creating data with id: %i\n", WHEREARG, ETTAL);
	data = create(ETTAL, sizeof(int));
	*data = 928;			
	printf(WHERESTR "Value of data with id: %i, is now set to: %i\n", WHEREARG, ETTAL, 928);
	release(data);
	printf(WHERESTR "Data with id: %i released\n", WHEREARG, ETTAL);	
	
	sleep(3);
	
	printf(WHERESTR "Acquiring data with id: %i\n", WHEREARG, ETTAL);
	data = acquire(ETTAL, &size, WRITE);
	*data = 153;
	printf(WHERESTR "Value of data with id: %i, is now set to: %i\n", WHEREARG, ETTAL, 153);
	release(data);
	printf(WHERESTR "Data with id: %i released\n", WHEREARG, ETTAL);
	
	for (i = 0; i < SPU_THREADS; i++)
		pthread_join(spu_threads[i], NULL);
	
	printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	
	return 0;
}

#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>

#define SPU_THREADS 1

int main(int argc, char **argv) {
	
	unsigned long size;
	int i;
	int* data;
	
	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	printf(WHERESTR "Creating\n", WHEREARG);
	data = create(ETTAL, sizeof(int));
	(*data) = 928;
	
	printf(WHERESTR "Data location is %u\n", WHEREARG, (unsigned int)data);
	printf(WHERESTR "Data value is %i\n", WHEREARG, *data);	
	printf(WHERESTR "Releasing\n", WHEREARG);
	release(data);
	
	data = acquire(ETTAL, &size, READ);				
	printf(WHERESTR "Data location is %u\n", WHEREARG, (unsigned int)data);
	printf(WHERESTR "Data value is %i\n", WHEREARG, *data);		
	release(data);

	for (i = 0; i < SPU_THREADS; i++)
		pthread_join(spu_threads[i], NULL);
	
	data = acquire(ETTAL, &size, READ);				
	printf(WHERESTR "Data location is %u\n", WHEREARG, (unsigned int)data);
	printf(WHERESTR "Data value is %i\n", WHEREARG, *data);		
	release(data);
				
	printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	
	return 0;
}

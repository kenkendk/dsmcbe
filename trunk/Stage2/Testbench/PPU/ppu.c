#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>

#define SPU_THREADS 1

int main(int argc, char **argv) {
	
	unsigned long size;
	unsigned int items;
	size_t i;

	printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(SPU_THREADS);

	printf(WHERESTR "Creating\n", WHEREARG);
	int* data = create(ETTAL, sizeof(int));
	(*data) = 928;
	
	printf(WHERESTR "Data location is %i\n", WHEREARG, (unsigned int)data);
	
	printf(WHERESTR "Releasing\n", WHEREARG);
	release(data);
	
	data = acquire(ETTAL, &size);
	printf(WHERESTR "Data location is %i\n", WHEREARG, (unsigned int)data);
	printf(WHERESTR "Value is %i. The expected value is 210.\n", WHEREARG, *data);
	
	release(data);

	
	items = 16 * 1025;
	unsigned int* largeblock = create(LARGE_ITEM, items * sizeof(unsigned int));

	printf(WHERESTR "Created large block at %d\n", WHEREARG, (unsigned int)largeblock);
	for(i = 0; i < items; i++)
		largeblock[i] = i;
	
	printf(WHERESTR "Releasing large block with %d items\n", WHEREARG, items);
	release(largeblock);

	printf(WHERESTR "Acquiring SPU item\n", WHEREARG);
	data = acquire(SPUITEM, &size);
	printf(WHERESTR "Acquired SPU item, value is %d. Expected value 4.\n", WHEREARG, (*data));
	release(data);
	printf(WHERESTR "Released SPU item\n", WHEREARG);
	
	
	
	printf(WHERESTR "Re-acquire\n", WHEREARG);
	largeblock = acquire(LARGE_ITEM, &size);
	printf(WHERESTR "Acquired large block at %d (%d)\n", WHEREARG, (unsigned int)largeblock, (unsigned int)size);
	items = size / sizeof(unsigned int);
	
	for(i = 0; i < items; i++)
	{
		if (largeblock[i] != (i + 2))
			printf(WHERESTR "Ivalid value at %d\n", WHEREARG, i);
	}
		
	release(largeblock);
	printf(WHERESTR "Tests completed, shutting down\n", WHEREARG);
	
	printf(WHERESTR "Released, waiting for SPU to complete\n", WHEREARG);
	for(i = 0; i < SPU_THREADS; i++)
		pthread_join(spu_threads[i], NULL);

	printf(WHERESTR "All SPU's are terminated, cleaning up\n", WHEREARG);
	//terminate();

	printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	return 0;
}

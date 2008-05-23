#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>
#include <stdlib.h>

unsigned int id;
char* file;
unsigned int SPU_THREADS;

int main(int argc, char **argv) {
	
	unsigned long size;
	unsigned int items;
	size_t i;
	
	if(argc != 4)
	{
		printf("Wrong number of arguments \"./PPU id network-file spu-threads\"\n");
		return -1;
	}
	
	id = atoi(argv[1]);
	file = argv[2]; 	
	SPU_THREADS = atoi(argv[3]);
	
	printf(WHERESTR "id: %i, file %s, SPU_THREADS %i\n", WHEREARG, id, file, SPU_THREADS);
	
	//printf(WHERESTR "Starting\n", WHEREARG);

	pthread_t* spu_threads;
	spu_threads = simpleInitialize(id, file, SPU_THREADS);

	int* data;
	
	printf(WHERESTR "%d: Connected, starting\n", WHEREARG, id);
	sleep(1);

	if (id == 0)
	{
		printf(WHERESTR "%d: Creating\n", WHEREARG, id);
		data = create(ETTAL, sizeof(int));
		(*data) = 928;
		
		printf(WHERESTR "%d: Data location is %i\n", WHEREARG, id, (unsigned int)data);
		
		printf(WHERESTR "%d: Releasing\n", WHEREARG, id);
		release(data);
	}
	else
	{
		printf(WHERESTR "%d: Reading\n", WHEREARG, id);
		data = acquire(ETTAL, &size, READ);

		printf(WHERESTR "%d: Read: %d\n", WHEREARG, id, *data);
		
		release(data);

		printf(WHERESTR "%d: Released\n", WHEREARG, id);
	}
	
	sleep(5);
	
	release(acquire(LOCK_ITEM_SPU, &size, READ));
	//sleep(1);
	
	//printf(WHERESTR "Updating data\n", WHEREARG);
	
	data = acquire(ETTAL, &size, WRITE);
	
	*data = 210;
	
	//printf(WHERESTR "Data location is %i\n", WHEREARG, (unsigned int)data);
	//printf(WHERESTR "Value is %i. The expected value is 210.\n", WHEREARG, *data);
	
	//printf(WHERESTR "Releasing, this should invalidate SPU\n", WHEREARG);
	release(data);
	
	release(create(LOCK_ITEM_PPU, 0)); 
	//printf(WHERESTR "Released\n", WHEREARG, *data);

	
	items = 16 * 1025;
	unsigned int* largeblock = create(LARGE_ITEM, items * sizeof(unsigned int));

	printf(WHERESTR "Created large block at %d\n", WHEREARG, (unsigned int)largeblock);
	for(i = 0; i < items; i++)
		largeblock[i] = i;
	
	printf(WHERESTR "Releasing large block with %d items\n", WHEREARG, items);
	release(largeblock);


	printf(WHERESTR "Creating large sequence, %d blocks of size %d\n", WHEREARG, SEQUENCE_COUNT, items * sizeof(unsigned int));
	for(i = 0; i < SEQUENCE_COUNT; i++)
		release(create(LARGE_SEQUENCE + i, items * sizeof(unsigned int)));

	/*printf(WHERESTR "Creating small sequence, %d blocks of size %d\n", WHEREARG, SMALL_SEQUENCE_COUNT, sizeof(unsigned int));
	for(i = 0; i < SMALL_SEQUENCE_COUNT; i++)
		release(create(SMALL_SEQUENCE + i, sizeof(unsigned int)));
	*/
	
	printf(WHERESTR "Acquiring SPU item\n", WHEREARG);
	data = acquire(SPUITEM, &size, WRITE);
	printf(WHERESTR "Acquired SPU item, value is %d. Expected value 4.\n", WHEREARG, (*data));
	release(data);
	printf(WHERESTR "Released SPU item\n", WHEREARG);
	
	/*
	
	printf(WHERESTR "Re-acquire\n", WHEREARG);
	largeblock = acquire(LARGE_ITEM, &size, WRITE);
	printf(WHERESTR "Acquired large block at %d (%d)\n", WHEREARG, (unsigned int)largeblock, (unsigned int)size);
	items = size / sizeof(unsigned int);
	
	for(i = 0; i < items; i++)
	{
		if (largeblock[i] != (i + (2 * SPU_FIBERS)))
			printf(WHERESTR "Invalid value at %d\n", WHEREARG, i);
	}
		
	release(largeblock);
	printf(WHERESTR "Tests completed, shutting down\n", WHEREARG);
	
	*/
	
	//printf(WHERESTR "Released, waiting for SPU to complete\n", WHEREARG);
	
	for(i = 0; i < SPU_THREADS; i++)
		pthread_join(spu_threads[i], NULL);

	//printf(WHERESTR "All SPU's are terminated, cleaning up\n", WHEREARG);
	//terminate();

	//printf(WHERESTR "All done, exiting cleanly\n", WHEREARG);
	return 0;
}

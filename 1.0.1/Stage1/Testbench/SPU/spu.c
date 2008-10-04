#include "spu.h"
#include <stdio.h>
#include <spu_intrinsics.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_mfcio.h> 
#include "../guids.h"
#include <common/debug.h>

#define MAXRUN 1000000

int main(int argc, char **argv) {
	
	initialize();
	
	printf(WHERESTR "Hello World\n", WHEREARG);
	unsigned long size;
	unsigned int i;
	unsigned int items;
	
	int* allocation = acquire(ETTAL, &size);

	printf(WHERESTR "Value read from acquire is: %i\n", WHEREARG, *allocation);
	
	*allocation = 210;
			
	release(allocation);
	printf(WHERESTR "Release completed\n", WHEREARG);
	
	
	printf(WHERESTR "Reading large value\n", WHEREARG);
	unsigned int* largeblock = acquire(LARGE_ITEM, &size);
	items = size / sizeof(unsigned int);
	printf(WHERESTR "Read large value (%d, %d)\n", WHEREARG, size, items);
	
	
	for(i = 0; i < items; i++)
	{
		if (largeblock[i] != i)
			printf(WHERESTR "Ivalid value at %d\n", WHEREARG, i);
		largeblock[i] = i + 2;
	}
	
	printf(WHERESTR "Releasing large value\n", WHEREARG);
	release(largeblock);
	printf(WHERESTR "Released large value\n", WHEREARG);
	
	printf(WHERESTR "Starting long memory test\n", WHEREARG);
	
	for(i = 0; i < MAXRUN; i++)
	{
		release(acquire(LARGE_ITEM, &size));
		if (i % 100 == 0)
			printf(WHERESTR "At itteration %d of %d\n", WHEREARG, i, MAXRUN);
	}
	
	printf(WHERESTR "Done\n", WHEREARG);
	
	return 0;
}

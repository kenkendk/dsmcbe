#include "spu.h"
#include <stdio.h>
#include <spu_intrinsics.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_mfcio.h> 
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>

int main(int argc, char **argv) {
	
	unsigned long size;
	int* data; 
	
	initialize();	
	printf(WHERESTR "Hello World\n", WHEREARG);
	
	sleep(1);
	
	printf(WHERESTR "Acquiring data with id: %i\n", WHEREARG, ETTAL);
	data = acquire(ETTAL, &size, READ);
	printf(WHERESTR "Data with id: %i has value: %i\n", WHEREARG, ETTAL, *data);					
	release(data);
	printf(WHERESTR "Data with id: %i released\n", WHEREARG, ETTAL);
	
	sleep(7);
	
	printf(WHERESTR "Acquiring data with id: %i\n", WHEREARG, ETTAL);
	data = acquire(ETTAL, &size, READ);
	printf(WHERESTR "Data with id: %i has value: %i\n", WHEREARG, ETTAL, *data);					
	release(data);
	printf(WHERESTR "Data with id: %i released\n", WHEREARG, ETTAL);

	printf(WHERESTR "Done\n", WHEREARG);

	return 0;
}

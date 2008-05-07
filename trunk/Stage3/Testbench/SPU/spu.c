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
	
	int i,j,k;
	unsigned long space;
	unsigned char* data; 
	int size = 200;
	
	initialize();	
	printf(WHERESTR "Hello World\n", WHEREARG);
	
	for(i = 0; i < 1; i++) {
		data = acquire(ETTAL + i, &space, WRITE);
		for(j = 0; j < size; j++)
			for(k = 0; k < size; k++)
				if (data[(j * size) + k] != 1)
					printf(WHERESTR "Error in ETTAL with id; %i at position (%i,%i) value was: %i\n", WHEREARG, i, j, k, data[(j * size) + k]);			
				
		printf(WHERESTR "Releasing\n", WHEREARG);
		release(data);
	}
	
	printf(WHERESTR "Done\n", WHEREARG);
	
	return 0;
}

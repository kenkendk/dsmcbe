#include "spu.h"
#include <stdio.h>
#include <spu_intrinsics.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_mfcio.h> 
#include "../guids.h"
#include <common/debug.h>


int main(int argc, char **argv) {
	
	initialize();
	
	//int data = (int)spu_read_in_mbox();
	
	/*if(data == 1)*/ {
		printf(WHERESTR "Hello World\n", WHEREARG);
		unsigned long size;
		
		int* allocation = acquire(ETTAL, &size);
	
		printf(WHERESTR "Value read from acquire is: %i\n", WHEREARG, *allocation);
		
		*allocation = 210;
				
		release(allocation);
		printf(WHERESTR "Release completed\n", WHEREARG);
	}
	
	printf(WHERESTR "Done\n", WHEREARG);
	
	return 0;
}

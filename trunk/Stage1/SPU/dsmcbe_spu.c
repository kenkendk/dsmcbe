#include "dsmcbe_spu.h"
#include <stdio.h>
#include <spu_intrinsics.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_mfcio.h> 
#include "datastructures.h"

int main(int argc, char **argv) {
	
	int data = (int)spu_read_in_mbox();
	
	if(data == 1) {
		printf("spu.c: Hello World\n");
		spu_write_out_mbox(1);
		spu_write_out_mbox(2);
		spu_write_out_mbox(1);
		
		data = spu_read_in_mbox();
		printf("spu.c: Message type: %i\n", (int)data);
		
		data = spu_read_in_mbox();
		printf("spu.c: Request id: %i\n", (int)data);
		
		data = spu_read_in_mbox();
		printf("spu.c: Data size: %i\n", (int)data);
		
		data = spu_read_in_mbox();
		printf("spu.c: Data EA: %i\n", (int)data);		
	}
	
	spu_write_out_mbox(1);
	
	return 0;
}

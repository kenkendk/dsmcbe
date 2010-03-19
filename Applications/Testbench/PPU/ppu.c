#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>

#include <datapackages.h>

#define REPETITIONS 100000

#define VALUE 100

unsigned int id;
char* file;
unsigned int SPU_THREADS;
extern spe_context_ptr_t* spe_threads;
unsigned long size;

int main(int argc, char **argv)
{
	printf("Compile time - %s\n", __TIME__);
	printf("Ready\n");
	pthread_t* spu_threads;
	spu_threads = simpleInitialize(id, NULL, 0);

	printf("Initialize done\n");

	int* item = create(VALUE, sizeof(int), CREATE_MODE_NONBLOCKING);
	*item = 1;
	printf("Value set to %d\n", *item);
	release(item);
	
	printf("Item released\n");

	item = acquire(VALUE, &size, ACQUIRE_MODE_DELETE);
	printf("Value before delete %d\n", *item);
	release(item);
	
	item = create(VALUE, sizeof(int), CREATE_MODE_NONBLOCKING);
	*item = 255;
	printf("Value set to %d\n", *item);
	release(item);
	
	printf("Item released after second create\n");

	item = acquire(VALUE, &size, ACQUIRE_MODE_READ);
	printf("Value is %d\n", *item);
	release(item);
		
	return 0;
}

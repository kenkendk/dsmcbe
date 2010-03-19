#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <common/debug.h>
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>

#include <datapackages.h>

#define REPETITIONS 100000

#define VALUE1 100
#define VALUE2 200

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

	int* item = create(VALUE1, sizeof(int), CREATE_MODE_NONBLOCKING);
	*item = 1;
	printf("Value set to %d\n", *item);
	release(item);
	
	printf("Item released\n");

	item = acquire(VALUE1, &size, ACQUIRE_MODE_DELETE);
	printf("Value before delete %d\n", *item);
	release(item);
	
	item = create(VALUE2, sizeof(int), CREATE_MODE_NONBLOCKING);
	*item = 255;
	printf("Value set to %d\n", *item);
	release(item);
	printf("Item released\n");

	item = create(VALUE1, sizeof(int), CREATE_MODE_NONBLOCKING);
	*item = 210;
	printf("Value set to %d\n", *item);
	release(item);
	printf("Item released\n");

	item = acquire(VALUE1, &size, ACQUIRE_MODE_READ);
	printf("Value is %d\n", *item);
	release(item);
	printf("Item released\n");

	item = acquire(VALUE2, &size, ACQUIRE_MODE_READ);
	printf("Value is %d\n", *item);
	release(item);
	printf("Item released\n");

	return 0;
}

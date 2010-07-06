#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "guids.h"
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
	int i;
	unsigned long size;
	int* data;

	int spu_count = 6;

	printf("Compile time - %s\n", __TIME__);
	printf("Ready\n");
	pthread_t* spu_threads;
	spu_threads = simpleInitialize(0, NULL, spu_count);

	printf("Initialize done\n");

	data = create(PROCESS_COUNTER, sizeof(int), CREATE_MODE_NONBLOCKING);
	*data = 0;
	release(data);

	create(START_BARRIER, spu_count, CREATE_MODE_BARRIER);
	release(acquire(COMPLETE_LOCK, &size, ACQUIRE_MODE_READ));

	/*struct timespec time;
	time.tv_sec = 0;

	for(i = 0; i < REPETITIONS; i++)
	{
		time.tv_nsec = rand() / 500;
		nanosleep(&time, NULL);

		//printf("Reading data\n");
		data = acquire(CONTENTION_ITEM, &size, ACQUIRE_MODE_DELETE);
		if (i % 100 == 0)
			printf("Iteration %d\n", i);

		time.tv_nsec = rand() / 500;
		nanosleep(&time, NULL);

		release(data);
		//printf("Released data\n");
	}*/
	
	return 0;
}

#include <dsmcbe_spu.h>
#include <stdlib.h>
#include <stdio.h>
#include <spu_mfcio.h>
#include <math.h>
#include <libmisc.h>
#include <debug.h>
#include "../PPU/guids.h"
#include <dsmcbe.h>
#include <time.h>

//We need the counter on the heap so the threads share it
//static int counter = 0;

void TEST();

int main(int argc, char** argv) {

	int spuid;
	unsigned long size;
	int* data;
	int count = 0;

	initialize();

	printf(WHERESTR "Initialized\n", WHEREARG);

	data = acquire(PROCESS_COUNTER, &size, ACQUIRE_MODE_WRITE);
	spuid = (*data)++;
	release(data);

	srand ( spuid );

	acquireBarrier(START_BARRIER);


	struct timespec time;
	time.tv_sec = 0;

	if (spuid % 2 == 0)
	{
		//Writer
		while(1) {
			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			data = create(CONTENTION_ITEM, sizeof(int), CREATE_MODE_BLOCKING);
			if (data == NULL)
				printf("NULL pointer recieved ...\n");

			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			release(data);

			count++;

			if (count % 100 == 0)
				printf("SPU %d has reached %d\n", spuid, count);

			//sleep(2);
		}
	}
	else
	{
		//Reader
		while(1) {
			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			data = acquire(CONTENTION_ITEM, &size, ACQUIRE_MODE_DELETE);
			if (data == NULL)
				printf("NULL pointer recieved ...\n");

			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			release(data);

			count++;

			if (count % 100 == 0)
				printf("SPU %d has reached %d\n", spuid, count);
		}
	}
	
	terminate();
	printf(WHERESTR "Done\n", WHEREARG);
	
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


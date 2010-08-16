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

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId) {

	int spuid;
	unsigned long size;
	int* data;
	int count = 0;

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	printf(WHERESTR "Initialized\n", WHEREARG);

	data = dsmcbe_acquire(PROCESS_COUNTER, &size, ACQUIRE_MODE_WRITE);
	spuid = (*data)++;
	dsmcbe_release(data);

	srand ( spuid );

	dsmcbe_acquireBarrier(START_BARRIER);


	struct timespec time;
	time.tv_sec = 0;

	if (spuid % 2 == 0)
	{
		//Writer
		while(1) {
			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			data = dsmcbe_create(CONTENTION_ITEM, sizeof(int));
			if (data == NULL)
				printf("NULL pointer recieved ...\n");

			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			dsmcbe_release(data);

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

			//data = dsmcbe_acquire(CONTENTION_ITEM, &size, ACQUIRE_MODE_DELETE);
			printf("Sample is using unsupported feature\n");

			if (data == NULL)
				printf("NULL pointer recieved ...\n");

			time.tv_nsec = rand() / 100;
			nanosleep(&time, NULL);

			dsmcbe_release(data);

			count++;

			if (count % 100 == 0)
				printf("SPU %d has reached %d\n", spuid, count);
		}
	}
	
	printf(WHERESTR "Done\n", WHEREARG);
	
	return 0;
}


#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <stdio.h>
#include "../guids.h"
#include <debug.h>
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>
#include "guids.h"
#include "StopWatch.h"

#ifndef MAX
#define MAX(x,y) ((x > y) ? (x) : (y))
#endif


int main(int argc, char **argv)
{
	printf("Compile time - %s\n", __TIME__);

	void* data;

    size_t spu_threadcount;
    int machineid;
    int workCount;
    int dataSize;

    size_t i;
    char buf[256];

    machineid = 0;
    spu_threadcount = 6;

	if (argc == 4) {
		spu_threadcount = atoi(argv[1]);
		workCount = atoi(argv[2]);
		dataSize = atoi(argv[3]);
	} else {
		printf("Wrong number of arguments \"./CommToCompPPU spu-threads work-count data-size\"\n");
		return -1;
	}

	if (spu_threadcount < 1)
	{
		perror("There must be at least one SPE\n");
		exit(1);
	}



    pthread_t* spu_threads = dsmcbe_simpleInitialize(0, NULL, spu_threadcount);

	CSP_SAFE_CALL("create setup channel", dsmcbe_csp_channel_create(SETUP_CHANNEL, 0, CSP_CHANNEL_TYPE_ONE2ANY));
	CSP_SAFE_CALL("create delta channel", dsmcbe_csp_channel_create(DELTA_CHANNEL, 0, CSP_CHANNEL_TYPE_ONE2ONE));

	for(i = 0; i < spu_threadcount; i++)
	{
		CSP_SAFE_CALL("create setup item", dsmcbe_csp_item_create(&data, sizeof(unsigned int) * 4));
		((unsigned int*)data)[0] = i;
		((unsigned int*)data)[1] = spu_threadcount;
		((unsigned int*)data)[2] = workCount / spu_threadcount;
		((unsigned int*)data)[3] = dataSize;
		CSP_SAFE_CALL("write setup", dsmcbe_csp_channel_write(SETUP_CHANNEL, data));
	}

	sw_init();
	sw_start();

	int counter = 0;
	int repcount = 0;

	double totalseconds = 0;

#define ROUNDS 10

	while(1)
	{
		size_t size;
		CSP_SAFE_CALL("read delta", dsmcbe_csp_channel_read(DELTA_CHANNEL, &size, &data));
		CSP_SAFE_CALL("free delta", dsmcbe_csp_item_free(data));

		if (counter >= REPETITIONS)
		{
			sw_stop();
			sw_timeString(buf);
			totalseconds += sw_getSecondsElapsed();
			printf("CommToComp: %f usec, datasize: %d, work-ops: %d (%d each), %d ops on %d processes's in %s.\n", (sw_getSecondsElapsed() / counter / spu_threadcount) * 1000000, size, workCount, workCount / spu_threadcount, counter, spu_threadcount, buf);
			counter = 0;
			sw_start();
			repcount ++;
			if (repcount >= ROUNDS)
			{
				CSP_SAFE_CALL("poison delta", dsmcbe_csp_channel_poison(DELTA_CHANNEL));
				printf("CommToComp avg: %f usec,\nTotal Seconds are: %lf\n", (totalseconds / (ROUNDS * REPETITIONS) / spu_threadcount) * 1000000, totalseconds);
				break;
			}
		}
		else
		{
			//printf("Run %d completed\n", counter);
			counter++;
		}
	}

	printf("PPU is waiting for threads to complete\n");

	/*for(i = 0; i < spu_threadcount; i++)
		pthread_join(spu_threads[i], NULL);*/

	printf("PPU is terminating\n");

	dsmcbe_terminate();
	return 0;
}

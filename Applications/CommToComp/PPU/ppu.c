#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <stdio.h>
#include "guids.h"
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
    int spu_fibers;

    size_t i;
    char buf[256];

    machineid = 0;
    spu_threadcount = 6;

	if (argc == 5) {
		spu_threadcount = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
		workCount = atoi(argv[3]);
		dataSize = atoi(argv[4]);
	} else {
		printf("Wrong number of arguments \"./CommToCompPPU spu-threads spu-fibers work-count data-size\"\n");
		return -1;
	}

	if (spu_threadcount * spu_fibers < 1)
	{
		perror("There must be at least one SPE\n");
		exit(1);
	}

	printf(WHERESTR "Using %u hardware threads\n", WHEREARG, PPE_HARDWARE_THREADS);
	dsmcbe_SetHardwareThreads(PPE_HARDWARE_THREADS);

	pthread_t* spu_threads = dsmcbe_simpleInitialize(0, NULL, spu_threadcount, spu_fibers);

	CSP_SAFE_CALL("create setup channel", dsmcbe_csp_channel_create(SETUP_CHANNEL, 0, CSP_CHANNEL_TYPE_ONE2ANY));
	CSP_SAFE_CALL("create delta channel", dsmcbe_csp_channel_create(DELTA_CHANNEL, 0, CSP_CHANNEL_TYPE_ONE2ONE_SIMPLE));

	unsigned int workers = (spu_threadcount * spu_fibers);

#ifdef SINGLE_PACKAGE
	printf(WHERESTR "Using COMMSTIME setup, generators: ", WHEREARG);

	unsigned int ids[] = SINGLE_PACKAGE;
	int idcounter = 0;

	while(ids[idcounter] != UINT_MAX)
		printf("%u ", ids[idcounter++]);

	printf("\n");
#else
	printf(WHERESTR "Using COMM-2-COMP setup\n", WHEREARG);
#endif

#ifdef BUFFERED_STARTUP_CHANNEL
	printf(WHERESTR "Using BUFFERED start channel\n", WHEREARG);
#else
	printf(WHERESTR "Using NON-BUFFERED start channel\n", WHEREARG);
#endif

#ifdef SMART_ID_ASSIGNMENT
	printf(WHERESTR "Using STRUCTURED ID assignment\n", WHEREARG);

	for(i = 0; i < spu_threadcount; i++)
	{
		CSP_SAFE_CALL("create setup item", dsmcbe_csp_item_create(&data, sizeof(unsigned int) * 5));
		((unsigned int*)data)[0] = i * spu_fibers;

#else
	printf(WHERESTR "Using RANDOM ID assignment\n", WHEREARG);
	for(i = 0; i < workers; i++)
	{
		CSP_SAFE_CALL("create setup item", dsmcbe_csp_item_create(&data, sizeof(unsigned int) * 5));
		((unsigned int*)data)[0] = i;
#endif
		((unsigned int*)data)[1] = workers;
		((unsigned int*)data)[2] = workCount / workers;
		((unsigned int*)data)[3] = dataSize;
		CSP_SAFE_CALL("write setup", dsmcbe_csp_channel_write(SETUP_CHANNEL, data));
	}

	sw_init();
	sw_start();

	int count = 0;

	double totalseconds = 0;

	while(1)
	{
		size_t size;
		CSP_SAFE_CALL("read delta", dsmcbe_csp_channel_read(DELTA_CHANNEL, &size, &data));
		CSP_SAFE_CALL("free delta", dsmcbe_csp_item_free(data));

		sw_stop();
		sw_timeString(buf);
		totalseconds += sw_getSecondsElapsed();
		printf("CommToComp: %f usec, datasize: %d, work-ops: %d (%d each), %d ops on %d:%d processes's in %s.\n", (sw_getSecondsElapsed() / SPE_REPETITIONS / workers) * 1000000, size, workCount, workCount / workers, SPE_REPETITIONS, spu_threadcount, spu_fibers, buf);
		sw_start();

		count++;
		if (count >= ROUND_COUNT)
		{
			for(i = 0; i < workers; i++)
			{
				//printf(WHERESTR "Poison channel %d\n", WHEREARG, RING_CHANNEL_BASE + i);
				CSP_SAFE_CALL("poison a comm chan", dsmcbe_csp_channel_poison(RING_CHANNEL_BASE + i));
			}

			//printf(WHERESTR "Poison channel %d\n", WHEREARG, DELTA_CHANNEL);
			CSP_SAFE_CALL("poison delta", dsmcbe_csp_channel_poison(DELTA_CHANNEL));
			printf("CommToComp avg: %f usec,\nTotal Seconds are: %lf\n", (totalseconds / (ROUND_COUNT * SPE_REPETITIONS) / workers) * 1000000, totalseconds);
			break;
		}
	}

	//printf("PPU is waiting for threads to complete\n");

	for(i = 0; i < spu_threadcount; i++)
	{
		//printf(WHERESTR "PPE is waiting for spe %d to finish\n", WHEREARG, i);
		pthread_join(spu_threads[i], NULL);
	}

	printf("PPU is terminating\n");

	dsmcbe_terminate();
	return 0;
}

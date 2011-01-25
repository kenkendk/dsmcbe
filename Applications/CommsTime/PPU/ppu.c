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

#define CSP_CONSUME(name, channel) \
	{ \
		void* __csp_tmp; \
		CSP_SAFE_CALL("reading " name, dsmcbe_csp_channel_read(channel, &size, &__csp_tmp)); \
		CSP_SAFE_CALL("free'ing " name, dsmcbe_csp_item_free(__csp_tmp)); \
	};

//HACK:, use the same code for all CSP processes, simply import the C files into this file
#define COMPILE_FOR_PPE
#include "../SPU/csp_commons.c"
#include "../SPU/csp_main.c"

//This is the wrapper entry function with a pthread signature
void* csp_thread_entry(void* dummy)
{
	UNUSED(dummy);
	return (void*)dsmcbe_main(0,0,0);
}


int main(int argc, char **argv)
{
	printf("Compile time - %s\n", __TIME__);

    size_t spu_threadcount;
    size_t ppu_threadcount;
    unsigned int spu_fibercount;

    int machineid;
    char* file;
    size_t i;
    char buf[256];
    int hw_threads;

    machineid = 0;
    spu_threadcount = 6;
    ppu_threadcount = 6;
    file = NULL;
    hw_threads = 1;

	if(argc == 7) {
		machineid = atoi(argv[1]);
		file = argv[2];
		ppu_threadcount = atoi(argv[3]);
		spu_threadcount = atoi(argv[4]);
		spu_fibercount =  atoi(argv[5]);
		hw_threads = atoi(argv[6]);
	} else if(argc == 6) {
		machineid = atoi(argv[1]);
		file = argv[2];
		ppu_threadcount = atoi(argv[3]);
		spu_threadcount = atoi(argv[4]);
		spu_fibercount =  atoi(argv[5]);
	} else if (argc == 5) {
		machineid = 0;
		file = NULL;
		ppu_threadcount = atoi(argv[1]);
		spu_threadcount = atoi(argv[2]);
		spu_fibercount =  atoi(argv[3]);
		hw_threads = atoi(argv[4]);
	} else if (argc == 4) {
		machineid = 0;
		file = NULL;
		ppu_threadcount = atoi(argv[1]);
		spu_threadcount = atoi(argv[2]);
		spu_fibercount =  atoi(argv[3]);
	} else {
		printf("Wrong number of arguments \"./PPU ppu-threads spu-threads spu-fibers\"\n");
		printf("                       or \"./PPU ppu-threads spu-threads spu-fibers ppu-hw-threads\"\n");
		printf("                       or \"./PPU id network-file ppu-threads spu-threads spu-fibers\"\n");
		printf("                       or \"./PPU id network-file ppu-threads spu-threads spu-fibers ppu-hw-threads\"\n");
		return -1;
	}

	if (spu_threadcount > 0 && spu_fibercount <= 0)
	{
		perror("There must be at least one fiber\n");
		exit(1);
	}

	if ((spu_threadcount * spu_fibercount) + ppu_threadcount <= 1)
	{
		perror("There must be at least two processes\n");
		exit(1);
	}

	pthread_t* spu_threads = dsmcbe_simpleInitialize(machineid, file, spu_threadcount, spu_fibercount);
	pthread_t* ppu_threads = (pthread_t*)malloc(sizeof(pthread_t) * ppu_threadcount);

	for(i = 0; i < ppu_threadcount; i++)
		pthread_create(&ppu_threads[i], NULL, csp_thread_entry, NULL);

	unsigned int actual_machines = MAX(1, dsmcbe_MachineCount());
	unsigned int actual_processes = (actual_machines * spu_threadcount * spu_fibercount) + (actual_machines * ppu_threadcount);

	if (machineid == 0)
	{
		CSP_SAFE_CALL("create process channel", dsmcbe_csp_channel_create(PROCESS_COUNTER_GUID, 0, CSP_CHANNEL_TYPE_ONE2ANY));
		CSP_SAFE_CALL("create delta channel", dsmcbe_csp_channel_create(DELTA_CHANNEL, 0, CSP_CHANNEL_TYPE_ONE2ONE));
		CSP_SAFE_CALL("create complete channel", dsmcbe_csp_channel_create(COMPLETION_LOCK, 0, CSP_CHANNEL_TYPE_ONE2ANY));

		//Test the skip channel feature
		GUID testchans[] = {DELTA_CHANNEL, UNUSED_CHANNEL_1, CSP_SKIP_GUARD};
		GUID selected_chan = 777;
		void* test_data;

		CSP_SAFE_CALL("test skip channel", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 3, &selected_chan, NULL, &test_data));
		if (selected_chan != CSP_SKIP_GUARD)
			printf("**** ERROR: SKIP_CHANNEL is broken :(\n");


		unsigned int* tmp;
		CSP_SAFE_CALL("create initial pid", dsmcbe_csp_item_create((void**)&tmp, sizeof(unsigned int) * 2));

		//Setup the control information
		tmp[0] = 0;
		tmp[1] = actual_processes;

		CSP_SAFE_CALL("release initial pid", dsmcbe_csp_channel_write(PROCESS_COUNTER_GUID, tmp));

		CSP_SAFE_CALL("test skip channel", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
		if (selected_chan != DELTA_CHANNEL)
			printf("**** ERROR: ALTING is broken :(, selected_chan=%d\n", selected_chan);

		CSP_SAFE_CALL("free test read", dsmcbe_csp_item_free(test_data));

		sw_init();
		sw_start();

		int counter = 0;
		int repcount = 0;

		double totalseconds = 0;

#define ROUNDS 10

		while(1)
		{
			size_t size;
			CSP_CONSUME("delta", DELTA_CHANNEL);

			if (counter >= REPETITIONS)
			{
				sw_stop();
				sw_timeString(buf);
				totalseconds += sw_getSecondsElapsed();
				printf("CommsTime: %f usec, datasize: %d, %d ops on %d processes's in %s.\n", (sw_getSecondsElapsed() / counter / actual_processes) * 1000000, size, counter, actual_processes, buf);
				counter = 0;
				sw_start();
				repcount ++;
				if (repcount >= ROUNDS)
				{
					CSP_SAFE_CALL("poison delta", dsmcbe_csp_channel_poison(DELTA_CHANNEL));
					CSP_SAFE_CALL("poison completion", dsmcbe_csp_channel_poison(COMPLETION_LOCK));

					printf("CommsTime avg: %f usec\n", (totalseconds / (ROUNDS * REPETITIONS) / actual_processes) * 1000000);
					break;
				}
			}
			else
			{
				//printf("Run %d completed\n", counter);
				counter++;
			}
		}
    }
	else
	{
		printf("PPU is waiting for shutdown\n");

		//Just wait for completion
		void* tmp;
		if (dsmcbe_csp_channel_read(COMPLETION_LOCK, NULL, &tmp) != CSP_CALL_POISON)
			printf("BAD complete response!\n");
	}

	printf("PPU is waiting for threads to complete\n");

	for(i = 0; i < ppu_threadcount; i++)
		pthread_join(ppu_threads[i], NULL);

	for(i = 0; i < spu_threadcount; i++)
		pthread_join(spu_threads[i], NULL);

	printf("PPU is terminating\n");

	dsmcbe_terminate();
	return 0;
}

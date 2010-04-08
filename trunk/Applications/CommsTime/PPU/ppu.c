#include <dsmcbe_ppu.h>
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

	unsigned long size;
    int spu_threads;
    int machineid;
    char* file;
    int i;
    char buf[256];

    machineid = 0;
    spu_threads = 6;
    file = NULL;

	if(argc == 4) {
		machineid = atoi(argv[1]);
		file = argv[2];
		spu_threads = atoi(argv[3]);
	} else if (argc == 2) {
		machineid = 0;
		file = NULL;
		spu_threads = atoi(argv[1]);
	} else {
		printf("Wrong number of arguments \"./PPU spu-threads\"\n");
		printf("                       or \"./PPU id network-file spu-threads\"\n");
		return -1;
	}

	if (spu_threads <= 1)
	{
		perror("There must be at least two SPU process\n");
		exit(1);
	}

    pthread_t* threads = simpleInitialize(machineid, file, spu_threads);

    if (machineid == 0)
    {
		//Create the ID counter object
		int* tmp = create(PROCESS_COUNTER_GUID, sizeof(int), CREATE_MODE_NONBLOCKING);
		*tmp = 0;
		release(tmp);

		//Create the shared barrier object
		create(BARRIER_GUID, MAX(1, DSMCBE_MachineCount()) * spu_threads, CREATE_MODE_BARRIER);
    }

    sw_init();
    release(acquire(DELTA_CHANNEL, &size, ACQUIRE_MODE_DELETE));

	sw_start();

	int counter = 0;

    while(1)
    {
        release(acquire(DELTA_CHANNEL, &size, ACQUIRE_MODE_DELETE));
    	if (counter >= REPETITIONS)
    	{
    		sw_stop();
    		sw_timeString(buf);
    		printf("CommsTime: %f usec, %d ops on %d SPU's in %s.\n", (sw_getSecondsElapsed() / counter / (MAX(1, DSMCBE_MachineCount()) * spu_threads)) * 1000000, counter, MAX(1, DSMCBE_MachineCount()) * spu_threads, buf);
    		counter = 0;
    		sw_start();
    	}
    	else
    	{
    		printf("Run %d completed\n", counter);
    		counter++;
    	}
    }

	printf("PPU is waiting for shutdown\n");

	//Just wait for completion
    release(acquire(COMPLETION_LOCK, &size, ACQUIRE_MODE_READ));

	printf("PPU is waiting for threads to complete\n");

	for(i = 0; i < spu_threads; i++)
	{
	    //printf(WHERESTR "waiting for SPU %i\n", WHEREARG, i);
		pthread_join(threads[i], NULL);
	}

	printf("PPU is terminating\n");

	terminate();
	return 0;
}

#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <stdio.h>
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>
#include "guids.h"

#ifndef MAX
#define MAX(x,y) ((x > y) ? (x) : (y))
#endif

int main(int argc, char **argv)
{
	printf("Compile time - %s\n", __TIME__);
	printf("Ready\n");

	unsigned long size;
    int spu_threads;
    int machineid;
    char* file;
    int i;

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

	printf("Initializing\n");
    pthread_t* threads = dsmcbe_simpleInitialize(machineid, file, spu_threads);
	printf("Initialized\n");

    if (machineid == 0)
    {
		int* tmp;

		printf("Creating process counter\n");

		//Create the ID counter object
		tmp = dsmcbe_create(PROCESS_COUNTER_GUID, sizeof(int));
		*tmp = 0;
		dsmcbe_release(tmp);

		printf("Creating barrier object\n");

		//Create the shared barrier object
		dsmcbe_createBarrier(BARRIER_GUID, MAX(1, dsmcbe_MachineCount()) * spu_threads);
    }

	unsigned long* fibonacci;

	sleep(2);
	printf("PPU is waiting for first fibonacci\n");

	while(1)
	{
		CSP_SAFE_CALL("read fib", dsmcbe_csp_channel_read(CHANNEL_OUT, NULL, (void**)&fibonacci));
		printf("Next fibonacci is: %lu\n", (unsigned long)(*fibonacci));
		CSP_SAFE_CALL("free fib", dsmcbe_csp_item_free(fibonacci));
		sleep(1);
	}

	printf("PPU is waiting for shutdown\n");

	//Just wait for completion
	dsmcbe_release(dsmcbe_acquire(COMPLETION_LOCK, &size, ACQUIRE_MODE_READ));

	printf("PPU is waiting for threads to complete\n");

	for(i = 0; i < spu_threads; i++)
	{
	    //printf(WHERESTR "waiting for SPU %i\n", WHEREARG, i);
		pthread_join(threads[i], NULL);
	}

	printf("PPU is terminating\n");

	dsmcbe_terminate();
	return 0;
}

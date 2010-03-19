#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include "../PPU/guids.h"
#include <time.h>

//We need the counter on the heap so the threads share it
//static int counter = 0;

void TEST();

int main(int argc, char** argv) {
	
	printf("SPU is initializing\n");
	initialize();

	unsigned long size;
	int pid;
	int processcount;
	time_t begintime, endtime;
	double elapsedSeconds;
	int* tmp;
	int counter = 0;

	printf("SPU is initialized, waiting for process counter\n");

	//Basic setup, get a unique id for each participating process
	tmp = acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	pid = (*tmp)++;
	release(tmp);

	printf("SPU %d is waiting for barrier\n", pid);

	acquireBarrier(BARRIER_GUID);

	//Get the number of participating processes
	tmp = acquire(PROCESS_COUNTER_GUID, &size, ACQUIRE_MODE_WRITE);
	processcount = *tmp;
	release(tmp);

	acquireBarrier(BARRIER_GUID);

	printf("SPU %d is now running\n", pid);

	//We now start the timings
	if (pid == 0)
		time(&begintime);

	GUID readerChanel = CHANNEL_START_GUID + (pid % processcount);
	GUID writerChanel = CHANNEL_START_GUID + ((pid + 1) % processcount);

	printf("SPU %d is reading on %d and writing on %d\n", pid, readerChanel, writerChanel);

	//The initial processor starts the sequence
	if (pid == 0)
	{
		tmp = create(writerChanel, sizeof(unsigned int), CREATE_MODE_NONBLOCKING);
		*tmp = 0;
		release(tmp);
	}

	//Now repeat until we get the desired value
	while(counter < REPETITIONS)
	{
		if (pid == 0 && counter % 1000 == 0)
			printf("Counter on %d is currently: %d of %d\n", pid, counter, REPETITIONS);

		//printf("%d reading %d\n", pid, readerChanel);
		tmp = acquire(readerChanel, &size, ACQUIRE_MODE_DELETE);

		//printf("%d got pointer %d for id %d\n", pid, (unsigned int)tmp, readerChanel);
		if (pid == 0)
			(*tmp)++;

		counter = *tmp;
		release(tmp);

		//sleep(5);
		//printf("%d writing %d\n", pid, writerChanel);
		tmp = create(writerChanel, sizeof(unsigned int), CREATE_MODE_NONBLOCKING);
		//printf("%d got pointer %d for id %d\n", pid, (unsigned int)tmp, writerChanel);
		*tmp = counter;
		release(tmp);
	}
	
	if (pid == 0)
	{
		time(&endtime);
		elapsedSeconds = difftime (endtime, begintime);
		printf("Elapsed time for %d iterations is %.2lf seconds, CommsTime score is: %.2lf.\n", counter, elapsedSeconds, elapsedSeconds / counter);
		release(create(COMPLETION_LOCK, sizeof(int), CREATE_MODE_NONBLOCKING));
	}

	printf("SPU %d is terminating\n", pid);

	terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


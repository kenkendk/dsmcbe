#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <dsmcbe_ppu.h>
#include <unistd.h>

#include "guids.h"

#define MAX(x,y) ((x) < (y) ? (y) : (x))

int main(int argc, char* argv[])
{
	int SPU_THREADS;
	unsigned int PPEid;
	char* file;
	int i;
	unsigned long size;
	
	if (argc == 2)
	{
		SPU_THREADS = atoi(argv[1]);
		PPEid = 0;
		file = NULL; 	
	}
	else if (argc == 4)
	{
		SPU_THREADS = atoi(argv[1]);
		PPEid = atoi(argv[2]);
		file = argv[3]; 	
	}
	else
		printf("Wrong number of inputs\n");
		
	pthread_t* threads = simpleInitialize(PPEid, file, SPU_THREADS);

	if(PPEid == 0)
	{
		unsigned int* count = create(SPU_ID, sizeof(unsigned int) * 2, CREATE_MODE_NONBLOCKING);
		count[0] = 0;
		count[1] = SPU_THREADS * MAX(DSMCBE_MachineCount(), 1);
		release(count);
		
		create(BARRIER_LOCK, SPU_THREADS * MAX(DSMCBE_MachineCount(), 1), CREATE_MODE_BARRIER);
		
		count = create(BARRIER_ITEM, sizeof(unsigned int) * 2, CREATE_MODE_NONBLOCKING);
		count[0] = count[1] = 0;
		release(count);
		
	}
	else
	{
		release(acquire(MASTER_COMPLETION_LOCK, &size, ACQUIRE_MODE_READ));
	}
	
	for(i = 0; i < SPU_THREADS; i++)
		pthread_join(threads[i], NULL);
	
	release(create(MASTER_COMPLETION_LOCK, 1, CREATE_MODE_NONBLOCKING));
	
	sleep(1);
	
		
	return 0;
}

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
		unsigned int* count = create(COUNT, sizeof(unsigned int));
		*count = SPU_THREADS * MAX(DSMCBE_MachineCount(), 1);
		release(count);
	}
	
	for(i = 0; i < SPU_THREADS; i++)
		pthread_join(threads[i], NULL);
	
	sleep(1);
		
	return 0;
}

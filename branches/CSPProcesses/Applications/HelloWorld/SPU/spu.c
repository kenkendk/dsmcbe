#include <stdio.h>
#include <dsmcbe_spu.h>

#include "../PPU/guids.h"

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	unsigned long size;
	printf("SPE %llu, thread %u on machine %u is saying Hello World\n", speid, threadId, machineId);
		
	unsigned int* count = dsmcbe_acquire(COUNT, &size, ACQUIRE_MODE_WRITE);
	*count = *count - 1;
	printf("SPE %llu saying COUNT is %u\n", speid, *count);
	dsmcbe_release(count);
	
	while(1)
	{
		count = dsmcbe_acquire(COUNT, &size, ACQUIRE_MODE_READ);
		if (*count == 0)
		{
			dsmcbe_release(count);
			break;			
		}
		dsmcbe_release(count);
	}
	
	printf("SPE %llu saying Goodbye\n", speid);

	return 0;
}

#include <stdio.h>
#include <dsmcbe_spu.h>

#include "../PPU/guids.h"

int main(unsigned long long speid, unsigned long long argp, unsigned long long envp)
{
	dsmcbe_initialize();
	unsigned long size;
	printf("SPE %llu saying Hallo World\n", speid);
		
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
	dsmcbe_terminate();

	//Remove compiler warning
	argp = 0;
	envp = 0;

	return 0;
}

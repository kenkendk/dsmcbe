#include <stdio.h>
#include <dsmcbe_spu.h>

#include "../PPU/guids.h"

int main(unsigned long long speid, unsigned long long argp, unsigned long long envp)
{
	initialize();
	unsigned long size;
	printf("SPE %llu saying Hallo World\n", speid);
		
	unsigned int* count = acquire(COUNT, &size, ACQUIRE_MODE_WRITE);	
	*count = *count - 1;
	printf("SPE %llu saying COUNT is %u\n", speid, *count);
	release(count);
	
	while(1)
	{
		count = acquire(COUNT, &size, ACQUIRE_MODE_READ);
		if (*count == 0)
		{
			release(count);
			break;			
		}
		release(count);
	}
	
	printf("SPE %llu saying Goodbye\n", speid);
	terminate();
	return 0;
}

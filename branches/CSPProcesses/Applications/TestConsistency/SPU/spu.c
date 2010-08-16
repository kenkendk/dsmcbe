#include <stdlib.h>
#include <spu_mfcio.h>
#include <stdio.h>
#include <math.h>
#include <libmisc.h>
#include <dsmcbe_spu.h>
#include <unistd.h>
#include "../PPU/guids.h"

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	dsmcbe_initialize();
	unsigned long size;
	printf("SPE %llu, thread %u on machine %u is saying Hello World\n", speid, threadId, machineId);
	unsigned int spu_no;
	unsigned int spu_count;
	unsigned int it = 0;

	unsigned int* count = dsmcbe_acquire(SPU_ID, &size, ACQUIRE_MODE_WRITE);
	spu_no = count[0];	
	spu_count = count[1];
	count[0]++;
	printf("SPE %llu is number %u\n", speid, spu_no);
	dsmcbe_release(count);

	while(it < 4000000)
	{	
		dsmcbe_acquireBarrier(BARRIER_LOCK);
	
		count = dsmcbe_acquire(BARRIER_ITEM, &size, ACQUIRE_MODE_WRITE);
		if(count[0]++ == spu_count - 1)
		{
			count[0] = 0;
			count[1]++;
		}
		dsmcbe_release(count);
		
		dsmcbe_acquireBarrier(BARRIER_LOCK);
		
		count = dsmcbe_acquire(BARRIER_ITEM, &size, ACQUIRE_MODE_READ);
		while(count[0] != 0)
		{
			dsmcbe_release(count);
			printf("Bad from %d!\n", spu_no);
			sleep(1);
			count = dsmcbe_acquire(BARRIER_ITEM, &size, ACQUIRE_MODE_READ);
		}
		
		it = count[1];
		dsmcbe_release(count);
		
		if (spu_no == 0 && it % 100 == 0)
			printf("Progress: %d\n", it);
	}
	
	printf("SPE %llu saying Goodbye\n", speid);
	dsmcbe_terminate();

	return 0;
}

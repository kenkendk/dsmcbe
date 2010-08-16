#include <dsmcbe_spu.h>
#include <debug.h>
#include "../PPU/Shared.h"
#include <libmisc.h>
#include <spu_mfcio.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <malloc_align.h>

#ifdef DSM_MODE_SINGLE
#define DSM_MODE
#endif

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

#ifdef MBOX_MODE
	while(1)
	{
		unsigned int x = spu_read_in_mbox();
		x++;
		spu_write_out_mbox(x);
	}
#else /*MBOX_MODE*/
#ifdef DSM_MODE

		unsigned int prev = PROBLEM_SIZE - 1;
		
		while(prev < PROBLEM_SIZE)
		{
			unsigned int* data = dsmcbe_acquire(OBJ_1, NULL, ACQUIRE_MODE_WRITE);
			if (*data != prev)
			{
				(*data)++;
				prev = *data;
				//printf("%d\n", *data);
			}
			dsmcbe_release(data);
		}
		
		dsmcbe_acquireBarrier(OBJ_BARRIER);

#else /*DSM_MODE*/
	unsigned int* value = _malloc_align(DATA_SIZE, 7);

	while(1)
	{	
		unsigned int ea = spu_read_in_mbox();
		//printf("Spu has EA: %d, and LS: %d\n", ea, (unsigned int)value);
		mfc_get(value, ea, DATA_SIZE, 0, 0, 0);
	
		unsigned int tag_mask = (1 << 0);
		mfc_write_tag_mask(tag_mask);
		mfc_read_tag_status_any();
		
		(*value)++;
		
		mfc_put(value, ea, DATA_SIZE, 0, 0, 0);
		mfc_write_tag_mask(tag_mask);
		mfc_read_tag_status_any();
		
		spu_write_out_mbox(1);
	}
#endif /*DSM_MODE*/		
#endif /*MBOX_MODE*/
	
	
	return 0;
}


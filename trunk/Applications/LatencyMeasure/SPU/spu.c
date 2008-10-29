#include "dsmcbe_spu.h"
#include "common/debug.h"
#include "../PPU/Shared.h"
#include <libmisc.h>
#include <spu_mfcio.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <malloc_align.h>

int main(long long id)
{
#ifdef MBOX_MODE
	while(1)
	{
		unsigned int x = spu_read_in_mbox();
		x++;
		spu_write_out_mbox(x);
	}
#else
	unsigned int* value = _malloc_align(DATA_SIZE, 7);

	while(1)
	{		
		unsigned int ea = spu_read_in_mbox();
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
		
#endif
	
	
	 
	return 0;
}


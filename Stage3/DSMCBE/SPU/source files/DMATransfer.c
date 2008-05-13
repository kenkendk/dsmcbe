#include "../header files/DMATransfer.h"
#include "../../common/debug.h"

struct DMA_LIST_ELEMENT {
	union {
		unsigned int all32;
		struct {
			unsigned int stall : 1;
			unsigned int reserved : 15;
			unsigned int nbytes : 16;
		} bits;
	} size;
	unsigned int ea_low;
};

static struct DMA_LIST_ELEMENT list[16] __attribute__ ((aligned (16)));

int IsDMATransferGroupCompleted(int groupid)
{
	unsigned int tag_mask = (1 << groupid);
	mfc_write_tag_mask(tag_mask);
	return mfc_read_tag_status_immediate();
}

void WaitForDMATransferByGroup(int groupid)
{
	unsigned int tag_mask = (1 << groupid);
	mfc_write_tag_mask(tag_mask);
	mfc_read_tag_status_any();
}

void StartDMAWriteTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid)
{
	if (((unsigned int)buffer % 128) != 0)
		printf(WHERESTR "Warning detected non-aligned DMA transfer\n", WHEREARG);

	if ((ea % 128) != 0)
		printf(WHERESTR "Error, EA was non-aligned in DMA transfer\n", WHEREARG);

	if (size < 16384 ) {
		//printf(WHERESTR "Single DMA write-transfer, source: %d, ea: %d, size: %d, tag: %d\n", WHEREARG, (int)buffer, ea, size, groupid);
		mfc_put(buffer, ea, size, groupid, 0, 0);
	} else {
		//printf(WHERESTR "DMA list write-transfer, source: %d, ea: %d, size: %d, tag: %d\n", WHEREARG, (int)buffer, ea, size, groupid);
		unsigned int i = 0;
		unsigned int listsize;	
		
		if (!size)
			return;
		
		while (size > 0) {
			unsigned int sz;
			sz = (size < 16384) ? size : 16384;
			list[i].size.all32 = sz;
			list[i].ea_low = ea;
			size -= sz;
			ea += sz;
			i++;
		}
		
		listsize = i * sizeof(struct DMA_LIST_ELEMENT);

		//printf(WHERESTR "List DMA args %d, %d, %d, %d, %d\n", WHEREARG, buffer, list[0].ea_low, list, listsize, groupid);
		mfc_putl(buffer, list[0].ea_low, list, listsize, groupid, 0, 0);		
		//printf(WHERESTR "List DMA with %d segments\n", WHEREARG, i);
	}
	
}

void StartDMAReadTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid)
{
	if (((unsigned int)buffer % 128) != 0)
		printf(WHERESTR "Warning detected non-aligned DMA transfer\n", WHEREARG);

	if ((ea % 128) != 0)
		printf(WHERESTR "Error, EA was non-aligned in DMA transfer\n", WHEREARG);
	
	if (size < 16384 ) {
		//printf(WHERESTR "Single DMA read-transfer, target: %d, ea: %d, size: %d, tag: %d\n", WHEREARG, buffer, ea, size, groupid);
		mfc_get(buffer, ea, size, groupid, 0, 0);
	} else {
		//printf(WHERESTR "DMA list read-transfer, target: %d, ea: %d, size: %d, tag: %d\n", WHEREARG, buffer, ea, size, groupid);
		unsigned int i = 0;
		unsigned int listsize;	
		
		if (!size)
			return;
		
		while (size > 0) {
			unsigned int sz;
			sz = (size < 16384) ? size : 16384;
			list[i].size.all32 = sz;
			list[i].ea_low = ea;
			size -= sz;
			ea += sz;
			i++;
		}
		
		listsize = i * sizeof(struct DMA_LIST_ELEMENT);
		mfc_getl(buffer, list[0].ea_low, list, listsize, groupid, 0, 0);		
	}
}

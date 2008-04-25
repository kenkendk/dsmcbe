#include "DMATransfer.h"
#include "../Common/debug.h"

void WaitForDMATransferByGroup(int groupid)
{
	unsigned int tag_mask = (1 << groupid);
	mfc_write_tag_mask(tag_mask);
	mfc_read_tag_status_any();
}

inline int GetDMAGroupID(void* b0, void* b1, void* current)
{
	return current == b0 ? 1 : 2;
}

void StartDMAReadTransferOfNext(void* b0, void* b1, void* current, unsigned int* ea, unsigned int* size, unsigned int *last_size, unsigned int buffersize)
{
	unsigned char* target = current == b0 ? b1 : b0;
	
	if (((unsigned int)target % 128) != 0)
		printf(WHERESTR "Warning detected non-aligned DMA transfer\n", WHEREARG);

	if (((*ea) % 128) != 0)
		printf(WHERESTR "Error, EA was non-aligned in DMA transfer\n", WHEREARG);
		
	int groupid = GetDMAGroupID(b0, b1, current);

	(*last_size) = (*size) > buffersize ? buffersize : (*size);
	mfc_get(target, *ea, *last_size, groupid, 0, 0);
	(*ea) += (*last_size);
	(*size) -= (*last_size);
}

void WaitForDMATransfer(void* b0, void* b1, void* current)
{
	WaitForDMATransferByGroup(GetDMAGroupID(b0, b1, current));
}

void StartDMAWriteTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid)
{
	if (((unsigned int)buffer % 128) != 0)
		printf(WHERESTR "Warning detected non-aligned DMA transfer\n", WHEREARG);

	if ((ea % 128) != 0)
		printf(WHERESTR "Error, EA was non-aligned in DMA transfer\n", WHEREARG);

	mfc_put(buffer, ea, size, groupid, 0, 0);
}


void StartDMAReadTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid)
{
	if (((unsigned int)buffer % 128) != 0)
		printf(WHERESTR "Warning detected non-aligned DMA transfer\n", WHEREARG);

	if ((ea % 128) != 0)
		printf(WHERESTR "Error, EA was non-aligned in DMA transfer\n", WHEREARG);

	mfc_get(buffer, ea, size, groupid, 0, 0);
}

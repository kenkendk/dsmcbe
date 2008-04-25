#include "ppu.h"

void DMATest(speid_t* spe_ids, unsigned int spu_count)
{
	int i, j;

	#define DATA_SIZE (250 * 1024)
	unsigned char* datablocks[SPU_THREADS];
	
	for(i = 0; i < spu_count; i++)
		datablocks[i] = (unsigned char *)_malloc_align(DATA_SIZE, 7);
	
	for(i = 0; i < spu_count; i++)
		for(j = 0; j < DATA_SIZE; j++)
			datablocks[i][j] = i + 1;
	
	unsigned int spu_data[3] = { 0x0, 0x0, 0x0 };
	for(i = 0; i < spu_count; i++)
	{
		spu_data[0] = i + 1;
		spu_data[1] = (int)datablocks[i];
		spu_data[2] = DATA_SIZE;
		send_mailbox_message_to_spe(spe_ids[i], 3, spu_data);
	}
	
	#define MESSAGE_RESPONSES 1
	int read_count[SPU_THREADS];
	for(i=0;i<spu_count; i++)
		read_count[i] = MESSAGE_RESPONSES;

	int total_reads = spu_count * MESSAGE_RESPONSES;
	
	do
	{
		for(i=0;i<spu_count; i++)
		{
			if (read_count[i] > 0 && spe_out_mbox_status(spe_ids[i]) != 0)
			{
				unsigned int val;
				spe_out_mbox_read(spe_ids[i], &val, 1);
				printf("Read value %d from (0x%dx)\n", val, (int)spe_ids[i]);
				read_count[i]--;
				total_reads--;
			}
		}
	}
	while (total_reads > 0);		
	
	WaitForSPUCompletion(spe_ids, spu_count);	
	
	for(i=0;i<spu_count;i++) {
		_free_align(datablocks[i]);
	}
}
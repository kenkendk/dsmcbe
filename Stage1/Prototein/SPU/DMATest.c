#include "DMATransfer.h"

#define AVALIBLE_STORAGE (10 * 1024)
#define MAX_BUFFER_SIZE (AVALIBLE_STORAGE / 2)

int DMATest(unsigned long long id) {
	
	int i;
	//prof_clear();
	//prof_start();
	printf("HelloCell: (0x%llx)\n",id);
	
	unsigned int value = spu_read_in_mbox();
	unsigned int ea = spu_read_in_mbox();
	unsigned int size = spu_read_in_mbox();
		
	printf("SPU(0x%llx): %d:%d %d\n",id, ea, size, value);
	
	unsigned char* buffer0 = malloc_align(MAX_BUFFER_SIZE, 7);
	unsigned char* buffer1 = malloc_align(MAX_BUFFER_SIZE, 7);
	
	unsigned char* current_buffer = buffer0;
	int done = 0;
	unsigned int last_buffer_size = 0;
	
	printf("Starting DMA transfer 1: (0x%llx)\n",id);
	StartDMAReadTransferOfNext(buffer0, buffer1, buffer1, &ea, &size, &last_buffer_size, MAX_BUFFER_SIZE);
	if (size == 0)
		done = 1;
	else
	{
		printf("Starting DMA transfer 2: (0x%llx)\n",id);		
		StartDMAReadTransferOfNext(buffer0, buffer1, current_buffer, &ea, &size, &last_buffer_size, MAX_BUFFER_SIZE);
	}

	int bad_data = 0;		

	do
	{
		printf("Waiting for DMA Transfer completion (buffer%d):  (0x%llx)\n", GetDMAGroupID(buffer0, buffer1, current_buffer) , id);		
		WaitForDMATransfer(buffer0, buffer1, current_buffer);
		printf("DMA Transfer completed! (buffer%d):  (0x%llx)\n", GetDMAGroupID(buffer0, buffer1, current_buffer), id);		
	
		for(i = 0; i < (done == 1 ? last_buffer_size : MAX_BUFFER_SIZE); i++)
			if (current_buffer[i] != value)
			{
				printf("Bad data detected %d vs %d:  (0x%llx)\n", current_buffer[i], value, id);		
				
				bad_data = 1;
				break;
			}
	
		if (bad_data != 0)
			break;
	
		current_buffer = current_buffer == buffer0 ? buffer1 : buffer0;
		if (size > 0)
			StartDMAReadTransferOfNext(buffer0, buffer1, current_buffer, &ea, &size, &last_buffer_size, MAX_BUFFER_SIZE);
	
		if (size == 0)
			done++;
				
	} while (done < 2 && bad_data == 0);	
		
	spu_write_out_mbox(bad_data);
	
	free_align(buffer0);
	free_align(buffer1);
			
	//prof_stop();
	
	return 0;
}


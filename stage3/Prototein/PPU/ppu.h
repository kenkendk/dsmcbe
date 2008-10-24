#ifndef PPU_H_
#define PPU_H_

#include<stdlib.h>
#include<stdio.h>
#include<errno.h>

#include<libspe2.h>
#include<malloc_align.h>
#include<free_align.h>

#define SPU_THREADS 1
extern spe_program_handle_t SPU;

#define speid_t spe_context_ptr_t

void send_mailbox_message_to_spe(speid_t target, unsigned int data_size, unsigned int* data);
void WaitForSPUCompletion(pthread_t* ids, unsigned int threads);

#endif /*PPU_H_*/

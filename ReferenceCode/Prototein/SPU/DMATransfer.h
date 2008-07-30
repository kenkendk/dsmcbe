#ifndef DMATRANSFER_H_
#define DMATRANSFER_H_

#include<stdio.h>
#include<spu_mfcio.h>
#include<malloc_align.h>
#include<free_align.h>
#include<spu_intrinsics.h>
#include<libmisc.h>

#define malloc_align _malloc_align
#define free_align _free_align

inline int GetDMAGroupID(void* b0, void* b1, void* current);
void StartDMAReadTransferOfNext(void* b0, void* b1, void* current, unsigned int* ea, unsigned int* size, unsigned int *last_size, unsigned int buffersize);
void WaitForDMATransfer(void* b0, void* b1, void* current);
void StartDMAReadTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid);
void WaitForDMATransferByGroup(int groupid);
void StartDMAWriteTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid);

#endif /*DMATRANSFER_H_*/


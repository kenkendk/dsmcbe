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

void StartDMAReadTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid, unsigned int dmaComplete);
void WaitForDMATransferByGroup(int groupid);
void StartDMAWriteTransfer(void* buffer, unsigned int ea, unsigned int size, int groupid);
int IsDMATransferGroupCompleted(int groupid);

#define ALIGNED_SIZE(x) (x + ((16 - x) % 16))

#endif /*DMATRANSFER_H_*/


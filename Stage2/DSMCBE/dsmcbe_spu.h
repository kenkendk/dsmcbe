#ifndef DSMCBE_SPU_H_
#define DSMCBE_SPU_H_

#include "./dsmcbe.h"
extern void initialize();
extern void terminate();

void TerminateThread(void);
int CreateThreads(int threadCount);
int YieldThread(void);

#endif /*DSMCBE_SPU_H_*/

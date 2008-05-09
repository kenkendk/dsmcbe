#ifndef DSMCBE_SPU_THREADS_H_
#define DSMCBE_SPU_THREADS_H_

extern void TerminateThread(void);
extern int CreateThreads(int threadCount);
extern int YieldThread(void);
extern int IsThreaded(void);

#endif DSMCBE_SPU_THREADS_H_

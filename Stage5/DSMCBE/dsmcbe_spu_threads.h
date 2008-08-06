#ifndef DSMCBE_SPU_THREADS_H_
#define DSMCBE_SPU_THREADS_H_

extern void TerminateThread(void);
extern int CreateThreads(int threadCount);
extern int YieldThread(void);
extern int IsThreaded(void);

extern void thread_free(void* data);
extern void* thread_malloc(unsigned long size);
extern void thread_free_align(void* data);
extern void* thread_malloc_align(unsigned long size, unsigned int base);

#endif /*DSMCBE_SPU_THREADS_H_*/

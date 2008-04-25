#ifndef SPUTHREADS_H_
#define SPUTHREADS_H_

#include <setjmp.h>

//Stack must be multiple of 128 bits
#define STACK_SIZE ((1024 * 16) % 16)

//SP is in register 29
#define SP 29

typedef struct 
{
	int id;
	jmp_buf env;
	int stack[STACK_SIZE] __attribute__((__aligned__(16)));
} thread_struct;

void Terminate(void);
int CreateThreads(int threadCount);
int Yield(void);

#endif /*SPUTHREADS_H_*/

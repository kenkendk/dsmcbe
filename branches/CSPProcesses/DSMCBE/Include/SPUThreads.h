#ifndef SPUTHREADS_H_
#define SPUTHREADS_H_

#include <setjmp.h>

//Stack must be multiple of 128 bits
#define STACK_SIZE (1024 * 16)

//SP is in register 1
#define SP 1

//The width of a single register is 128 bits
#define REGISTER_WIDTH (128 / 8)
#define INTS_PR_REGISTER (REGISTER_WIDTH / sizeof(unsigned int))

typedef struct 
{
	int id;
	jmp_buf env;
	int stack[STACK_SIZE] __attribute__((__aligned__(16)));
} thread_struct;

#endif /*SPUTHREADS_H_*/

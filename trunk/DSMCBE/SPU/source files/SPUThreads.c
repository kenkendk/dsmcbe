#include <malloc.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include "../header files/SPUThreads.h"
#include "../../common/debug.h"
#include "../../dsmcbe_spu.h"
#include <malloc_align.h>
#include <free_align.h>
#include <string.h>

//** If you are using this file in another project (other than DSMCBE),
//** be aware that the threads cannot call malloc.
//** You can either supply your own MALLOC call, 
//** or your threads have to call thread_malloc
//** You can map MALLOC to thread_malloc in a header file

//** DO NOT map MALLOC to thread_malloc in this module! **/

//Allow non-DSMCBE programs to call malloc as well
#ifndef MALLOC
#define THREAD_MALLOC_REQUIRED
#define MALLOC(x) malloc(x)
#endif


static thread_struct* threads = NULL; //The threads
static thread_struct* current_thread = NULL; //The currently executing thread
static jmp_buf* main_env = NULL; //The main entry point
static int no_of_threads; //Keep number of threads on the heap
static int loop_counter; //Keep the loop counter on the heap

static unsigned int malloc_size;
static void* malloc_result;
static unsigned int malloc_base;

#define JMP_MALLOC 3
#define JMP_MALLOC_ALIGN 4
#define JMP_FREE 5
#define JMP_FREE_ALIGN 6

/*
	Returns the index of the given thread, or -1 if the given thread was invalid
*/
int getThreadID(thread_struct* current)
{
	if (current == NULL)
		return -1;
	else
		return current->id;
}

/*
	Returns a pointer to the next waiting thread, and NULL if no threads are waiting
*/
thread_struct* getNextWaitingThread()
{
	int threadNo;
	int i;
	thread_struct* res;

	threadNo = getThreadID(current_thread);
	if (threadNo < 0 || threads == NULL || threadNo >= no_of_threads)
		return NULL;
	else 

	//This simulates a very basic scheduler, that just takes the next processID
	for(i = 0; i < no_of_threads; i++)
	{
		res = &threads[(threadNo + i + 1) % no_of_threads];
		if (res->id != -1 && res->id != threadNo)
			return res;
	}

	return NULL;
}

/*
	Terminates the current running thread
*/
void TerminateThread(void) {

	//printf(WHERESTR "In terminate for thread %d, finding next thread\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id);
	thread_struct* nextThread = getNextWaitingThread();
	if (nextThread == NULL) {
		//printf(WHERESTR "In terminate, no more waiting threads, return to main\n", WHEREARG);
		free(threads);
		threads = NULL;
		current_thread = NULL;
		longjmp(*main_env, 1);
	} else {
		//printf(WHERESTR "In terminate, flagging thread %d, as dead and resuming %d\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id, nextThread->id);
		current_thread->id = -1; //Flag it as dead
		current_thread = nextThread;
		longjmp(current_thread->env, 1);
	}
}

/*void PrintRegisters(jmp_buf env)
{
	unsigned int* temp;
	printf("R0: %d, %d, %d, %d\n", ((unsigned int *)env)[0], ((unsigned int *)env)[1], ((unsigned int *)env)[2], ((unsigned int *)env)[3]);
	printf("R1: %d, %d, %d, %d\n", ((unsigned int *)env)[4], ((unsigned int *)env)[5], ((unsigned int *)env)[6], ((unsigned int *)env)[7]);
	temp = (unsigned int *)(((unsigned int*)env)[SP * INTS_PR_REGISTER]);
	while(temp != NULL)
	{
		printf(WHERESTR "Backtracing stack, SP is %d, avalible space is %d \n", WHEREARG, temp[0], temp[1]);
		temp = (unsigned int*)temp[0];
	}
	printf("\n");

}*/

void CopyStack(jmp_buf env, void* newstack)
{
	void* begin;
	void* end;
	unsigned int* temp;
	unsigned int* temp2;
	unsigned int size;
	unsigned int remainsize;
	unsigned int extraspace;
	int offset;
	void* newsp;
	
	begin = (void*)(((unsigned int*)env)[4]);
	temp = (unsigned int*)begin;
	extraspace = ((unsigned int*)temp[0])[0]- temp[0];

	while(temp != NULL)
	{	
		if (temp[0] != 0)
			end = (void*)temp[0];
		temp = (unsigned int*)temp[0];
	}
	
	size = (end - begin);
	newsp = (newstack + STACK_SIZE) - size;
	memcpy(newsp, begin, size);
	remainsize = STACK_SIZE - size;
	
	offset = newsp - begin; 
	temp = (unsigned int*)(((unsigned int*)env)[4]);
	temp2 = (unsigned int *)newsp;
	//temp = (unsigned int*)temp[0]; //Skip the first
	
	while(temp != NULL)
	{
		if (temp[0] != 0)
		{
			temp2[0] = temp[0] + offset;
			temp2[1] = remainsize;
			temp2[2] = temp[0] + offset;
			temp2[3] = temp[0] + offset;
			
			remainsize += ((unsigned int*)temp[0])[0] - temp[0];
		}
		else
			((int*)temp2)[1] = -3200;

		//printf(WHERESTR "Assignment, SP %d (%d), To SP %d (%d) \n", WHEREARG, temp[0], temp[1], temp2[0], temp2[1]);
		
		temp = (unsigned int*)temp[0];
		temp2 = (unsigned int*)temp2[0];
	}
	
	((unsigned int*)env)[4] = (unsigned int) newsp;
	((unsigned int*)env)[5] = STACK_SIZE - size - extraspace;
	((unsigned int*)env)[6] = (unsigned int) newsp;
	((unsigned int*)env)[7] = (unsigned int) newsp;
	
}

/*
	Spawns a number of threads.
	A return value -1 means that the main thread is returning
	A return value less than -1 indicates an error
	A return value of zero or larger indicates the ID of the process
*/
int CreateThreads(int threadCount)
{
	if (main_env != NULL)
	{
			printf("Cannot re-enter this function\n");
			return -2;
	}

	if (threadCount <= 1)
	{
			printf("Must have at least two threads\n");
			return -2;
	}

	main_env = (jmp_buf*)MALLOC(sizeof(jmp_buf));
	if (main_env == NULL)
	{
			printf("Out of memory\n");
			return -2;
	}

	current_thread = NULL;
	no_of_threads = threadCount;

	//printf(WHERESTR "Before setjmp w/ main\n", WHEREARG);
	switch(setjmp(*main_env))
	{
		case 1:
			//printf(WHERESTR "After setjmp w/ main\n", WHEREARG);
			free(main_env);
			main_env = NULL;
			return -1;
		case JMP_MALLOC:
			//Special malloc case
			malloc_result = malloc(malloc_size);
			longjmp(current_thread->env, 1);
			break;
		case JMP_MALLOC_ALIGN:
			//Special malloc_align case
			malloc_result = _malloc_align(malloc_size, malloc_base);
			longjmp(current_thread->env, 1);
			break;
		case JMP_FREE:
			//Special free case
			free(malloc_result);
			longjmp(current_thread->env, 1);
			break;
		case JMP_FREE_ALIGN:
			//Special free case
			_free_align(malloc_result);
			longjmp(current_thread->env, 1);
			break;
	}
	
	//printf(WHERESTR "After setjmp NOT main\n", WHEREARG);

	//Create the treads
	threads = (thread_struct*) MALLOC(sizeof(thread_struct) * no_of_threads);
	if (threads == NULL)
		perror("SPU malloc failed for thread storage");

	//printf(WHERESTR "Creating threads\n", WHEREARG);
		
	for(loop_counter = 0; loop_counter < no_of_threads; loop_counter++)
	{
		//printf(WHERESTR "Creating thread %d\n", WHEREARG, loop_counter);
		threads[loop_counter].id = loop_counter;
		//current_thread = &threads[loop_counter];
		if (setjmp(threads[loop_counter].env) == 0)
		{
			//printf(WHERESTR "Created thread %d\n", WHEREARG, loop_counter);
			//PrintRegisters(threads[loop_counter].env);

			CopyStack(threads[loop_counter].env, threads[loop_counter].stack);				

			//PrintRegisters(threads[loop_counter].env);
	
			//printf(WHERESTR "Assigned stack for %d\n", WHEREARG, loop_counter);
		}
		else
		{
			//printf(WHERESTR "Returning from a thread, threadid %d\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id);
			break;
		}
	}

	//printf(WHERESTR "Done creating threads, threadid: %d\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id);

	if (current_thread == NULL)
	{
		//printf(WHERESTR "Initial create, setting thread to #0\n", WHEREARG);
		current_thread = &threads[0];
		longjmp(current_thread->env, 1);
	}
		
	if (current_thread == NULL)
		return -2;
	else
	{
		//printf(WHERESTR "Returning\n", WHEREARG);
		return current_thread->id;
	}
}

void thread_free(void* data)
{
	if (main_env == NULL)
		free(data);
	else
	{
		if (setjmp(current_thread->env) == 0)
		{
			malloc_result = data;
			longjmp(*main_env, JMP_FREE);
		}
	}
}

void* thread_malloc(unsigned long size)
{
	if (main_env == NULL)
		return malloc(size);
	else
	{
		if (setjmp(current_thread->env) == 0)
		{
			malloc_result = NULL;
			malloc_size = size;
			longjmp(*main_env, JMP_MALLOC);
		}
		return malloc_result;
	}
}

void thread_free_align(void* data)
{
	if (main_env == NULL)
		_free_align(data);
	else
	{
		if (setjmp(current_thread->env) == 0)
		{
			malloc_result = data;
			longjmp(*main_env, JMP_FREE_ALIGN);
		}
	}
}

void* thread_malloc_align(unsigned long size, unsigned int base)
{
	if (main_env == NULL)
		return _malloc_align(size, base);
	else
	{
		if (setjmp(current_thread->env) == 0)
		{
			malloc_result = NULL;
			malloc_size = size;
			malloc_base = base;
			longjmp(*main_env, JMP_MALLOC_ALIGN);
		}
		return malloc_result;
	}
}



/*
	Returns true if a thread switch is possible
*/
int IsThreaded(void)
{
	return main_env != NULL;
}

/*
	Selects another process to run, if any.
	A return value of zero indicates success, any other value indicates failure.
*/
int YieldThread(void)
{
	thread_struct* nextThread;

	if (main_env == NULL || threads == NULL || current_thread == NULL)
	{
		//printf("Yield called before mainenv was initialized");
		return -2;
	}

	//printf(WHERESTR "In yield, setting return value\n", WHEREARG);
	if (setjmp(current_thread->env) != 0)
	{
		//printf(WHERESTR "In yield, return from longjmp\n", WHEREARG);
		return 0;
	}
	else
	{
		//printf(WHERESTR "In yield, selecting next thread\n", WHEREARG);
	}

	//printf(WHERESTR "In yield, getting next thread\n", WHEREARG);
	nextThread = getNextWaitingThread();
	//printf(WHERESTR "In yield, next thread is %d\n", WHEREARG, nextThread == NULL ? -1 : nextThread->id);

	if (nextThread == NULL)
		return 0;
	else
	{
		//printf(WHERESTR "In yield, resuming thread %d from thread %d\n", WHEREARG, nextThread->id, current_thread->id);
		current_thread = nextThread;
		longjmp(current_thread->env ,1);
	}
	
	return 0;
}

#include <malloc.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include "../header files/SPUThreads.h"
#include "../../common/debug.h"

static thread_struct* threads = NULL; //The threads
static thread_struct* current_thread = NULL; //The currently executing thread
static thread_struct* main_env = NULL; //The main entry point
static int no_of_threads; //Keep number of threads on the heap
static int loop_counter; //Keep the loop counter on the heap
	

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

	printf(WHERESTR "In terminate for thread %d, finding next thread\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id);
	thread_struct* nextThread = getNextWaitingThread();
	if (nextThread == NULL) {
		printf(WHERESTR "In terminate, no more waiting threads, return to main\n", WHEREARG);
		free(threads);
		threads = NULL;
		current_thread = NULL;
		longjmp(main_env->env, 1);
	} else {
		printf(WHERESTR "In terminate, flagging thread %d, as dead and resuming %d\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id, nextThread->id);
		current_thread->id = -1; //Flag it as dead
		current_thread = nextThread;
		longjmp(current_thread->env, 1);
	}
}

/*
	Spawns a number of threads.
	A return value -1 means that the main thread is returning
	A return value less than -1 indicates an error
	A return value of zero or larger indicates the ID of the process
*/
int CreateThreads(int threadCount)
{
	int test = 4;
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

	main_env = (thread_struct*) malloc(sizeof(thread_struct));
	if (main_env == NULL)
	{
			printf("Out of memory\n");
			return -2;
	}

	current_thread = NULL;
	no_of_threads = threadCount;

	printf(WHERESTR "Before setjmp w/ main\n", WHEREARG);

	if (setjmp(main_env->env) == 0) 
	{
		void* newstack;
		
		unsigned int *temp;
		unsigned int *temp2;
		void* stackbegin;
		void* stackend;
		unsigned int stacksize;
		int offset;
		unsigned int* target;
		
		printf(WHERESTR "After setjmp NOT main\n", WHEREARG);
		
		//Create the treads
		threads = (thread_struct*) malloc(sizeof(thread_struct) * no_of_threads);
		if (threads == NULL)
			perror("SPU malloc failed for thread storage");

		printf(WHERESTR "Creating threads\n", WHEREARG);
			
		for(loop_counter = 0; loop_counter < no_of_threads; loop_counter++)
		{
			printf(WHERESTR "Creating thread %d\n", WHEREARG, loop_counter);
			threads[loop_counter].id = loop_counter;
			//current_thread = &threads[loop_counter];
			if (setjmp(threads[loop_counter].env) == 0)
			{
				printf(WHERESTR "Created thread %d\n", WHEREARG, loop_counter);
				newstack = (unsigned int*)threads[loop_counter].stack;

				temp = (unsigned int *)(((unsigned int*)threads[loop_counter].env)[SP * INTS_PR_REGISTER]);
				target = (unsigned int *)(((unsigned int*)threads[loop_counter].env)[SP * INTS_PR_REGISTER]);
				
				stackbegin = (void*)temp[0];
				
				printf(WHERESTR "Copy target %d, source %d, size %d\n\n", WHEREARG, target, temp, REGISTER_WIDTH);

				memcpy(target, temp, REGISTER_WIDTH);
				
				while(temp != NULL)
				{
					printf(WHERESTR "Backtracing stack, SP is %d, avalible space is %d \n", WHEREARG, temp[0], temp[1]);
					if (temp[0] != 0)
						stackend = (void*)temp[0];
					temp = (unsigned int*)temp[0];
				}
				
				stacksize = (stackend - stackbegin);
				newstack = (newstack + STACK_SIZE) - stacksize;
				printf(WHERESTR "Stacksize is determined to be %d (%d - %d)\n", WHEREARG, stacksize, stackend, stackbegin);
				memcpy(newstack, stackbegin, stacksize);

				offset = newstack - stackbegin; 
				temp = (unsigned int *)(((unsigned int*)threads[loop_counter].env)[SP * INTS_PR_REGISTER]);
				temp2 = (unsigned int *)newstack;
				
				while(temp != NULL)
				{
					if (temp[0] != 0)
						temp2[0] = temp[0] + offset;
					temp = (unsigned int*)temp[0];
					temp2 = (unsigned int*)temp2[0];
				}
				
			
				
				//(((unsigned int*)threads[loop_counter].env)[SP * INTS_PR_REGISTER]) = ((unsigned int)newstack); 
				temp = (unsigned int *)(((unsigned int*)threads[loop_counter].env)[SP * INTS_PR_REGISTER]);
			
				while(temp != NULL)
				{
					printf(WHERESTR "Backtracing new stack, SP is %d, avalible space is %d\n", WHEREARG, temp[0], temp[1]);
					temp = (unsigned int*)temp[0];
				}
		
				printf(WHERESTR "Assigned stack for %d\n", WHEREARG, loop_counter);
			}
			else
			{
				printf(WHERESTR "Returning from a thread, threadid %d\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id);
				break;
			}
		}

		printf(WHERESTR "Done creating threads, threadid: %d\n", WHEREARG, current_thread == NULL ? -1 : current_thread->id);

		if (current_thread == NULL)
		{
			printf(WHERESTR "Initial create, setting thread to #0\n", WHEREARG);
			current_thread = &threads[0];
			longjmp(current_thread->env, 1);
		}
			
		if (current_thread == NULL)
			return -2;
		else
		{
			printf(WHERESTR "Returning, test is %d\n", WHEREARG, test);
			return current_thread->id;
		}
	}
	else
	{
		printf(WHERESTR "After setjmp w/ main\n", WHEREARG);
		
		free(main_env);
		return -1;
	}
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
		printf("Yield called before mainenv was initialized");
		return -2;
	}

	printf(WHERESTR "In yield, setting return value\n", WHEREARG);
	if (setjmp(current_thread->env) != 0)
	{
		printf(WHERESTR "In yield, return from longjmp\n", WHEREARG);
		return 0;
	}
	else
	{
		printf(WHERESTR "In yield, selecting next thread\n", WHEREARG);
	}

	printf(WHERESTR "In yield, getting next thread\n", WHEREARG);
	nextThread = getNextWaitingThread();
	printf(WHERESTR "In yield, next thread is %d\n", WHEREARG, nextThread == NULL ? -1 : nextThread->id);

	if (nextThread == NULL)
		return 0;
	else
	{
		printf(WHERESTR "In yield, resuming thread %d from thread %d\n", WHEREARG, nextThread->id, current_thread->id);
		current_thread = nextThread;
		longjmp(current_thread->env ,1);
	}
	
	return 0;
}

#include <malloc.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "SPUThreads.h"

static thread_struct** threads = NULL; //The threads
static thread_struct* current_thread = NULL; //The currently executing thread
static thread_struct* main_env = NULL; //The main entry point
static int no_of_threads = 2;

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
		res = threads[(threadNo + i + 1) % no_of_threads];
		if (res->id != -1 && res->id != threadNo)
			return res;
	}

	return NULL;
}

/*
	Terminates the current running thread
*/
void Terminate(void) {

	thread_struct* nextThread = getNextWaitingThread();
	if (nextThread == NULL) {
		free(threads);
		threads = NULL;
		current_thread = NULL;
		longjmp(main_env->env, 1);
	} else {
		current_thread->id = -1; //Flag it as dead
		current_thread = nextThread;
		longjmp(current_thread->env, 1);
	}
}

/*
	Spawns two threads.
	A return value -1 means that the main thread is returning
	A return value less than -1 indicates an error
	A return value of zero or larger indicates the ID of the process
*/
int CreateThreads(int threadCount)
{
	int i;
	
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

	if (setjmp(main_env->env) == 0) 
	{
		//Create the treads
		threads = (thread_struct**) malloc(sizeof(thread_struct) * no_of_threads);
		for(i = 0; i < no_of_threads; i++)
		{
			threads[i]->id = i;
			if (setjmp(threads[i]->env) == 0)
			{
				((int *)current_thread->env)[SP] = (int)current_thread->stack + STACK_SIZE;
				longjmp(threads[i]->env, 1);
			}
		}

		Yield();
		if (current_thread == NULL)
			return -2;
		else
			return current_thread->id;
	}
	else
	{
		free(main_env);
		return -1;
	}
}


/*
	Selects another process to run, if any.
	A return value of zero indicates success, any other value indicates failure.
*/
int Yield(void)
{
	thread_struct* nextThread;

	if (main_env == NULL || threads == NULL)
	{
		printf("Yield called before mainenv was initialized");
		return -2;
	}

	if (current_thread == NULL) //First run
	{	
		current_thread = threads[0];
		return 0;
	}
	else
	{
		if (setjmp(current_thread->env) != 0)
			return 0;

		nextThread = getNextWaitingThread();
	}

	if (nextThread == NULL)
		return 0;
	else
		longjmp(nextThread->env ,1);
	
	return 0;
}

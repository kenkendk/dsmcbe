#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include <SPUThreads.h>
#include <debug.h>
#include <dsmcbe_spu.h>
#include <dsmcbe_spu_internal.h>

//** If you are using this file in another project (other than DSMCBE),
//** be aware that the threads cannot call malloc.

//Indicates that the thread is stopped
#define THREAD_STATE_STOPPED (0xff)
//Indicates that the thread is ready to run
#define THREAD_STATE_READY (0xfe)

//SP is in register 1
#define SP 1

//The width of a single register is 128 bits
#define REGISTER_WIDTH (128 / 8)

//The data for a single thread
typedef struct
{
	//The thread state, either THREAD_STATE_STOPPED, THREAD_STATE_READY or the request id that is blocking the thread
	int state;
	//The register jump buffer
	jmp_buf env;
	//The stack for the thread
	void* stack;
} dsmcbe_thread_struct;


//The list of active threads
dsmcbe_thread_struct* dsmcbe_threads = NULL;

//The id of the currently executing thread
int dsmcbe_current_thread = -1;

//The number of scheduled threads
int dsmcbe_thread_count = 0;

//The main entry point
jmp_buf dsmcbe_main_env;

//The id assigned to the SPE by libspe
unsigned long long dsmcbe_speid;

//The dsmcbe machine id
unsigned int dsmcbe_machineid;


//Returns the index of the current thread, or -1 if the current thread is invalid
inline int dsmcbe_thread_current_id()
{
	return dsmcbe_current_thread;
}

//Returns the status of the given thread
inline int dsmcbe_thread_get_status(int id)
{
	if (dsmcbe_threads == NULL)
		return THREAD_STATE_STOPPED;
	else
		return dsmcbe_threads[id].state;
}

//Sets the status of the given thread
inline void dsmcbe_thread_set_status(int id, int status)
{
	if (dsmcbe_threads != NULL)
		dsmcbe_threads[id].state = status;
}

//Returns the id of the next waiting thread, or -1 if no threads are waiting
int dsmcbe_thread_nextindex()
{
	int i;
	int threadNo = dsmcbe_current_thread;
	int firstBlocked = -1;

	//This is the scheduler, it selects the first non-blocked thread,
	// or the thread with the next id if all are blocked
	for(i = 0; i < dsmcbe_thread_count - 1; i++)
	{
		threadNo = (threadNo + 1) % dsmcbe_thread_count;
		//threadNo = (threadNo == 0 ? dsmcbe_thread_count : threadNo) - 1;

		if (dsmcbe_threads[threadNo].state == THREAD_STATE_READY)
			return threadNo;
		else if (firstBlocked == -1 && dsmcbe_threads[threadNo].state != THREAD_STATE_STOPPED)
			firstBlocked = threadNo;
	}

	return firstBlocked;
}

//Terminates the current running thread
void dsmcbe_thread_exit() {

	if (!dsmcbe_thread_is_threaded())
		return;

	//printf(WHERESTR "Stopping thread %d\n", WHEREARG, dsmcbe_current_thread);

	//printf(WHERESTR "In terminate for thread %d, finding next thread\n", WHEREARG, dsmcbe_current_thread);
	int nextThread = dsmcbe_thread_nextindex();
	if (nextThread == -1) {
		//printf(WHERESTR "In terminate, no more waiting threads, return to main\n", WHEREARG);
		dsmcbe_current_thread = -1;
		longjmp(dsmcbe_main_env, 1);
	} else {
		//printf(WHERESTR "In terminate, flagging thread %d, as dead and resuming %d\n", WHEREARG, dsmcbe_current_thread, nextThread);
		dsmcbe_threads[dsmcbe_current_thread].state = THREAD_STATE_STOPPED;
		dsmcbe_current_thread = nextThread;
		longjmp(dsmcbe_threads[dsmcbe_current_thread].env, 1);
	}
}

//Runs a number of threads, returns 0 when all threads are done, non-zero indicates an error
int dsmcbe_thread_start(unsigned int fibers, unsigned int stacksize)
{
	//printf(WHERESTR "dsmcbe_thread_start invoked with %u threads and stacksize %u\n", WHEREARG, fibers, stacksize);

	int i;

	if (dsmcbe_threads != NULL)
	{
		printf(WHERESTR "Cannot re-enter the dsmcbe_thread_start function\n", WHEREARG);
		return -1;
	}

	stacksize -= stacksize % REGISTER_WIDTH;

	if (fibers == 0)
		fibers = 1;

	dsmcbe_initialize();

	dsmcbe_threads = MALLOC(sizeof(dsmcbe_thread_struct) * fibers);
	if (dsmcbe_threads == NULL)
	{
		printf(WHERESTR "Out of memory\n", WHEREARG);
		return -1;
	}

	dsmcbe_thread_count = fibers;

	//printf(WHERESTR "Before setjmp w/ main\n", WHEREARG);
	if (setjmp(dsmcbe_main_env) == 1)
	{
		//printf(WHERESTR "All threads done, cleaning up\n", WHEREARG);

		//We are now done, free used resources
		for(i = 0; i < dsmcbe_thread_count; i++)
		{
			FREE_ALIGN(dsmcbe_threads[i].stack);
			dsmcbe_threads[i].stack = NULL;
		}

		FREE(dsmcbe_threads);
		dsmcbe_threads = NULL;

		dsmcbe_terminate();
		return 0;
	}
	
	//printf(WHERESTR "After setjmp main\n", WHEREARG);

	for(i = 0; i < dsmcbe_thread_count; i++)
	{
		if (setjmp(dsmcbe_threads[i].env) == 0)
		{
			//Initial setjmp call, allocate and initialize stack
			dsmcbe_threads[i].state = THREAD_STATE_READY;
			dsmcbe_threads[i].stack = MALLOC_ALIGN(stacksize, 7);

			if (dsmcbe_threads[i].stack == NULL)
			{
				printf(WHERESTR "Out of memory\n", WHEREARG);
				return -1;
			}

			//Clear the backpointer
			unsigned int* bp = (unsigned int*)(dsmcbe_threads[i].stack + (stacksize - REGISTER_WIDTH));
			bp[0] = 0;
			bp[1] = 0; //Faster than memset
			bp[2] = 0;
			bp[3] = 0;

			//printf(WHERESTR "Bp is %u\n", WHEREARG, (unsigned int)dsmcbe_threads[i].stack + (stacksize - REGISTER_WIDTH));

			//Set the back chain pointer for the new stack
			unsigned int* sp = dsmcbe_threads[i].stack + (stacksize - (REGISTER_WIDTH * 3));
			sp[0] = (unsigned int)bp;
			sp[1] = stacksize - REGISTER_WIDTH;
			sp[2] = (unsigned int)bp;
			sp[3] = (unsigned int)bp;

			//Set the new stack pointer for the longjmp
			unsigned int* newsp = (unsigned int*)(&dsmcbe_threads[i].env[SP]);
			newsp[0] = (unsigned int)sp;
			newsp[1] = stacksize - (REGISTER_WIDTH * 3);
			newsp[2] = (unsigned int)sp;
			newsp[3] = (unsigned int)sp;

			//printf(WHERESTR "Created stack for %i, stack=%u, sp=%u\n", WHEREARG, i, (unsigned int)dsmcbe_threads[i].stack, (unsigned int)sp);
		}
		else
		{
			//A thread has started, invoke its main function
			dsmcbe_main(dsmcbe_speid, dsmcbe_machineid, dsmcbe_thread_current_id());

			//Now the thread has completed, so lets mark it done
			dsmcbe_thread_exit();

			//We should never get here
			break;
		}
	}

	//printf(WHERESTR "Done creating threads, threadid: %d\n", WHEREARG, dsmcbe_current_thread);

	if (dsmcbe_current_thread == -1)
	{
		//printf(WHERESTR "Initial create, setting thread to #0\n", WHEREARG);
		dsmcbe_current_thread = 0;
		longjmp(dsmcbe_threads[0].env, 1);
	}

	//This should never happen
	return -1;
}

//Returns true if the current thread is not main (eg. there are no threads)
inline int dsmcbe_thread_is_threaded()
{
	return dsmcbe_threads != NULL && dsmcbe_current_thread != -1;
}

//Marks all threads waiting for the given requestId as ready.
//If no thread was activated, the function returns false, and otherwise true
int dsmcbe_thread_set_ready_by_requestId(int requestId)
{
	int i;
	int retval = FALSE;

	if (!dsmcbe_thread_is_threaded())
		return retval;

	for(i = 0; i < dsmcbe_thread_count; i++)
		if (dsmcbe_threads[i].state == requestId)
		{
			dsmcbe_threads[i].state = THREAD_STATE_READY;
			retval = TRUE;
		}

	return retval;
}

//If another thread is ready to run, it is activated, otherwise false is returned
int dsmcbe_thread_yield_any(int onlyReady)
{
	int nextThread;

	//Ignore this function, if we are not threaded
	if (!dsmcbe_thread_is_threaded())
		return FALSE;

	//Process any pending messages, this is done before we select the next thread
	while (spu_stat_in_mbox() != 0)
		spu_dsmcbe_readMailbox();

	nextThread = dsmcbe_thread_nextindex();

	if (nextThread == -1 || (dsmcbe_threads[nextThread].state != THREAD_STATE_READY && onlyReady))
	{
		return FALSE;
	}
	else
	{
		//Save state
		if (setjmp(dsmcbe_threads[dsmcbe_current_thread].env) != 0)
			return TRUE; //Return when we are awakened

		//Activate the other thread
		dsmcbe_current_thread = nextThread;
		longjmp(dsmcbe_threads[dsmcbe_current_thread].env ,1);
	}

	//We should never get here
}

int dsmcbe_thread_next_id(int onlyReady)
{
	int nextThread = dsmcbe_thread_nextindex();
	if (nextThread == -1 || (dsmcbe_threads[nextThread].state != THREAD_STATE_READY && onlyReady))
		return -1;
	else
		return nextThread;
}

void dsmcbe_thread_yield_to(int nextThread)
{
	if (nextThread < 0 || nextThread >= dsmcbe_thread_count)
		return;

	//Save state
	if (setjmp(dsmcbe_threads[dsmcbe_current_thread].env) != 0)
		return; //Return when we are awakened

	//Activate the other thread
	dsmcbe_current_thread = nextThread;
	longjmp(dsmcbe_threads[dsmcbe_current_thread].env ,1);

	//We should never get here
}

//If another thread can run, it is activated.
//The return value is true if a yield was performed, false otherwise
inline int dsmcbe_thread_yield()
{
	return dsmcbe_thread_yield_any(FALSE);
}

//Gets the number of threads created
inline int dsmcbe_thread_get_count()
{
	return dsmcbe_thread_count;
}

//If another thread is ready, it is activated.
//The return value is true if a yield was performed, false otherwise
inline int dsmcbe_thread_yield_ready()
{
	return dsmcbe_thread_yield_any(TRUE);
}

//The main entry point for the SPU application
int main(unsigned long long speid, unsigned long long argp, unsigned long long envp)
{
	dsmcbe_speid = speid;
	dsmcbe_machineid = (unsigned int)argp;

	//printf(WHERESTR "In main, envp=%llu, fibers=%u, stacksize=%u\n", WHEREARG, envp, (unsigned int)(envp & 0xffff), (unsigned int)(envp >> 16));

	return dsmcbe_thread_start((unsigned int)(envp & 0xffff), (unsigned int)(envp >> 16));
}

#ifndef SPUTHREADS_H_
#define SPUTHREADS_H_

//Returns the index of the current thread, or -1 if the current thread is invalid
int dsmcbe_thread_current_id();

//Terminates the current running thread
void dsmcbe_thread_exit();

//Returns true if the current thread is not main (eg. there are no threads)
inline int dsmcbe_thread_is_threaded();

//If another thread can run, it is activated.
//The return value is true if a yield was performed, false otherwise
inline int dsmcbe_thread_yield();

//Gets the number of threads created
inline int dsmcbe_thread_get_count();

//If another thread is ready, it is activated.
//The return value is true if a yield was performed, false otherwise
inline int dsmcbe_thread_yield_ready();

//Returns the status of the given thread
inline int dsmcbe_thread_get_status(int id);

//Sets the status of the given thread
void dsmcbe_thread_set_status(int id, int status);

#endif /*SPUTHREADS_H_*/

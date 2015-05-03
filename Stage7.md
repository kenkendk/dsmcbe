A full implementation of all the stages before this, will give a complete system that satifies our goals, and is expected to perform well. In this stage we will implement some utillity functions that will ease use of the system, but does not add any new functionallity.

### Syncronization primitives ###
Even though DSMCBE handles syncronization within the consistency model, it is often usefull to have explicit syncronization primitives. The simplest syncronization primitive is a mutex. It can be implemented by allocating a small object. If all processes attempt to gain a write lock on the object, it works as a mutex, since only one process may have the write lock at a time.

Once a mutex is implemented, it is easy to implement a semaphore. The implementation is basically the same as for the mutex, but a value is written into the allocated object. This requires some form of waiting mechanism, because the process must release the write lock before another process can write into the object. This waiting can be active, or passive by waiting for an object that is not yet created (this requires that the object can be released again).

A barrier can be created with the same pattern. The barrier processes agree on two ID's. The first ID is used to create an data object, and write the value 0 into the object. When a process reaches the barrier, it acquires a write lock on the object and increments the value. If the value is lower than the barrier threshold, the process acuires the object with the second ID. Since this object does not yet exist, the process waits. When the final process reaches the barrier, it resets the value and creates the object with the second ID. This will activate all processes that waits for this object. The process that created the object can now release the object again, because the request for the write lock will be put in the back of the queue, and thus all processes will have proceeded, when the object is removed.

### TryAcquire ###
All `acquire` calls in DSMCBE are blocking, which is easy to control. In some cases, a program can benefit from being able to do other work while an object is locked. To achieve this, we add a function with the following syntax:
```
  void* tryAcquire(unsigned int id, unsigned long& size, int type, unsigned long offset, unsigned long timeout);
```

The functionality of the function is equvalent to the regular `acquire` function, but maintains a timeout. Hvis the request is in queue for a period that is longer than the given timeout, a NULL pointer is returned. When aborting the wait, the process must also be removed from the waitlist. If the requested object is on a remote machine, it is possible that the access is being granted at the same time as the wait is aborted. In this case, the machine replies as if the process released the lock immediately.

### CreateOrAcquire ###
For the majority of functions, it is resonable that a single process handles object creation. In some cases it might be benficial that the first process that accesses an object creates it. We defined such a function in stage 1:
```
function void* CreateOrAcquire(unsigned int ID, unsigned long& size)
{
  void* result = create(id, size);
  if (result == NULL)
    result = acquire(id, size);
  return result;
}
```

### MultiAcquire ###
As many small packages gives poor performance, the system should try to gather requests. The problem with this approach is that the system cannot determine when the calling process will send another request. One solution is to delay transmission of all requests, and await another request. Unfortunately this delays all messages.

Another solution is to move responsibility onto the programmer, and let the programmer request multiple items with a single call:
```
	void** multiAcquire(unsigned int itemCount unsigned int* ids, unsigned long* sizes);
```

One should observe that such a function can increase the occurence of deadlocks, since it is not entirely visible what order the locks are taken in.

### Free and Resize ###
We have assumed that shared objects exist until program termination. The function just has to lock the write table and the data object. It can then write an invalid value into the pagetable and release both. This must be done by the data origin, because it has to release the datastructures allocated for the object. The programmer has to be aware that if any processes try to `acquire` the object after it has been free'ed they will behave as if the object had never existed. This means that the calls will block and not return an error.

This waiting mechanism can be used to re-create the object with another size. If the function is built into DSMCBE the amount of data copying required can be reduced to a minimum. The programmer must take care to ensure that all parts of the program can handle this size change.

### Mailbox ###
In order for DSMCBE to work, it must transmit messages. So far, we have reserved the mailboxes for exclusive use for DSMCBE. To allow the user program to use the mailboxes, we must add some functions that does this without interfering with DSMCBE:
```
    //SPE version
    void* getMailbox(int& size)
    void sendMailbox(void* msg, unsigned int size)
	
    //PPE version
    void sendMailbox(spe_t recipient, void* msg, unsigned int size)
    void* getMailbox(spe_t sender, unsigned int& size)
```

It does not make sense to send mailbox messages over the network, as there is no performance gain. The mailbox functions only work within a single Cell BE machine.
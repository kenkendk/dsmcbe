Having implemented a fully functional system in stage 1, we add lightweight threads to the SPE, and allow the SPE to create new shared objects. Event though it is possible to implement the lightwight threads on top of stage 1, without modifying the stage 1 code, we will build it with a tighter integration. None of the other stages depend on stage 2.

### Create ###
The create function should do exactly the same as on the PPE. The only extra handling is forwarding the call to the PPE, since it is not desireable for a SPE to administrate the data. The call is blocking but otherwise identical to the same call in stage 1.

### Lightweight threads ###
As indicated in the Cell BE handbook, thread switching is fully supported on the SPE. The functionality is ment to aid the operating system in providing preemptive thread switching, and thus involves a complete switch of the SPU problem state. This is a resonable implementation, since the operating system has no way to determine what the SPE is currently working on. Unfortunately it also clears all pending DMA transfers and restart them once the thread is restored. This is not desireable for overlapping DMA transfers and execution.

By defining what operations are permitted, and which are not, it is possible to reduce the requirements and hereby also the cost of a thread switch.

We want to perform a thread switch that only involves switching the registerfile and thus the call stack. Such a switch has very little overhead, but requires that the program does not use hardware interrupts without special care.

Since the SPE only has 256Kb of LS and a register file with 2Kb data, the amount of concurrent threads is small. Since the 256Kb is also shared with data and program code, we  expect the number of threads is 4 or less. For most programs 2 threads should give the desired overlap between data transfer and execution.

The implementation consists of the functions `createThreads`, `yield` and `terminate`. In an effort to simplify the interface, we have choosen not to use the standard pthreads signatures. The functionality is more similar to the unix fork() call.

### CreateThreads ###
In order to create threads, the program calls the `createThreads` function with an integer denoting the number of threads to create. The function allocates space for the threads' stack and register files. The return value of the function indicates the threads number. When all threads have called `terminate`, the originating thread returns with the value -1. The syntax is:
```
  int createThreads(int threadCount);
```

### Yield ###
When a thread want's to allow another thread to execute, it calls the function `yield`. Calling `yield` will switch to the next thread, based on the thread ID. This makes a very simple scheduling mechanism. There is no problem in allowing a thread to call `yield`, but  it is usually simpler if the library does this automatically. We will call yield while a thread awaits a pending DMA transfer. The syntax is:
```
  void yield();
```

### Terminate ###
When a thread completes all assigned work, it must call the `terminate` function. This will free the resources allocated for the thread, and switch to the next avalible thread. If there are no more threads, control will resume to the thread that orignally called `createThreads`. If a thread exits without calling the `terminate` function, the SPU runtime library will raise an error because the call stack is empty at the function completion. The syntax is:
```
  void terminate();
```

### Typical use of the thread system ###
A typical use of the thread system will look like this:
```
  int threadNo;
  //At this point there is only 1 thread
  threadNo = createThreads(2);
  if (threadNo == -1) //Main thread returns
    return;
  //At this point, there are two threads
  
  while(work_to_do)
  {
    ... perform work ...
    yield(); //Switch thread, implicit when waiting for DMA
  }
  terminate();
  //This line will not be hit
```

### Summary ###
The functions in stage 2 are largely unrelated to DSM systems, but gives the programmer a simple way of implementing overlapped execution and data transfer. The interface looks very similar to traditional multhreaded programming.
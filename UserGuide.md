# Introduction #
This page describes how you can build programs for the Cell BE platform, using DSMCBE.

# Pre-requisites #
Before you can start using DSMCBE, you must know how to write programs for the Cell Platform. This guide will not explain this. You can read [this IBM guide](http://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/FC857AE550F7EB83872571A80061F788), if you want to get started.

When building Cell projects, you are usually building a PPU unit and an SPU unit. Although you can run cell programs (and DSMCBE programs) without an SPU unit, they will not give you high performance.

Due to requirements from the [MiG project](http://www.migrid.org/) DSMCBE requires [GLib 2.14.6](http://library.gnome.org/devel/glib/). If you machine does not have that version of GLib installed, you can get it from [the svn source](http://code.google.com/p/dsmcbe/source/browse/#svn/trunk/glib-ppc32/usr/lib).

# Linking and compiling with DSMCBE #
Once you have a copy of DSMCBE, you should reference the header files. For the PPU unit, you must include dsmcbe\_ppu.h, and for the SPU unit, you must include dsmcbe\_spu.h.

When linking, the PPU must link to libdsmcbe\_ppu.a and the SPU must link to libdsmcbe\_spu.a.

A sample PPU compile command would be:
```
ppu-gcc -ldsmcbe_ppu -lthread-2.0 -lglib-2.0 file.c -o"Program"
```

A sample SPU compile command would be:
```
spu-gcc -ldsmcbe_spu file.c -o"SPU"
```

# Getting started #
If you [check out the source code from the repository](http://code.google.com/p/dsmcbe/source/checkout), there are a number of applications that are ready to run. For starters you may want to look at the simple [Hello world application](http://code.google.com/p/dsmcbe/source/browse/#svn/trunk/Applications/HelloWorld). If you are using Eclipse for development, you can import the DSMCBE folder and the desired Application folder into your workspace, and it should compile right away.

# DSMCBE function overview #
DSMCBE works by handling data transfers between PPU units (threads) and SPU units. If required, DSMCBE also transfers data between machines in a cluster, in such a way that the programmer does not have to deal with localization or transfers.

In DSMCBE, each data object has a unique id, named a GUID. A GUID is a 32 bit unsigned integer, and is provided when creating an object. The GUID zero is reserved for internal use by DSMCBE. In the current release, the maximum GUID allowed, is defined to be 50000.

In DSMCBE you create a shared object by calling the function **create**:
```
size_t i;
GUID my_id = 42;
int* my_data = create(my_id, sizeof(int) * 4);

for(i = 0; i < 4; i++)
    my_data[i] = i;

release(my_data);
```

As shown, you can create objects of any size, using DSMCBE.  After an object has been created, it can be acquired by any unit in DSMCBE, by calling the function **acquire**:
```
size_t i;
size_t my_size;
GUID my_id = 42;

int* my_data = acquire(my_id, &my_size, ACQUIRE_MODE_READ);

for(i = 0; i < 4; i++)
    printf("my_data[%d] = %d\n", i, my_data[i]);

release(my_data);
```

All calls in DSMCBE are blocking. So if the object is not created at the time the **acquire** call is made, the call will block until the object is created and released. This is very usefull in startup situations, as well as result gathering situations. When an object is created, it is automatically locked, so the creator can fill it with meaningfull data before anyone else can get to it.

Since all units must agree on what ID to use, it may be preferable to define ID's statically in a common header file, using a #define directive:
```
#define MY_ID 42
```

# Consistency #
DSMCBE uses a consistency model known as **entry consistency**. This model assigns a lock to each object and guarantees that data does not change while reading, if the lock is held. At the same time the model guarantees that only one unit may have the lock in write mode at any given time (exclusive access). After the lock is released the data will appear consistent at all locations, when the lock is granted.

For this model to work, it is required that the **release** function is called when data is no longer being used. The shorter the time a lock is held, the less likely it becomes that another process is being blocked, and this gives the best possible performance.
Note that **release** takes the pointer to the data, and _NOT_ the GUID. It is not allowed to call **acquire** twice from the same thread on the same GUID, without calling **release**. In other words, the DSMCBE locks are _NOT_ re-entrant. DSMCBE is thread safe, and handles concurrent access to data from multiple threads.

A data object can be acquired in either **READ\_ONLY** or **READ\_WRITE** mode. An object is acquired in **READ\_MODE** by calling **acquire** with the third parameter set to **ACQUIRE\_MODE\_READ**. It is up to the programmer to ensure that the code does not try to write to an object acquired as **READ\_ONLY**. The programmer must also ensure that the program does not try to read or write data after **release** has been called. If those rules are honored, DSMCBE guarantees that data does not change unexpectedly while holding the lock.

To **acquire** an object as **READ\_WRITE**, one must call **acquire** with the third agument set to **ACQUIRE\_MODE\_WRITE**. An object acquired as **READ\_WRITE** is automatically granted exclusive access to the data. DSMCBE guarantees that only one process can hold a write lock on the object at any given time. The changes made to an object with **READ\_WRITE** access, will be visible to other processes, shortly after **release** has been called. If all processes acquire the same object in **READ\_WRITE** mode, it is guaranteed that the changes from one process will be visible as soon as the next process recieves the data.

# Handling SPU units #
An SPU can access data the same way a PPU can. All calls are named the same, behave the same, and have the same signatures.

Since the performance of a Cell BE system is highly dependant on the performance of the SPU units, there are special functions in DSMCBE that target the SPU units. An essential part of SPU performance is the ability to perform **overlapped execution** (aka. double buffering). This technique involves fetching the next piece of data, while performing calculation on another. Since all calls in DSMCBE are blocking, there are special asynchronous calls avalible to handle that.

Consider the following non-optimized code:
```
size_t workcount = 1000;
size_t my_size;
GUID my_id = 42;

initialize();

for(i = 0; i < workcount; i++)
{
    void* my_data = acquire(my_id + i, &my_size, ACQUIRE_MODE_READ);
    calculate_on_data(my_data, my_size);
    release(my_data);
}

terminate();
```

In each run, data is being transfered, and calculated on. But each transfer takes time, and while transfering, the SPU is basically waiting for data. To remedy this situation, asynchronous calls provide overlapped execution:
```
size_t workcount = 1000;
size_t my_size;
GUID my_id = 42;

unsigned int handle1;
unsigned int handle2;

initialize();

handle1 = beginAcquire(my_id, ACQUIRE_MODE_READ);

for(i = 0; i < workcount; i++)
{
    if (i+1 < workcount)
	    handle2 = beginAcquire(my_id + i + 1);
		
    void* my_data = endAsync(handle1, &my_size);
    calculate_on_data(my_data, my_size);
    release(my_data);

    handle1 = handle2;
}

terminate();
```
By using asynchronous calls the transfer time can be hidden, if the calculation time is sufficiently large. The price for this is a slightly more complex code. By calling the function **getAsyncStatus** the code can determine if the data is ready. Advanced code can then end the transfers in a different order than they were started.

Note that the SPU cannot retrieve more data than there is room for. This is especially important for overlapped execution, because it requires twice the amount of storage. If the SPU runs out of space, the call (either **acquire** or **endAsync**) will return a null pointer.

DSMCBE also provides user-mode threads on the SPE, but you should not use them, because they take up too much memory, and are thus likely to reduce overall performance.

# Using DSMCBE on a cluster #
DSMCBE hides network transfers completely from the user. But before the network can be used, DSMCBE must be initialized with information about which machines are part of the network, and how to connect to those machines. The information is provided to DSMCBE through a file, which could look like this:
```
PS3-01 2237
PS3-02 4532
PS3-03 7756
```
Every line in the file names a machine, and a port number, seperated with spaces. The machines are enumerated so the first line is machine 0, the second line is machine 1, and so forth. It is important that all machines are given the exact same file. An example of starting DSMCBE with a network file looks like this:
```
int main(int argc,char** argv) {
	char buf[256];
	int spu_threads;
	int machineid;
	char* file;
	
	machineid = 0;
	spu_threads = 6;
	file = NULL;
	
	if(argc == 2) {
		machineid = atoi(argv[1]);
		file = "machinelist.txt"
	}

	pthread_t* threads = simpleInitialize(machineid, file, spu_count);
	
	.... code that performs work ....
	
	terminate();
}
```
By supplying the machine number on the commandline, it is possible to use the same compiled program on all participating machines. If NULL is supplied as the file name, it is assumed that the program only runs on the local machine. It is not required that all machines run the same compiled program, and one could also supply the filename on the commandline.

Machine number zero, that is, the first machine on the list, has a special role during startup. It initates communication between all the machines. It is recomended that machine number zero is started as the last one, otherwise there may be error messages during startup while the machines try to connect. If the connection fails, the machine will wait 5 seconds and try again, and retry up to 5 times.

DSMCBE does not perform package scheduling, and it is therefore recomended to use a switched network with full interconnect.

Machine number zero is also special, because it maintains a table that describes the location of all objects. When a new object is created, machine zero is notified of this action. If the program creates many objects, machine zero can be overloaded, but this does not happen with regular usage.

# Using barriers #
A barrier in DSMCBE can be implemented by creating an object containing an integer, initialized to the value zero. Each component that join the barrier acquires the object in **READ\_WRITE** mode. The integer in the object is incremented and is compared to the total number of components expected to join the barrier. If the number is equal to the expected number of components, the number is set to zero. If not, the component repeatedly calls **acquire** for the object in **READ\_ONLY** mode until this value is zero. An example of this could be:
```
#define BARRIER_COUNT 4
#define BARRIER_ID 42
unsigned int size;

unsigned int* barrier = acquire(BARRIER_ID, &size, ACQUIRE_MODE_WRITE);
(*barrier)++;
if (*barrier == BARRIER_COUNT)
    *barrier = 0;

while(*barrier != 0)
{
	release(barrier);
	acquire(BARRIER_ID, &size, ACQUIRE_MODE_READ);
}
release(barrier);
```
This works very well, and optimizations in DSMCBE heavily reduces the amount of data transfered. But each write to the object requires that the changes are distributed to the reading components. Since the content has no use other than for determining if the end criteria has been met, it can be optimized even more. For this reason, DSMCBE has an optimized barrier function where no unnecesary data is transmitted.

A barrier is created by calling the function **createBarrier** with the barrier **GUID**, and the number of processes that are meeting in the barrier. Processes that join in the barrier can call **acquireBarrier** with the barrier **GUID**. The call will block until all processes have arrived. Unlike a traditional **acquire**, no pointer is returned and **release** may not be called.
The optimal goal for DSMCBE is to be able to use a data object, regardless of its current location, by using the name of the data object. For an SPE, this requires that the EA of the object is retrieved and data is transfered by DMA to the SPE LS. It also requires that the compiler knows that the item being accessed is shared, possibly by hinting.

It is not possible to remove the DMA transfer, but it is possible to remove the compiler modification, if the programmer explicitly requests the shared objects. Since sending the actual name of the dataobject will increase the message size, we use ID's for objects, and the programmer can use standard C #define macros to assign meaningfull names:
```
#define picture 0
#define width 1
#define height 2
```

Data objects can be opened in either read-only or read-write mode. The read-only mode allows mutual access, and the read-write mode grants exclusive acces. The read-only mode will be refered to as read(-mode) and read-write mode will be refered to as write(-mode).

**TODO: Mention write-only here?**

This is close to the access patterns found in multithreaded programming, on other architectures.

The DMA system makes the SPE's able to process and fetch data simultaneously. This allows for the SPE's to run at full speed almost constantly. This also means that if the SPE is waiting for data, it is effectively wasting processing time. In a system where one process can obtain an exclusive lock on an object, there may be infinite waiting time, which translates into wasted processing time. A solution to this problem, is to switch execution context (ea. thread), so that other data can be processed.

### Lifting the system ###
If the above is efficiently implemented, it would be natural to expand the model to support multiple machines. Optimally, it would be possible to take a program that uses the shared memory model on one machine, and let it run on a cluster of Cell BE machines, with as little code modification as possible. This would make the Cell BE architeture much more accesible and attractive for programmers. The goals for DSMCBE thus becomes:
  * Ability to use data in read or write mode
  * Perform threadswithing on the SPE when data is unavalible
  * Use the same model for a single Cell BE as well as a cluser of Cell BE machines

To simplify the complete development process, we have split the development into stages. The first stage contains many limitiations to the above system, and the following stages remove the limitations gradually. This gives the possibility to start with something very simple and functional, and then build on this to achieve better and more advanced functionality.
Stage 1 is the first stage, and the result of an implementation of this stage is a functional system, which is severely limited. We will start by listing the limitations of this stage.

The first limitiation, is that there is only one Cell BE machine. This makes the step easier to understand and simplifies problems such as data locality. In this stage, it is desireable to use static locality for the data, because the PPE is the only element with a resonable storage capacity.

In stage 1, it is also only possible to fetch an entire object. This may pose problem, if the data objects are larger than the avalible LS space. We ignore that problem, because the programmer may split a large object manually, and it greatly simplifies the code, since data can only be avalible or not, not partially avalible.

In this stage there is only one way to retrieve an object, and that is a function called `acquire`, and one to release the object, called `release`. This means that all access is considered write access, which is requires exclusive access. If any unit tries to retrieve an object that is not avalible, the call will block until the object is avalible. All data transfers will be between main memory and LS, and we will not perform LS to LS transfers.

To create a new object, the process should call `create`, which is only avalible on the PPE. Below is a summarization of the limitations in the first stage:
  * Only a single Cell BE machine
  * Only complete object transfers
  * No SPE to SPE transfers
  * No threadswitching on blocking operations for the SPE
  * Only one type of `acquire` and `release` (write-mode)
  * The `create` function is only avalible on the PPE

The next step is to define the functions avalible in the system. To be able to use the system, there should be a function call to create new shared data objects. We call this function `create`, and it is only avalible on the PPE in the first stage. Once the shared object is created, it should be possible to retrieve it, and we name the function for that `acquire`. Since the acquire locks the data object, there should be a function to release the lock, when it is no longer needed. We call that function `release`. This is the three functions that we will implement for the use API in stage 1. The rest of this page describes how the three functions are implemented.

### Create ###
As described, it is only possible to create shared data objects on the PPE in stage 1. As all shared data objects must be identifiable, this function must take an identifier for the object to create. The Cell BE has strict allignment requirements for data transfer. It is thus very beneficial if all data transfers are correctly alligned. Since most programmers are ignorant to alignment, the `create` function should allocate the object and allign it correctly. This means that the function must also take a size parameter. After creation, the creating process is expected to fill the object with meaningfull data. To avoid race conditions, the `create` function will automically call the `acquire` function and return the created object with write access. This ensures that no other process can retrieve the object before the object contains meaningfull data.

To keep track of the processes that wait for a given locked object, a wait list i required. It is also required that the system stores the id, the size and the EA of the object. This information is most manageable if it is stored in a structure:
```
struct SharedObject
{
  unsigned int ID;
  unsigned long size;
  unsigned long EA;
  queue waitlist;
}
```
It is expected that the number of items will be large, and efficient access to the list is crucial, so we recommend a hashtable for storing this information.

The result of a call to the `create` function, depends on wether data already exists. If data is already created, the system could automatically call acquire on the object. We find that this can easily lead to bugs that are hard to find. Since the `create` call is probably fairly rare, it should be considered an error to create an object that already exists. It is possible to build a function that either creates or acquires an object in user code:
```
function void* CreateOrAcquire(unsigned int ID, unsigned long& size)
{
  void* result = create(id, size);
  if (result == NULL)
    result = acquire(id, size);
  return result;
}
```

If `create` is called, and the object does not exist, the function should allocate space for the object, record information about the object, and return the pointer to the object.
The final syntax for the function is:
```
  void* create(unsigned int id, unsigned long& size);
```

### Acquire ###
The `acquire` function is used to obtain access to a shared object, and in stage 1, this will always be write access. The process wishing to acquire an object, will send a request with the only known thing about the object, the ID. The PPE will then examine if the object is avalible. If the object is not yet avalible, the request is put into the waitlist for the object. Once the object is avalible, the PPE will return the EA and size of the object to the caller. If the caller was an SPE, it must allocate space locally, and transfer the object into LS via a DMA transfer. When all work is complete, the function can return a pointer to the data.

If a process calls acquire for an object that is not yet created, there are three logical actions to choose from: return an error, create the object or wait for the object to be created. Since a data object is rarely usefull before being filled with data, we choose not to create the object. Since the function will also wait for data if it is locked, the caller will anticipate some wait time, and we consider this to be the most logical action.

Later on, a timeout parameter for the `acquire` call can help in implementing functionallity that returns an error if the object is not yet created.

To achieve the waiting functionality, there should be a queue for processes waiting for objects that are not yet created. Once an object is created, this queue can be inserted as the objects waitlist, rather than using an empty one.

The final syntax for the `acquire` function is:
```
  void* acquire(unsigned int id, unsigned long& data);
```

### Release ###
Once an object is no longer needed, the caller must inform DSMCBE about this, with a call to `release`. Since stage 1 only has write access, the data must be written back to the originating EA, using a DMA transfer. Once data has been written, the PPE must examine if other processes are waiting for the object, and signal those. The syntax for the `release` function is:
```
  void release(void* data);
```
### Summary ###
We have described the three functions that constitutes the core of DSMCBE. We will try to implement DSMCBE with only these three functions. Later stages add parameters, and the final stage adds helper functions.

To communicate between SPE and PPE, we will use the fast Cell BE mailbox system. To further simplify this stage, we assume that user code does not wish to use the mailbox system.
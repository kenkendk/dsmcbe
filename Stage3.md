Stage 3 expands the system by allowing processes to retrieve data in read mode. This enables multiple processes to use the same piece of data, which is crucial for the systems overall performance. This expansion also requires the definition of a consistency model.

### Consistency ###
Much research indicates that optimal performance can be achieved by using a relaxed consistency model. We find _entry consistency_ to be the model with highest performance potential. This potential comes with the cost of added programming complexity. However, we do not feel that the extra complexity differs much from the way thread programming is usually handled. Multithreading usually has a mutex to ensure exclusive access to a region, and so has entry consistency.

We expect that most `acquire` calls will be for read access. We will try to reduce the overhead for this type of call. In stage 1 we blocked processes that tried to access locked data. This behavior continues, but there is no problems in allowing two processes read access to the same data object. Since such data cannot be altered, there is no need to send back any data when `release` is called. It is sufficent to keep a local copy of data, and return a pointer to this data if the process calls `acquire` again. If data is ever updated somewhere else, this change must propagate to all the read copies. This approach is known as  "multiple reader, single writer".

We choose to use the _invalidate_ model, and thus keep a list of all processes that has recieved a copy of the data. When a write has been performed, all processes in the list recieves an invalidate message. The recieving process then removes its local copy. When all copies have been invalidated, the writer recieves a copy. If another process calls acquire, its copy would be invalidated and it will have to fetch a fresh copy. This fetching would be delayed if the data object is still being written to.

We have devised an optimized version of release constiency that works well on systems with seperated physical memory, such as the Cell BE. It removes some of the wait time as illustrated in the figure: TODO: Upload image and link to it.

### Lists ###
For this model to work, it is only necesary to add an extra parameter to the `acquire` function, so the system can differentiate between a read and a write call. For each data object, there must now also be a list of read copies. There must also be an invalidation mechanism.

### Summary ###
This stage only adds en extra parameter to the `acquire` call:
```
  void* acquire(unsigned int id, unsigned long& size, int type);
```
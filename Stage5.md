So far, we have used the main storage as the only place to store data objects. This creates a bottleneck at the main storage, because the main storage cannot fully saturate the EIB. For situations where data is solely used by the SPE units of a single machine, there is no need to write data back to main memory, because Cell BE supports SPE to SPE transfers.

Such a situation could be one where all SPE units read a data object, and one of the SPE units write the object. After the SPE has written data, it must be replicated onto the other SPE units. Since Cell BE supports mapping one SPE's LS into EA, it is enough to simply return the EA, and thus utilize the EIB better, and reduce strain on the main memory. A more advanced system might also provide package management for the EIB, but we will not implement this.

### Implementation ###
By allowing data to exist solely on the SPE, some special situations must be handled. In the list of dataobjects, the EA can still be stored as before, but there must be some indication if the object is not in main memory. This is required, because the PPE must load the object into main memory if it should be transfered onto the network.

On the SPE there must be some indication that the object is the sole copy, and thus cannot be discarded before it has been transfered into main memory. If the dataobject is discarded, the PPE must provide a suitable place in main memory where the data can be placed. Both the SPE and the PPE must be able to initiate the transfer into main memory.

An easy solution is to keep both the current and the original EA address, that way it is easy to determine if the data object is in main memory or somewhere else.

### Summary ###
Since Cell BE supports mapping of LS adresses into EA, this task is fairly straightforward. We expect this expansion to give a significant improvement in performance, because of the increased EIB utilization, and the reduction on main memory access.
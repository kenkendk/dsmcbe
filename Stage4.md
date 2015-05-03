Stage 4 extends the system by handling more than one Cell BE machine. This extension requires some extra configuration, so the machines can communicate. The first problem we will adress is how this can be set up. Then we will decribe how the startup phase works, and  how the machines communcate. Finally we will discuss two different methods for finding data.

### Machine identification ###
There exists protocols that allows machines to determine group membership dynamically. For this system we choose a simpler model, where all machines are listed in a file. The system operator will the be responsible for making sure that this file is the same on all machines. We assume TCP communcation, and thus denote a machine address by IP adress and port number. An example file could look like this:
```
1, 192.168.1.1:1025
2, 192.168.1.2:4073
3, 192.168.10.1:7712
```

It is possible to use the adress as the ID, but if some form of ordering is required in the running program, an ID can be used for that purpose. A common use of ordering, is to assign the first machine the task of initializing the system, and all others wait for this one machine. It is also space consuming to have an ID, especially if the adresses become IPv6.

To load this file, we add a function, called `initialize`. This function must also assign the machine the correct ID. At a later time, it would be nice if the system could determine its own ID. For the simpler scenario, where there is only one machine, it is not neccesary to call `intialize`. It is possible to implement stage 4 directly on top of a stage 1 implementation, but the extensions in stage 3 are vital for system performance.

### Startup ###
To ensure that all machines can communicate in a Cell BE cluster, there must be a connection from each machine to all other machines. We use TCP for communication, and this means that each connection is bidirectional. To start up the process, we use the first machine in the list described above. This machine becomes the coordinator for the startup proccess. The coordinator starts by creating a permanent connection to each of the other machines in the list. This entails that all other processes must be started before the coordinator starts. The ccordinator then creates a page table (described later on), and instructs the remaining machines to start connecting. The coordinator waits until all machines have responded, and then instructs the machines to start.

The non-coordinator machines will start up and await a message from a coordinator. When they recieve the message, they will create permanent connections to every machine that has an ID that is greater than their own. This ensures that there is only one connection between each machine pair. Once a machine has created all connections, it reports back to the coordinator.

With this approach, some machines have to create more connections than others, but the overall time spent in the startup phase is expected to be very small. We find that there is no signinficant gain in improving this startup scheme.

### Network topology and routing ###
We choose to leave the routing problem to the network, and assume a full interconnect. If the network is segmented, ea. by switches, the routing task will be handled by the network.

### Communcation ###
In stage 3 we described that the system maintains a list of proccesses awating a given object, and a list of data copies. These lists must now be expanded to include the distinction between a local process and a remote machine. It is not neccesary to be able to specify a process on a remote machine, since each machine records this information locally.

Once the lists are implemented, the TCP server-client infrastructure must be implemented. This server/client must be handled in a seperate thread, so the network traffic does not interfere with other activity on the machine. The data transmitted between the machines is identical to the data transmitted between PPE and SPU over DMA and mailboxes.

To ensure maximum responsiveness, the communcation should be prioritized so that invalidation can be performed before data transfers. This can be implemented with a sorted list, based on transmission size. The problem with prioritizing is that it leads to starvation. Another problem is that it is much more efficient if multiple packages can be transmitted together. Starvation can be prevented by using a system with aging. By collecting data with the same destination, and use the sum of priorities, some perfomance can be gained.

### Migration ###
In some situations, the read and write patterns make it desireable to move the data origin. This movement will place the data closer to the place where it is used, and thus reduce the amount of transfered data. We have not found any research that examines the optimal migration threshold. We will try to make it as simple as possible to change the migration criteria, so others may experiment with this.

The migration will happen when a machine issues an acquire. The machine that responds to the request is the current origin, and will respond with a message that satisfies the request and notifices the requesting machine that it the data origin has changed.

### Localizing data ###
Once data has migrated, some machines may not know the correct location of data anymore. Is becomes necesary to implement a `NACK` message, so a machine can deny processing the request.

We have not found any litterature that describes the start up phase in data localization. Stage 1 described how the `acquire` and `release` functions work. We want to maintain this functionality when there are multiple machines involved. This makes it a problem to determine if a given data block exists or not. Without knowledge about the data location, the only solution is to ask every machine in turn. Once a positive reply is recieved, it is known that data exists. If no positive responses are recieved, the data does not exist. This makes it a potentially costly task to determine the existence of a given data object,. The problem can be reduced a little by saving a hint about the data location on all machines that has had the data object.

### Coordinator ###
One solution is to assign one machine to be the coordinator, so that this single machine can determine the existence question. If the coordinator machine keeps a list of processes waiting for the creation of a given object, it can transfer the list to the first machine that creates the data. By introducing a coordinator machine, the lookup time is reduced from O(n - 1) to O(1). Network race conditions can be controlled with a mutex on the coordinator machine. Unfortunately this model is very much like the "centralized server model", which means that the coordinator machine becomes a bottleneck. By using an adressing scheme, the coordinator role can be spread out across all machines in the network. A very simple model is to use the formula "_id_ mod _machine count_". This spreads out the coordinator role, and allows the user to "select" the coordinator machine.

### Pagetable ###
Another solution is to use a shared memory object, that works as a lookup table for created data objects. By using the DSMCBE memory model, it is possible to use the regular `acquire` and `release` functions to coordinate access. Special care must be taken to avoid deadlocks, since the programmer does not explicitly issue all `acquire` calls. Like the previous model, a coordinator is required for the startup of this special data object.

The advantage of this model is that each machine has an up-to-date overview of data locations and this reduces the number of errorneous communications to the short periods where a machine reads the object location right before a migration.

The disadvantage of this model is that there will be a transmission of potentially unused data between the machines. This problem can be mitigated by using a partial `acquire` which we describe in stage 6. Another problem is that the size of the pagetable must be determined at startup, because DSMCBE does not allow resizing objects in this stage.

If it is determined that the first machine creates the and is originator for the page table, other machines can easily retrieve the pagetable. It would be problematic to allow migration of the pagetable, even though this might improve performance.

We suggest that the pagetable is an array of machine ID's. The ID of a data object can then be used as the index into this array, and the value at the given index indicates the data objects origin. By using segmentation, it is possible to overcome the limitation that the pagetable is a fixed size. The last entry in the pagetable simply point to the next "page" in the pagetable. This will allow the pagetable to grow in an unlimited fashion. The implementation of a partial `acquire` will reduce the amount of unnecesary data transfers.

In this model, a create is implemented by first acquiring a write lock on the pagetable, or the relevant entry if partial `acquire` is implemented. If there is an invalid value in the table, the creating machines ID is written, and the pagetable is released. If there is a valid value at the entry, the object has already been created, and an error is returned. A machine that has previously tried to acquire the object, will retrieve an invalidate message for the page table, and by issuing a new `acquire`, it can get the ID of the owner machine, and transfer all pending requests to that machine.

Migration is accomplished by first aquiring a write lock on the pagetable, and then the object to migrate. Once the object has been transfered, the locks can be released. There is a potential risk that an `acquire` is in progress while the object is being migrated. This can be solved by responding with a NACK, which forces the process to re-acquire the updated pagetable.

### Summary ###
We belive the pagetable model has greater performance potential than the coordinator model, especially if the partial `acquire` method is implemented. The problematic situation where a single machine is the bottleneck, is reduced to cases where objects are created and migrated, which is expected to be a small part of an entire program.

### Protocol ###
To communicate properly over the network, a protocol is required. We choose to define eight different packages that can be transmitted over the network: `acquireRequest`, `acquireResponse`, `migrateResponse`, `releaseRequest`, `releaseResponse`, `NACK`, `invalidateRequest` and `invalidateResponse`.

Each package contains a field named packageCode as the first entry. This field is used to determine the package layout and size. All packaged contain a field named requestID, which is assigned a unique value by the sender. The responder inserts this requestID into the response, so that a requester can have multiple pending requests, and correctly process the responses. The `NACK` package includes a hint field that can contain the ID of a machine that is more likely to be able to process the request. The packages are structured like this:
```
    struct acquireRequest
    {
        //1 for read, 2 for write
        unsigned char packageCode = 1;
        unsigned int requestID;
        unsigned int dataItem;
    }

    struct acquireResponse
    {
        unsigned char packageCode = 3;
        unsigned int requestID;
        unsigned long dataSize;
        void* data;
    }

    struct migrationResponse
    {
        unsigned char packageCode = 4;
        unsigned int requestID;
        unsigned long dataSize;
        unsigned long waitListSize;
        void* data;
        void* waitList;
    }

    struct releaseRequest
    {
        unsigned char packageCode = 5;
        unsigned int requestID;
        unsigned int dataItem;
        unsigned long dataSize;
        void* data;
    }

    struct releaseResponse
    {
        unsigned char packageCode = 6;
        unsigned int requestID;
    }

    struct NACK
    {
        unsigned char packageCode = 7;
        unsigned int requestID;
        unsigned int hint;
    }

    struct invalidateRequest
    {
        unsigned char packageCode = 8;
        unsigned int requestID;
        unsigned int dataItem;
    }

    struct invalidateResponse
    {
        unsigned char packageCode = 9;
        unsigned int requestID;
    }
```

### Summary ###
It is possible to implement manual migration on a system that supports creation of a data object on a given machine, by letting the new machine create the data object, and informing other that they should use the new data object.

Once there are more than one machine in the system, a localization algorithm is required. If the localization alogrithm does not rely on a relation between the data object id and the originating machine, migration can be supported with very little effort. The system only needs to support NACK responses to `acquire` requests. By adding a special migration message, the migration procedure can be made more effective.

This stage only adds a single function to the API:
```
  void initialize(unsigned int id, const char* filename);
```
So far all transfers have been for entire objects. In many cases it will be much more usefull to retrieve a slice of the entire object, which is also known as byte oriented access. This means that one can access the distributed memory as if it was one big byte array. It also means that the programmer can choose the granularity of objects, and to some extent also the size of transfered data. The syntax for the `acquire` call is changed slightly from stage 1 (and stage 3):
```
  void* acquire(unsigned int id, unsigned long& size, int type, unsigned long offset);
```

If size is set to zero, the object data returned is the remainder of the data from the offset to the end. This also ensures maximum compatibility, as one can set size, type and offset to 0, and obtain the same behavior as in stage 1.

Up until now, all write `acquire` calls have locked the entire object. While it is possible to continue to do this, there is much performance to be gained by allowing a more fine grained access. This requires that the lists that contain locks and read references must be expanded to contain the requested span. This gives some problems, because lock requests can be overlapping. One could imagine the following scenario:
```
    Process | Start | Slut | Type
    -----------------------------
       P0      10      22    Read
       P1       1      15    Read
```
If _P3_ requests write access to element 11, the system must check that no-one has a write lock for that element. After that all read copies that contain the element must be invalidated. This means that in the worst case, this requires two complete list itterations. It is possible to optimize the list access a bit by sorting on the starting element:
```
    Process | Start | Size | Type
    -----------------------------
       P1       1      15    Read
       P0      10      22    Read
```

### Summary ###
The functionality in this stage can be manually implemented, by allocating a shared data object for each byte. By implementing the functionality in the system, it is possible to drastically reduce the overhead associated with the very fine grained access. It also enables SPE units to use parts of shared data objects that are too big to fit in the LS entirely.

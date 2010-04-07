#ifndef DSMCBE_H_
#define DSMCBE_H_

typedef unsigned int GUID;

#define ACQUIRE_MODE_READ 1
#define ACQUIRE_MODE_WRITE 2
#define ACQUIRE_MODE_DELETE 3

#define CREATE_MODE_NONBLOCKING 1
#define CREATE_MODE_BLOCKING 2
#define CREATE_MODE_BARRIER 3

#define OBJECT_TABLE_ID 0
#define OBJECT_TABLE_OWNER 0
#define OBJECT_TABLE_SIZE 50000
#define OBJECT_TABLE_ENTRY_TYPE unsigned char
#define OBJECT_TABLE_RESERVED 255
 
extern void* create(GUID id, unsigned long size, int mode);
extern void* acquire(GUID id, unsigned long* size, int mode);
extern void release(void* data);

extern void* createmalloc(unsigned long size);
extern void put(GUID id, void* data, void* func);
extern void* get(GUID id, unsigned long* size);

extern void acquireBarrier(GUID id);

#endif /*DSMCBE_H_*/

#ifndef DSMCBE_H_
#define DSMCBE_H_

typedef unsigned int GUID;

#define ACQUIRE_MODE_READ 1
#define ACQUIRE_MODE_WRITE 2
#define ACQUIRE_MODE_CREATE 3

#define PAGE_TABLE_ID 0
#define PAGE_TABLE_OWNER 0
 
extern void* create(GUID id, unsigned long size);
extern void* acquire(GUID id, unsigned long* size, int type);
extern void release(void* data);

#endif /*DSMCBE_H_*/

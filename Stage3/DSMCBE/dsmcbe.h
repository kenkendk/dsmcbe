#ifndef DSMCBE_H_
#define DSMCBE_H_

typedef unsigned int GUID;

#define READ 0
#define WRITE 1
 
extern void* create(GUID id, unsigned long size);
extern void* acquire(GUID id, unsigned long* size, int type);
extern void release(void* data);

#endif /*DSMCBE_H_*/

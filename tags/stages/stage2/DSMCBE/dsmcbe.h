#ifndef DSMCBE_H_
#define DSMCBE_H_

typedef unsigned int GUID;
extern void* create(GUID id, unsigned long size);
extern void* acquire(GUID id, unsigned long* size);
extern void release(void* data);

#endif /*DSMCBE_H_*/
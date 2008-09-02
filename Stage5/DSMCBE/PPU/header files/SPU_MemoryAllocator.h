#ifndef SPU_MEMORYALLOCATOR_H_
#define SPU_MEMORYALLOCATOR_H_

#include <glib/ghash.h>

struct SPU_Memory_Map_struct {
	//The offset used to calculator the resulting pointer
	unsigned int offset;
	//The size of the bitmap, memsize is 16 * 8 * size
	unsigned int size;
	
	//The total number of bytes avalible in memory
	unsigned int totalmem;
	
	//An index to the first byte with avalible space
	unsigned int first_free;
	//An index to the last byte with avalible space
	unsigned int last_free;
	
	//A count of free bytes, may not be consecutive
	unsigned int free_mem;

	//The actual map of avalible space. 
	//Each bit here corresponds to 16 bytes of memory 
	unsigned char* bitmap;
	
	//A list of reserved objects
	GHashTable* allocated;
};

typedef struct SPU_Memory_Map_struct SPU_Memory_Map;

SPU_Memory_Map* spu_memory_create(unsigned int offset, unsigned int size);

void spu_memory_destroy(SPU_Memory_Map* map);

void* spu_memory_malloc(SPU_Memory_Map* map, unsigned int size);

void spu_memory_free(SPU_Memory_Map* map, void* data);

#endif /* SPU_MEMORYALLOCATOR_H_ */

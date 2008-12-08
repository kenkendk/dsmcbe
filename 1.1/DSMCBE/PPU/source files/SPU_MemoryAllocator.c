#include "../header files/SPU_MemoryAllocator.h"
#include "../../common/debug.h"
#include "../../common/datapackages.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <memory.h>

struct SPU_Memory_Object_struct {
	unsigned int pointer;
	unsigned int size;
};

//#define DEBUGMAP

typedef struct SPU_Memory_Object_struct SPU_Memory_Object;

//Threshold is in number of bits, ea. 1 bit = ALIGN_SIZE_COUNT, so 4 gives 4*16 = 64 bytes
#define SIZE_THRESHOLD 16000000
#define ALIGN_SIZE_COUNT 16
#define BITS_PR_BYTE 8
#define SIZE_TO_BITS(x) (ALIGNED_SIZE(size) / ALIGN_SIZE_COUNT)

void printMap(SPU_Memory_Map* map)
{
	unsigned int i;
	struct SPU_Memory_Object_struct* this = NULL;

	printf("\nPrinting list\n");

	for(i = 0; i < g_list_length(map->bitmap); i++)
	{
		this = g_list_nth_data(map->bitmap, i);
		printf("Pointer: %i Size: %i\n", this->pointer, this->size);
	}
	
	printf("List printed\n\n");
	//sleep(1);
}


//The offset is the actual pointer, the size is the number of bits to flip
void SPU_Memory_update_bitmap(SPU_Memory_Map* map, void* offset, unsigned int size)
{
	unsigned int i;
	struct SPU_Memory_Object_struct* prev = NULL;
	struct SPU_Memory_Object_struct* this = NULL;
	struct SPU_Memory_Object_struct* next = NULL;
	struct SPU_Memory_Object_struct* obj = NULL;

	size = size * 16;
	for(i = 0; i < g_list_length(map->bitmap); i++)
	{
		this = g_list_nth_data(map->bitmap, i);
		if(this->pointer > (unsigned int)offset)
		{
			if (i > 0)
				prev = g_list_nth_data(map->bitmap, i-1);
			else 
				prev = NULL;
			
			if (i != g_list_length(map->bitmap) - 1)
				next = g_list_nth_data(map->bitmap, i+1);
			else 
				next = NULL;
						
			if(prev != NULL && prev->pointer + prev->size == (unsigned int)offset)
			{
				if (next != NULL && next->pointer == (unsigned int)(offset + size))
				{
					prev->size += next->size + size;
					map->bitmap = g_list_remove(map->bitmap, next);
					free(next);
#ifdef DEBUGMAP
					printf(WHERESTR "Freed %i bytes at %i using prev and next free\n", WHEREARG, size, (unsigned int)offset);
					printMap(map);
#endif
					return;	
				}
				else
				{
					prev->size += size;
#ifdef DEBUGMAP
					printf(WHERESTR "Freed %i bytes at %i using prev free\n", WHEREARG, size, (unsigned int)offset);
					printMap(map);
#endif
					return;
				}
			}
			else if (next != NULL && next->pointer == (unsigned int)(offset + size))
			{
				next->pointer = (unsigned int)offset;
				next->size += size;
#ifdef DEBUGMAP
				printf(WHERESTR "Freed %i bytes at %i using special normal free\n", WHEREARG, size, (unsigned int)offset);
				printMap(map);
#endif								
				return;
			}
			else if (this->pointer == (unsigned int)offset + size)
			{
				this->pointer = (unsigned int)offset;
				this->size += size;
#ifdef DEBUGMAP				
				printf(WHERESTR "Freed %i bytes at %i using special normal free\n", WHEREARG, size, (unsigned int)offset);
				printMap(map);
#endif								
				return;
			}
			else
			{
				obj = MALLOC(sizeof(struct SPU_Memory_Object_struct));				
				obj->pointer = (unsigned int)offset;
				obj->size = (unsigned int)size;
				
				map->bitmap = g_list_insert(map->bitmap, obj, i);
#ifdef DEBUGMAP
				printf(WHERESTR "Freed %i bytes at %i using normal free\n", WHEREARG, size, (unsigned int)offset);
				printMap(map);				
#endif
				return;
			}
		}		
	}
	
	if((unsigned int)(offset + size) <= (unsigned int)(map->offset + map->size * ALIGN_SIZE_COUNT * BITS_PR_BYTE))
	{
		obj = MALLOC(sizeof(struct SPU_Memory_Object_struct));				
		obj->pointer = (unsigned int)offset;
		obj->size = (unsigned int)size;
		
		map->bitmap = g_list_insert(map->bitmap, obj, g_list_length(map->bitmap));
#ifdef DEBUGMAP
		printf(WHERESTR "Freed %i bytes at %i using top free\n", WHEREARG, size, (unsigned int)offset);
		printMap(map);
#endif
		return;
	}
	 
	printMap(map);
	printf(WHERESTR "Tried to free offset %i with size %i - mapOffset %i, mapSize %i\n", WHEREARG, (unsigned int)offset, size, map->offset, map->size);	
	REPORT_ERROR("Could not free memory\n");
}

void* SPU_Memory_find_chunk(SPU_Memory_Map* map, unsigned int size)
{
	unsigned int i;
	unsigned int position;
	struct SPU_Memory_Object_struct* this = NULL;
	struct SPU_Memory_Object_struct* best = NULL;
	unsigned int waste = UINT_MAX;
	unsigned int temp = 0;

	unsigned int firstFit = g_list_length(map->bitmap) > 10 ? 1 : 0;
	
	for(i = 0; i < g_list_length(map->bitmap); i++)
	{
		this = g_list_nth_data(map->bitmap, i);
				
		if (this->size >= size)
		{
			if(firstFit)
			{	
				// Use FirstFit
				best = this;
				break;
			}
			else
			{
				// Use BestFit
				temp = this->size - size;
				//printf(WHERESTR "Temp: %u, Waste: %u\n", WHEREARG, temp, waste); 
				if (temp < waste)
				{
					best = this;
					waste = temp;
					if (waste == 0)
						break;
				}
			}
		}
	}
	
	this = NULL;
	
	//Allocate memory
	if(best != NULL)
	{
		position = best->pointer;
		if(best->size > size)
		{
			best->pointer += size;
			best->size -= size;
#ifdef DEBUGMAP
			printf(WHERESTR "1:Allocated %i bytes at %i\n", WHEREARG, size, position);
#endif			
		}
		else
		{
			map->bitmap = g_list_remove(map->bitmap, best);
			free(best);
#ifdef DEBUGMAP
			printf(WHERESTR "2:Allocated %i bytes at %i\n", WHEREARG, size, position);
#endif			
		}

#ifdef DEBUGMAP
		printMap(map);
#endif
		return (void*)position;
	}
	
	/*REPORT_ERROR("Could not find free memory!");
	sleep(10);*/
	return NULL;	
}


SPU_Memory_Map* spu_memory_create(unsigned int offset, unsigned int size) {
	SPU_Memory_Map* map = MALLOC(sizeof(SPU_Memory_Map));

	//Always adjust to start at 16 byte boundary
	if (offset % ALIGN_SIZE_COUNT != 0)
		offset += ALIGN_SIZE_COUNT - (offset % ALIGN_SIZE_COUNT);
	
	//We can only use full blocks of 16 bytes
	if (size % ALIGN_SIZE_COUNT != 0)
		size -= size % ALIGN_SIZE_COUNT;
		
	map->free_mem = size;
	map->offset = offset;		
	map->size = size / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
	//TODO: We scrap up to 16 * 7 bytes at the end of memory
	map->totalmem = map->size * ALIGN_SIZE_COUNT * BITS_PR_BYTE;
	map->first_free = 0;
	map->last_free = map->size - 2;
	map->allocated = g_hash_table_new(NULL, NULL);

	map->bitmap = NULL;
	struct SPU_Memory_Object_struct* obj = NULL;
	obj = MALLOC(sizeof(struct SPU_Memory_Object_struct));
	obj->pointer = (unsigned int)map->offset;
	obj->size = (unsigned int)map->size * ALIGN_SIZE_COUNT * BITS_PR_BYTE;	
	map->bitmap = g_list_insert(map->bitmap, obj, 0);
#ifdef DEBUGMAP
	printf(WHERESTR "Creating schemes - offset %i, size %i\n", WHEREARG, map->offset, map->size * ALIGN_SIZE_COUNT * BITS_PR_BYTE);
	printMap(map);
#endif	

	return map;
}

void* spu_memory_malloc(SPU_Memory_Map* map, unsigned int size) {
#ifdef DEBUGMAP	
	printf(WHERESTR "MEMMGR - Trying to malloc %i\n", WHEREARG, size);
#endif	
	unsigned int bitsize = SIZE_TO_BITS(size);
	void* data = SPU_Memory_find_chunk(map, size);
	if (data == NULL)
		return NULL;
	
	g_hash_table_insert(map->allocated, data, (void*)bitsize);
	map->free_mem -= bitsize * ALIGN_SIZE_COUNT;

	return data; 
}

void spu_memory_free(SPU_Memory_Map* map, void* data) {
#ifdef DEBUGMAP		
	printf(WHERESTR "MEMMGR - Trying to free %i\n", WHEREARG, (unsigned int)data);
#endif	
	unsigned int bitsize = (unsigned int)g_hash_table_lookup(map->allocated, data);
	if (bitsize == 0)
	{
		REPORT_ERROR("Pointer was not allocated, or double free'd");
		return;
	}
	
	SPU_Memory_update_bitmap(map, data, bitsize);
	
	g_hash_table_remove(map->allocated, data);
	map->free_mem += bitsize * ALIGN_SIZE_COUNT;
}

void spu_memory_destroy(SPU_Memory_Map* map) {
	g_list_free(map->bitmap);
	g_hash_table_destroy(map->allocated);
	map->allocated = NULL;
	FREE(map);
	map = NULL;
}

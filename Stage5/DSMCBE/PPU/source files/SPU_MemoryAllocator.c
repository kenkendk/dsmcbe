#include "../header files/SPU_MemoryAllocator.h"
#include "../../common/debug.h"
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

struct SPU_Memory_Object_struct {
	unsigned int pointer;
	unsigned int size;
};

typedef struct SPU_Memory_Object_struct SPU_Memory_Object;

//Threshold is in number of bits, ea. 1 bit = ALIGN_SIZE_COUNT, so 4 gives 4*16 = 64 bytes
#define SIZE_THRESHOLD 100000
#define ALIGN_SIZE_COUNT 16
#define BITS_PR_BYTE 8
#define ALIGNED_SIZE(x) (x + ((16 - x) % 16))
#define SIZE_TO_BITS(x) (ALIGNED_SIZE(size) / ALIGN_SIZE_COUNT)

unsigned char spu_memory_lead_bit_count[] = {
8,7,6,6,5,5,5,5,4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

unsigned char spu_memory_trail_bit_count[] = {
8,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,
3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,
3,0,1,0,2,0,1,0,7,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,0,1,0,2,0,1,0,
3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
4,0,1,0,2,0,1,0,3,0,1,0,2,0,1};


//The offset is the actual pointer, the size is the number of bits to flip
void SPU_Memory_update_bitmap(SPU_Memory_Map* map, void* offset, unsigned int size, unsigned char newvalue)
{
	unsigned int i;
	unsigned char map_byte;
	unsigned int position = (((unsigned int)offset) - map->offset) / ALIGN_SIZE_COUNT;
	
	//Find the first byte,regardless of direction
	unsigned int first = position / BITS_PR_BYTE;
	unsigned int accumulated = size;
	
	//Flip as many bits as required/avalible in the byte
	map_byte = map->bitmap[first];
	for(i = position % BITS_PR_BYTE; i < BITS_PR_BYTE && accumulated > 0; i++, accumulated--)
		if (newvalue == 0)
			map_byte &= ~(1 << (BITS_PR_BYTE - i - 1));
		else
			map_byte |= (1 << (BITS_PR_BYTE - i - 1));
	map->bitmap[first] = map_byte;
	first++;
	
	//mark all full bytes as reserved flag		
	if (accumulated > BITS_PR_BYTE)
	{
		if (newvalue == 0)
			memset(&map->bitmap[first], 0x00, accumulated / BITS_PR_BYTE);
		else
			memset(&map->bitmap[first], 0xff, accumulated / BITS_PR_BYTE);
		first += accumulated / BITS_PR_BYTE;
		accumulated = accumulated % BITS_PR_BYTE;
	}
	
	//If there are more bits left, update the slack
	if (accumulated > 0)
	{
		map_byte = map->bitmap[first];
		for(i = 0; i < accumulated; i++)
			if (newvalue == 0)
				map_byte &= ~(1 << (BITS_PR_BYTE - 1 - i));
			else
				map_byte |= (1 << (BITS_PR_BYTE - 1 - i));
		map->bitmap[first] = map_byte;
	}
}

void* SPU_Memory_find_chunk(SPU_Memory_Map* map, unsigned int size)
{
	unsigned int i, j;
	unsigned char map_byte;
	unsigned int position;
	
	unsigned int desired_bitcount = SIZE_TO_BITS(size);
	
	int direction = desired_bitcount > SIZE_THRESHOLD ? -1 : 1;
	unsigned int initial = desired_bitcount > SIZE_THRESHOLD ? map->last_free : map->first_free;

	unsigned char* prev = desired_bitcount > SIZE_THRESHOLD ? spu_memory_lead_bit_count : spu_memory_trail_bit_count;
	unsigned char* next = desired_bitcount > SIZE_THRESHOLD ? spu_memory_trail_bit_count : spu_memory_lead_bit_count;
	
	unsigned int accumulated = prev[map->bitmap[initial]];
	unsigned int first_avalible = initial;
	unsigned int current = initial;
	
	void* data;
	
	if (size == 0)
		return NULL;
	
	while(accumulated < desired_bitcount) {
		current += direction;
		
		//No more space, current can never be negative, so it becomes UINT_MAX, which is larger than size
		if (current > map->size)
			return NULL;
		
		//If this byte is zero, we can run further
		if (map->bitmap[current] == 0)
			accumulated += BITS_PR_BYTE;
		else {
			//Non-zero, we must restart the count
			accumulated += next[map->bitmap[current]];
			if (accumulated < desired_bitcount)
			{
				first_avalible = current;
				accumulated = prev[map->bitmap[current]];
			} 
		}
	}
	
	if (direction > 0) {
		map_byte = map->bitmap[first_avalible]; 
		
		//Find bit offset to the first non-avalible bit
		for(i = 0; i < BITS_PR_BYTE; i++)
			if (((1 << (i)) & map_byte) != 0)
				break;
	
		//Calculate the position in bytes	
		position = (first_avalible * BITS_PR_BYTE) + (BITS_PR_BYTE - i);
	} else {
		map_byte = map->bitmap[first_avalible]; 
		
		//Find bit offset to the first non-avalible bit
		for(i = 0; i < BITS_PR_BYTE; i++)
			if (((1 << (BITS_PR_BYTE-i-1)) & map_byte) != 0)
				break;
		
		//Calculate the bit offset from current
		j = (desired_bitcount - i) % BITS_PR_BYTE;
		
		position = (((current * BITS_PR_BYTE) + j));
	}
	
	data = (void*)(map->offset + (position * ALIGN_SIZE_COUNT));
	
	return data;
}


SPU_Memory_Map* spu_memory_create(unsigned int offset, unsigned int size) {
	SPU_Memory_Map* map;
	
	if ((map = malloc(sizeof(SPU_Memory_Map))) == NULL)
		REPORT_ERROR("malloc error");

	//Always adjust to start at 16 byte boundary
	if (offset % ALIGN_SIZE_COUNT != 0)
		offset += ALIGN_SIZE_COUNT - (offset % ALIGN_SIZE_COUNT);
	
	//We can only use full blocks of 16 bytes
	if (size % ALIGN_SIZE_COUNT != 0)
		size -= size % ALIGN_SIZE_COUNT;
		
	map->free_mem = size;
	map->offset = offset;		
	map->size = size / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
	map->first_free = 0;
	map->last_free = map->size - 1;
	map->allocated = g_hash_table_new(NULL, NULL);
	map->bitmap = malloc(map->size);
	memset(map->bitmap, 0, map->size);
	
	return map;
}

void* spu_memory_malloc(SPU_Memory_Map* map, unsigned int size) {

	unsigned int bitsize = SIZE_TO_BITS(size);
	void* data = SPU_Memory_find_chunk(map, size);
	if (data == NULL)
		return NULL;
	
	g_hash_table_insert(map->allocated, data, (void*)bitsize);
	SPU_Memory_update_bitmap(map, data, bitsize, 0xff);
	map->free_mem -= bitsize * ALIGN_SIZE_COUNT;
	
	if (bitsize > SIZE_THRESHOLD) {
		map->last_free = (((unsigned int)data) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
	} else {
		map->first_free = ((((unsigned int)data) + (bitsize * ALIGN_SIZE_COUNT)) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
	}
	
	return data; 
}

void spu_memory_free(SPU_Memory_Map* map, void* data) {
	unsigned int bitsize = (unsigned int)g_hash_table_lookup(map->allocated, data);
	if (bitsize == 0)
	{
		REPORT_ERROR("Pointer was not allocated, or double free'd");
		return;
	}
	
	SPU_Memory_update_bitmap(map, data, bitsize, 0x00);
	
	g_hash_table_remove(map->allocated, data);
	map->free_mem += bitsize * ALIGN_SIZE_COUNT;
	
	unsigned int newguess;
	
	if (bitsize > SIZE_THRESHOLD) {
		newguess = ((((unsigned int)data) + (bitsize * ALIGN_SIZE_COUNT)) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
		if (newguess > map->last_free)
			map->last_free = newguess;  
	} else {
		newguess = (((unsigned int)data) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
		if (newguess < map->first_free)
			map->first_free = newguess;  
	}
	
}

void spu_memory_destroy(SPU_Memory_Map* map) {
	free(map->bitmap);
	map->bitmap = NULL;
	g_hash_table_destroy(map->allocated);
	map->allocated = NULL;
	free(map);
}

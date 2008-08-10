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
#define SIZE_THRESHOLD 1000000
#define ALIGN_SIZE_COUNT 16
#define BITS_PR_BYTE 8
#define ALIGNED_SIZE(x) (x + ((16 - x) % 16))
#define SIZE_TO_BITS(x) (ALIGNED_SIZE(size) / ALIGN_SIZE_COUNT)

//This lookup table counts the number of consecutive bits that are zero, going from the left (MSB) 
unsigned char spu_memory_lead_bit_count[] = {
8,7,6,6,5,5,5,5,4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

//This lookup table counts the number of consecutive bits that are zero, going from the right (LSB)
unsigned char spu_memory_trail_bit_count[] = {
8,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,
3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,
3,0,1,0,2,0,1,0,7,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,0,1,0,2,0,1,0,
3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
4,0,1,0,2,0,1,0,3,0,1,0,2,0,1};

//This lookup table counts the number of consequtive zero bits in a byte
unsigned char spu_memory_free_bit_count[] = {
8,7,6,6,5,5,5,5,4,4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,5,4,3,3,2,2,2,2,
3,2,2,2,2,2,2,2,4,3,2,2,2,2,2,2,3,2,2,2,2,2,2,2,6,5,4,4,3,3,3,3,3,2,2,2,2,2,2,2,
4,3,2,2,2,1,1,1,3,2,1,1,2,1,1,1,5,4,3,3,2,2,2,2,3,2,1,1,2,1,1,1,4,3,2,2,2,1,1,1,
3,2,1,1,2,1,1,1,7,6,5,5,4,4,4,4,3,3,3,3,3,3,3,3,4,3,2,2,2,2,2,2,3,2,2,2,2,2,2,2,
5,4,3,3,2,2,2,2,3,2,1,1,2,1,1,1,4,3,2,2,2,1,1,1,3,2,1,1,2,1,1,1,6,5,4,4,3,3,3,3,
3,2,2,2,2,2,2,2,4,3,2,2,2,1,1,1,3,2,1,1,2,1,1,1,5,4,3,3,2,2,2,2,3,2,1,1,2,1,1,1,
4,3,2,2,2,1,1,1,3,2,1,1,2,1,1,0};

//This lookup table has a rising number of bits set from the left (MSB)
unsigned char spu_memory_lead_bits[] = { 0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF };

//This lookup table has a rising number of bits set from the right (LSB)
unsigned char spu_memory_trail_bits[] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };  


//The offset is the actual pointer, the size is the number of bits to flip
void SPU_Memory_update_bitmap(SPU_Memory_Map* map, void* offset, unsigned int size, unsigned char newvalue)
{
	unsigned int position = (((unsigned int)offset) - map->offset) / ALIGN_SIZE_COUNT;
	
	//Find the first byte,regardless of direction
	unsigned int first = position / BITS_PR_BYTE;
	unsigned int leadcount  = MIN(BITS_PR_BYTE - (position % BITS_PR_BYTE), size) ;
	
	unsigned int accumulated = size - leadcount;
	
	//Flip as many bits as required/avalible in the byte
	if (newvalue == 0)
		map->bitmap[first] &= ~(spu_memory_lead_bits[leadcount] >> (position % BITS_PR_BYTE));
	else
		map->bitmap[first] |= spu_memory_lead_bits[leadcount] >> (position % BITS_PR_BYTE);
		
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
		if (newvalue == 0)
			map->bitmap[first] &= ~(spu_memory_lead_bits[accumulated]);
		else
			map->bitmap[first] |= spu_memory_lead_bits[accumulated];
	}
}

void* SPU_Memory_find_chunk(SPU_Memory_Map* map, unsigned int size)
{
	unsigned int i;
	unsigned int position;
	
	unsigned int desired_bitcount = SIZE_TO_BITS(size);
	
	int direction = desired_bitcount > SIZE_THRESHOLD ? -1 : 1;
	unsigned int initial = desired_bitcount > SIZE_THRESHOLD ? map->last_free : map->first_free;

	unsigned char* prev = desired_bitcount > SIZE_THRESHOLD ? spu_memory_lead_bit_count : spu_memory_trail_bit_count;
	unsigned char* next = desired_bitcount > SIZE_THRESHOLD ? spu_memory_trail_bit_count : spu_memory_lead_bit_count;
	
	unsigned int accumulated = prev[map->bitmap[initial]];
	unsigned int first_avalible = initial;
	unsigned int current = initial;
	
	unsigned int temp_free;  
	
	void* data;
	
	if (size == 0)
		return NULL;
		
	while(accumulated < desired_bitcount) {
		
		//This detects and uses free bits inside a byte
		if (desired_bitcount <= BITS_PR_BYTE && spu_memory_free_bit_count[map->bitmap[current]] >= desired_bitcount)
		{
			//Find the first matching sequence
			for(i = 0; i < BITS_PR_BYTE - (desired_bitcount - 1); i++)
				if (direction > 0) {
					temp_free = spu_memory_lead_bits[desired_bitcount] >> i;
					if (~(~(temp_free) | map->bitmap[current]) == temp_free)
						return (void*)(map->offset + ((current * BITS_PR_BYTE + i) * ALIGN_SIZE_COUNT));
				} else {
					temp_free = spu_memory_trail_bits[desired_bitcount] << i;
					if (~(~(temp_free) | map->bitmap[current]) == temp_free)
						return (void*)(map->offset + (((current + 1) * BITS_PR_BYTE - (i + desired_bitcount)) * ALIGN_SIZE_COUNT));
				}
			
			printf(WHERESTR "Failure, desired_bitcount: %d, free bit count: %d, bitmap: %d, temp_free: %d\n", WHEREARG, desired_bitcount, spu_memory_free_bit_count[map->bitmap[current]], map->bitmap[current], temp_free);

			for(i = 0; i < BITS_PR_BYTE - desired_bitcount; i++)
				if (direction > 0) {
					printf(WHERESTR "Attempt %d: %d -> %d\n", WHEREARG, i, (spu_memory_lead_bits[desired_bitcount] >> i), spu_memory_free_bit_count[(spu_memory_lead_bits[desired_bitcount] >> i) | map->bitmap[current]]);
				} else {
					printf(WHERESTR "Attempt %d: %d -> %d\n", WHEREARG, i, (spu_memory_trail_bits[desired_bitcount] << i), spu_memory_free_bit_count[(spu_memory_trail_bits[desired_bitcount] << i) | map->bitmap[current]]);
				}
			
			REPORT_ERROR("Free bit count was OK, but no free bits were found?");
			return NULL;
		}
		
		current += direction;
		
		if (accumulated == 0)
			first_avalible += direction;
		
		//No more space, current can never be negative, so it becomes UINT_MAX, which is larger than size
		if (current >= map->size)
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
	
	//Calculate the position in bits	
	if (direction > 0)
		position = (first_avalible * BITS_PR_BYTE) + (BITS_PR_BYTE - spu_memory_trail_bit_count[map->bitmap[first_avalible]]);
	else
	{
		desired_bitcount %= BITS_PR_BYTE;
		if (desired_bitcount > spu_memory_lead_bit_count[map->bitmap[first_avalible]])
			desired_bitcount = BITS_PR_BYTE - spu_memory_lead_bit_count[map->bitmap[first_avalible]];
		else
			desired_bitcount = spu_memory_lead_bit_count[map->bitmap[first_avalible]] - desired_bitcount;
		
		position = ((current) * BITS_PR_BYTE) + (desired_bitcount);
	}
	
	//Convert to an actual pointers
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
	
	//It is not possible for the bitmap to be updated, unless it is allocated.
	//Settting the pointer to the end of the current allocation should thus always be safe
	if (bitsize > SIZE_THRESHOLD) {
		if (spu_memory_free_bit_count[map->bitmap[map->last_free]] == 0)
			map->last_free = ((((unsigned int)data) - ALIGN_SIZE_COUNT) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
	} else {
		if (spu_memory_free_bit_count[map->bitmap[map->first_free]] == 0)
			map->first_free = (((((unsigned int)data) + 0) + (bitsize * ALIGN_SIZE_COUNT)) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
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
			map->last_free = MIN(newguess, map->size - 1);  
	} else {
		newguess = (((unsigned int)data) - map->offset) / ALIGN_SIZE_COUNT / BITS_PR_BYTE;
		if (newguess < map->first_free)
			map->first_free = MAX(newguess, 0);  
	}
	
}

void spu_memory_destroy(SPU_Memory_Map* map) {
	free(map->bitmap);
	map->bitmap = NULL;
	g_hash_table_destroy(map->allocated);
	map->allocated = NULL;
	free(map);
}

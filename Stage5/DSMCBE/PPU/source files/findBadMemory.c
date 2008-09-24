#include <stdio.h>
#include <glib.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include <pthread.h>
#include <libspe2.h>
#include "../../common/debug.h"

GHashTable* fbmAllocatedMemory;
GHashTable* fbmAllocatedObjects; 

pthread_mutex_t fbmMutex;

char initialized = FALSE;

void fbminit()
{
	printf("Starting to init findBadMemory\n");
	if (!initialized)
	{
		fbmAllocatedMemory = g_hash_table_new(NULL, NULL);
		fbmAllocatedObjects = g_hash_table_new(NULL, NULL);
		pthread_mutex_init(&fbmMutex, NULL);
		initialized = TRUE;
	}
}

void* fbmMalloc(unsigned int size, char* str, unsigned int number)
{	
	if (!initialized)
	{
		REPORT_ERROR("Call fbminit first");
		sleep(10);
		return;
	}
	
	if (size <= 0)
	{
		REPORT_ERROR("Trying to malloc with size 0");
		sleep(10);
	}
		
		
	void* ptr = malloc(size + 1024 + 1024);
	
	if(ptr == NULL)
	{
		REPORT_ERROR("mallco returned null");
		sleep(10);
	}
	
	size_t i;
	if (ptr != NULL)
	{
		pthread_mutex_lock(&fbmMutex);
		g_hash_table_insert(fbmAllocatedMemory, ptr, (void*)size);
		g_hash_table_insert(fbmAllocatedObjects, ptr, (void*)number);
		pthread_mutex_unlock(&fbmMutex);
	}
		
	for(i = 0; i < (1024 + 1024 + size); i++)
	{
		((char*)ptr)[i] = 12;
	}	
	
	return ptr + 1024;
}

void fbmFree(void* ptr, char* str, unsigned int number)
{	
	if (!initialized)
	{
		REPORT_ERROR("Call fbminit first");
		sleep(10);
		return;
	}
	
	ptr -= 1024;
	unsigned int tail = 0, head = 0;
	pthread_mutex_lock(&fbmMutex);
	unsigned int size = (unsigned int)g_hash_table_lookup(fbmAllocatedMemory, ptr);
	if (size == 0)
		fprintf(stderr, WHERESTR "The pointer %u was not allocated with MALLOC\n", str, number, (unsigned int)ptr);
	
	size_t i;
		
	for(i = 0; i < 1024; i++)
	{
		if (((char*)ptr)[i] != 12)
			head++;
	}
	
	for(i = (size + 1024); i < (1024 + 1024 + size); i++)
	{
		if (((char*)(ptr))[i] != 12)
			tail++;
	}	
	
	if (tail > 0 || head > 0)
	{
		fprintf(stderr, WHERESTR "Free pointer %u allocated (malloc) at line %u, was overridden with head: %u and tail %u\n", str, number, (unsigned int)ptr, (unsigned int)g_hash_table_lookup(fbmAllocatedObjects, ptr), head, tail);
		sleep(10);
	}
			
	g_hash_table_remove(fbmAllocatedMemory, ptr);
	g_hash_table_remove(fbmAllocatedObjects, ptr);
	pthread_mutex_unlock(&fbmMutex);
	free(ptr);
}

void* fbmMallocAlign(unsigned int size, unsigned int offset, char* str, unsigned int number)
{	
	if (!initialized)
	{
		REPORT_ERROR("Call fbminit first");
		sleep(10);
		return;
	}
	
	if (size <= 0)
	{
		REPORT_ERROR("Trying to malloc with size 0");
		sleep(10);
	}
		
		
	void* ptr = _malloc_align(size + 1024 + 1024, offset);
	
	if(ptr == NULL)
	{
		REPORT_ERROR("mallco returned null");
		sleep(10);
	}
	
	size_t i;
	if (ptr != NULL)
	{
		pthread_mutex_lock(&fbmMutex);
		g_hash_table_insert(fbmAllocatedMemory, ptr, (void*)size);
		g_hash_table_insert(fbmAllocatedObjects, ptr, (void*)number);
		pthread_mutex_unlock(&fbmMutex);
	}
	
	for(i = 0; i < (1024 + 1024 + size); i++)
	{
		((char*)ptr)[i] = 12;
	}	
	
	return ptr + 1024;
}

void fbmFreeAlign(void* ptr, char* str, unsigned int number)
{	
	if (!initialized)
	{
		REPORT_ERROR("Call fbminit first");
		sleep(10);
		return;
	}
	
	pthread_mutex_lock(&fbmMutex);
	
	ptr -= 1024;
	unsigned int tail = 0, head = 0;
	unsigned int size = (unsigned int)g_hash_table_lookup(fbmAllocatedMemory, ptr);
	if (size == 0)
		fprintf(stderr, WHERESTR "The pointer %u was not allocated with MALLOC\n", str, number, (unsigned int)ptr);
	
	size_t i;
		
	for(i = 0; i < 1024; i++)
	{
		if (((char*)ptr)[i] != 12)
			head++;
	}
	
	for(i = (size + 1024); i < (1024 + 1024 + size); i++)
	{
		if (((char*)(ptr))[i] != 12)
			tail++;
	}	
	
	if (tail > 0 || head > 0)
	{
		fprintf(stderr, WHERESTR "Free pointer %u allocated (malloc) at line %u, was overridden with head: %u and tail %u\n", str, number, (unsigned int)ptr, (unsigned int)g_hash_table_lookup(fbmAllocatedObjects, ptr), head, tail);
		sleep(10);
	}
			
	g_hash_table_remove(fbmAllocatedMemory, ptr);
	g_hash_table_remove(fbmAllocatedObjects, ptr);
	
	pthread_mutex_unlock(&fbmMutex);
	
	_free_align(ptr);
}

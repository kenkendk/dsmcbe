#include "../../DSMCBE/dsmcbe_spu.h"
#include "../../DSMCBE/common/debug.h"
#include "../PPU/Shared.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#ifdef GRAPHICS
#define EXTRA_WAIT 1
#else
#define EXTRA_WAIT 0
#endif

unsigned int rc;
double deltasum;
double delta;
unsigned int map_width;
unsigned int map_height;
unsigned int red_round;
unsigned int sharedCount;
double epsilon;

unsigned int spu_count;
unsigned int spu_no;


//#define GET_MAP_VALUE(x,y) (((y) < adjustTop && firstRows != NULL) ? &firstRows[((y) * width) + x] : (((y) > height - 3 && lastRows != NULL) ? &lastRows[(((y) - 1 - height) * width) + x] : &data[(((y) - adjustTop) * width) + x]))  
#define GET_MAP_VALUE(x,y) findValue(x, y, isFirst, isLast, width, height, firstRows, lastRows, data)

//#define CHECK_BOUNDS(arr, index, sz) if (((unsigned int)(arr) > (unsigned int)(&arr[index])) || (((unsigned int)(&arr[index]) > ((unsigned int)(arr)) + sz))) REPORT_ERROR("Outside bounds!"); 
#define CHECK_BOUNDS(arr, index, sz)

PROBLEM_DATA_TYPE* findValue(unsigned int x, unsigned int y, unsigned int isFirst, unsigned int isLast, unsigned int width, unsigned int height, PROBLEM_DATA_TYPE* firstRows, PROBLEM_DATA_TYPE* lastRows, PROBLEM_DATA_TYPE* data)
{
	
	if (!isFirst && y < 2)
		return &firstRows[y * width + x];
	else if (!isLast && y >= height - 2)
		return &lastRows[(y - (height - 2)) * width + x];
	else if (!isFirst)
		return &data[(y - 2) * width + x];
	else
		return &data[y * width + x];
}


void solve(struct Work_Unit* work_item)
{
	printf(WHERESTR "SPU %d is solving %d\n", WHEREARG, spu_no, work_item->block_no);
	unsigned int x, y;
	unsigned int height, width;
	unsigned long firstSize;
	unsigned long lastSize;
	PROBLEM_DATA_TYPE* data;
	PROBLEM_DATA_TYPE old, new;
	double itemDelta = 0.0;

	PROBLEM_DATA_TYPE* firstRows = NULL;
	PROBLEM_DATA_TYPE* lastRows = NULL;

	height = work_item->heigth;
	width = map_width;
    
	unsigned int isFirst = 1;
	unsigned int isLast = 1;

	data = (PROBLEM_DATA_TYPE*)&work_item->problem;

	isFirst = work_item->block_no == 0;
	isLast = work_item->block_no >= sharedCount;
	
	if (!isFirst)
	{
	    //printf(WHERESTR "SPU %d is fetching top line %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
		firstRows = acquire(SHARED_ROW_OFFSET + work_item->block_no - 1, &firstSize, ACQUIRE_MODE_WRITE);
	}
	
	if (!isFirst)
		height += 2;
	if (!isLast)
		height += 2;
		
	//printf(WHERESTR "Start: %d, height: %d, adjusted height: %d, total height: %d, isFirst: %d, isLast: %d, red: %d\n", WHEREARG, work_item->line_start, work_item->heigth, height, map_height, isFirst, isLast, red_round);
   	
    //printf(WHERESTR "SPU %d is active (%d:%d)\r\n", WHEREARG, spu_no, width, height);
    //printf(WHERESTR "Epsilon: %lf\r\n", WHEREARG, epsilon);

	if (red_round)
	{
		//printf(WHERESTR "SPU %d is solving red values\n", WHEREARG, spu_no);
		// Update red values
		for(y = 1; y < height - 1; y++)
		{
			if (!isLast && y == height - 3)
			{
				//printf(WHERESTR "SPU %d fetching lastrow %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no);
				lastRows = acquire(SHARED_ROW_OFFSET + work_item->block_no, &lastSize, ACQUIRE_MODE_WRITE);
			}
			
			if (firstRows != NULL && y == 3)
			{
			    //printf(WHERESTR "SPU %d is releasing top line %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
				release(firstRows);
				firstRows = NULL;
			}
			
			for (x = (y%2) + 1; x < width - 1; x=x+2)
			{
				//printf("(%d, %d)\n", x, y);
				old = *GET_MAP_VALUE(x,y);
				new = 0.2 * (
				    *GET_MAP_VALUE(x,y) + 
				    *GET_MAP_VALUE(x,y-1) + 
				    *GET_MAP_VALUE(x,y+1) + 
				    *GET_MAP_VALUE(x-1,y) + 
				    *GET_MAP_VALUE(x+1,y));
				*GET_MAP_VALUE(x,y) = new;
				itemDelta += ABS(old-new);
				rc++;
			}
		}
	} 
	else 
	{
		//printf(WHERESTR "SPU %d is solving black values\n", WHEREARG, spu_no);
		// Update black values
		for(y = 1; y < height - 1; y++)
		{
			if (!isLast && y == height - 3)
			{
				//printf(WHERESTR "SPU %d reading lastrow %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no);
				lastRows = acquire(SHARED_ROW_OFFSET + work_item->block_no, &lastSize, ACQUIRE_MODE_WRITE);
			}
			
			if (firstRows != NULL && y == 3)
			{
			    //printf(WHERESTR "SPU %d is releasing top line %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
				release(firstRows);
				firstRows = NULL;
			}

			for (x = 2 - (y%2); x < width - 1; x=x+2)
			{
				//printf("(%d, %d)\n", x, y);
				old = *GET_MAP_VALUE(x,y);
				new = 0.2 * (
				    *GET_MAP_VALUE(x,y) + 
				    *GET_MAP_VALUE(x,y-1) + 
				    *GET_MAP_VALUE(x,y+1) + 
				    *GET_MAP_VALUE(x-1,y) + 
				    *GET_MAP_VALUE(x+1,y));
				*GET_MAP_VALUE(x,y) = new;
				itemDelta += ABS(old-new);
				rc++;
			}
		}	
	}

    //printf(WHERESTR "Epsilon: %lf\r\n", WHEREARG, epsilon);
	
	if (lastRows != NULL)
	{
		//printf(WHERESTR "SPU %d releasing lastrow %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no);
		release(lastRows);
	}

	if (firstRows != NULL)
	{
		//printf(WHERESTR "SPU %d releasing firstRow %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
		release(firstRows);
	}

	//printf(WHERESTR "Delta for %s workblock %d was %lf\n", WHEREARG, red_round ? "red" : "black", work_item->block_no, itemDelta);
	
	delta += itemDelta;
}

int main(long long id)
{
    struct Work_Unit* work_item;
    struct Barrier_Unit* barrier;
    unsigned long size;
    size_t i;
    unsigned int prefetch, prefetch_temp;
    
    initialize();
    
    rc = 0;
    delta = 0.0;
    deltasum = 0.0;
    

	struct Assignment_Unit* boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_WRITE);
	spu_no = boot->spu_no;
	spu_count = boot->spu_count;
	map_width = boot->map_width;
	map_height = boot->map_height;
	epsilon = boot->epsilon;
	sharedCount = boot->sharedCount;

	unsigned int firstJob = boot->next_job_no;
	unsigned int jobCount = boot->job_count;
	if (firstJob + jobCount > boot->maxjobs)
		jobCount = boot->maxjobs - firstJob;
	
	boot->spu_no++;
	boot->next_job_no += jobCount;
	
	//printf(WHERESTR "SPU %d has %d jobs, starting at %d\n", WHEREARG, spu_no, jobCount, firstJob); 
	
	release(boot);

	delta = epsilon + 1;

	while(delta > epsilon)
	{
		delta = 0.0;

		//printf(WHERESTR "SPU %d is at red\n", WHEREARG, spu_no);
		//Red round
		red_round = 1;
		
		prefetch = beginAcquire(WORK_OFFSET + firstJob + i, ACQUIRE_MODE_WRITE);
		
		for(i = 0; i < jobCount; i++)
		{
		    if (i + 1 < jobCount)
		    	prefetch_temp = beginAcquire(WORK_OFFSET + firstJob + i + 1, ACQUIRE_MODE_WRITE);
		    work_item = endAsync(prefetch, &size);
		    prefetch = prefetch_temp;
	    	solve(work_item);
			release(work_item);
		}
		
		//printf(WHERESTR "SPU %d is at red barrier\n", WHEREARG, spu_no);
		//Wait
		acquireBarrier(EX_BARRIER_1);
		
		//printf(WHERESTR "SPU %d is at black\n", WHEREARG, spu_no);
		//Black round
		red_round = 0;

		prefetch = beginAcquire(WORK_OFFSET + firstJob + i, ACQUIRE_MODE_WRITE);

		for(i = 0; i < jobCount; i++)
		{
		    if (i + 1 < jobCount)
		    	prefetch_temp = beginAcquire(WORK_OFFSET + firstJob + i + 1, ACQUIRE_MODE_WRITE);
		    work_item = endAsync(prefetch, &size);
		    prefetch = prefetch_temp;
	    	solve(work_item);
			release(work_item);
		}
		
		//printf(WHERESTR "SPU %d is at black lock\n", WHEREARG, spu_no);
		//Gather delta sum
		barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_WRITE);
	
		if (barrier->lock_count == 0)
			barrier->delta = delta;
		else
			barrier->delta += delta;

		//Update graphics or report status			
#ifdef GRAPHICS
		if (barrier->lock_count == spu_count)
		{
#else
		if (barrier->lock_count == spu_count - 1)
		{
			barrier->print_count++;
			if (barrier->print_count == 100)
			{
				barrier->print_count = 0;
				printf(WHERESTR "SPU %d is reporting delta: %lf\n", WHEREARG, spu_no, barrier->delta);
			}
#endif		
			//Reset lock for next round
			barrier->lock_count = 0;
		}
		else
			barrier->lock_count++;
			
		release(barrier);
		
		//printf(WHERESTR "SPU %d is at black barrier\n", WHEREARG, spu_no);
		acquireBarrier(EX_BARRIER_2);	
		
		barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		while(barrier->lock_count != 0)
		{
#ifndef GRAPHICS
			printf(WHERESTR "SPU %d reported bad timings\n", WHEREARG, spu_no);
#endif
			//We should not get here unless were are displaying graphics
			release(barrier);
			barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		}
		delta = barrier->delta;
		release(barrier);
	}
	
	struct Results* res = create(RESULT_OFFSET + spu_no, sizeof(struct Results));
	res->deltaSum = deltasum;
	res->rc = rc;
	release(res);
	
	printf(WHERESTR "SPU %d is terminating, delta was: %lf, epsilon was: %lf\n", WHEREARG, spu_no, delta, epsilon);
	terminate();	
	//printf(WHERESTR "SPU %d is done\n", WHEREARG, spu_no);
	  
	return id;  
}


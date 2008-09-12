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
double epsilon;

#define GET_MAP_VALUE(x,y) (((y) < adjustTop && firstRows != NULL) ? &firstRows[((y) * width) + x] : (((y) > height - 3 && lastRows != NULL) ? &lastRows[(((y) - 1 - height) * width) + x] : &data[(((y) - adjustTop) * width) + x]))  
	 
	 


void solve(struct Work_Unit* work_item)
{
	//printf(WHERESTR "SPU %d is solving\n", WHEREARG, spu_no);
	unsigned int x, y;
	unsigned int height, width;
	PROBLEM_DATA_TYPE* data;
	PROBLEM_DATA_TYPE old, new;

	PROBLEM_DATA_TYPE* firstRows = NULL;
	PROBLEM_DATA_TYPE* lastRows = NULL;

	// Don't update last row and last column
	height = work_item->heigth ;
	width = map_width;
    //data = (*work_item).problem;
    
    unsigned int acquireLastAt = UINT_MAX;
    unsigned int releaseFirstAt = UINT_MAX;
    unsigned int adjustTop = 0;
    
    //printf(WHERESTR "SPU %d is creating lines\n", WHEREARG, spu_no);
	data = (PROBLEM_DATA_TYPE*)&work_item->problem;
	
	if (work_item->line_start != 0)
	{
		firstRows = acquire(SHARED_ROW_OFFSET + work_item->block_no, NULL, ACQUIRE_MODE_WRITE);
		releaseFirstAt = 2;
		adjustTop = 2;
	}

	if (work_item->line_start + work_item->heigth < map_height)
		acquireLastAt = work_item->heigth - 3;
    	
    //printf(WHERESTR "SPU %d has created lines (%d:%d)", WHEREARG, spu_no, width, height);

	if (red_round)
	{
		//printf(WHERESTR "SPU %d is solving red values\n", WHEREARG, spu_no);
		// Update red values
		for(y = 1; y < height + 1; y++)
		{
			if (y == acquireLastAt)
				lastRows = acquire(SHARED_ROW_OFFSET + work_item->block_no + 1, NULL, ACQUIRE_MODE_WRITE);
			
			if (y == releaseFirstAt)
			{
				release(firstRows);
				firstRows = NULL;
			}
			
			for (x = (y%2) + 1; x < width - 1; x=x+2)
			{
				old = *GET_MAP_VALUE(x,y);
				new = 0.2 * (
				    *GET_MAP_VALUE(x,y) + 
				    *GET_MAP_VALUE(x,y-1) + 
				    *GET_MAP_VALUE(x,y+1) + 
				    *GET_MAP_VALUE(x-1,y) + 
				    *GET_MAP_VALUE(x+1,y));
				*GET_MAP_VALUE(x,y) = new;
				delta += ABS(old-new);
				rc++;
			}
		}
	} 
	else 
	{
		//printf(WHERESTR "SPU %d is solving black values\n", WHEREARG, spu_no);
		// Update black values
		for(y = 1; y < height + 1; y++)
		{
			if (y == acquireLastAt)
				lastRows = acquire(SHARED_ROW_OFFSET + work_item->block_no + 1, NULL, ACQUIRE_MODE_WRITE);

			if (y == releaseFirstAt)
			{
				release(firstRows);
				firstRows = NULL;
			}

			for (x = 2 - (y%2); x < width - 1; x=x+2)
			{
				old = MAPVALUE(x,y);
				new = 0.2 * (
				    *GET_MAP_VALUE(x,y) + 
				    *GET_MAP_VALUE(x,y-1) + 
				    *GET_MAP_VALUE(x,y+1) + 
				    *GET_MAP_VALUE(x-1,y) + 
				    *GET_MAP_VALUE(x+1,y));
				*GET_MAP_VALUE(x,y) = new;
				delta += ABS(old-new);
				rc++;
			}
		}	
	}
	
	if (lastRows != NULL)
		release(lastRows);

}

int main(long long id)
{
    struct Work_Unit* work_item;
    struct Job_Control* job;
    struct Barrier_Unit* barrier;
    unsigned long size;
    unsigned int spu_no;
    unsigned int spu_count;
    unsigned int jobno;
    
    initialize();
    
    rc = 0;
    delta = 0.0;
    deltasum = 0.0;
    
	//printf(WHERESTR "SPU %d is booting\n", WHEREARG, spu_no);

	struct Assignment_Unit* boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_WRITE);
	spu_no = boot->spu_no;
	spu_count = boot->spu_count;
	map_width = boot->map_width;
	map_height = boot->map_height;
	epsilon = boot->epsilon;
	boot->spu_no++;
	release(boot);
	
	while(1)
	{
	
		job = acquire(JOB_LOCK, &size, ACQUIRE_MODE_WRITE);
		jobno = job->nextjob;
		red_round = job->red_round;
		
		if (job->nextjob >= job->count)
		{
			release(job);
			if (!red_round)
			{
				barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_WRITE);
	
				if (barrier->lock_count == 0)
					barrier->delta = delta;
				else
					barrier->delta += delta;
	
#ifdef GRAPHICS
				if (barrier->lock_count == spu_count)
#else
				if (barrier->lock_count == spu_count - 1)
#endif		
					barrier->lock_count = 0;
				else
					barrier->lock_count++;
	
				while(barrier->lock_count > 0)
				{ 
					release(barrier);
					barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
				}
				delta = barrier->delta;
				release(barrier);
				
				if (delta < epsilon)
					break;
			}
			else		
				acquireBarrirer(EX_BARRIER_1);
			
	
			job = acquire(JOB_LOCK, &size, ACQUIRE_MODE_WRITE);
		}
		
		jobno = job->nextjob;
		job->nextjob++;
		
		if (job->nextjob == job->count)
			job->red_round = !job->red_round;
		
		release(job);
		
		
		//printf(WHERESTR "SPU %d is starting\n", WHEREARG, spu_no);
		    
	    work_item = acquire(WORK_OFFSET + jobno, &size, ACQUIRE_MODE_WRITE);
	    solve(work_item);
		release(work_item);
	}
	
	struct Results* res = create(RESULT_OFFSET + spu_no, sizeof(struct Results));
	res->deltaSum = delta;
	res->rc = rc;
	release(res);
	
	//printf(WHERESTR "SPU %d is terminating\n", WHEREARG, spu_no);
	terminate();	
	//printf(WHERESTR "SPU %d is done\n", WHEREARG, spu_no);
	  
	return id;  
}


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
unsigned int jobno;


//#define GET_MAP_VALUE(x,y) (((y) < adjustTop && firstRows != NULL) ? &firstRows[((y) * width) + x] : (((y) > height - 3 && lastRows != NULL) ? &lastRows[(((y) - 1 - height) * width) + x] : &data[(((y) - adjustTop) * width) + x]))  
#define GET_MAP_VALUE(x,y) findValue(x, y, isFirst, isLast, width, height, firstRows, lastRows, data, dataSize, firstSize, lastSize, work_item->line_start)

//#define CHECK_BOUNDS(arr, index, sz) if (((unsigned int)(arr) > (unsigned int)(&arr[index])) || (((unsigned int)(&arr[index]) > ((unsigned int)(arr)) + sz))) REPORT_ERROR("Outside bounds!"); 
#define CHECK_BOUNDS(arr, index, sz)

PROBLEM_DATA_TYPE* findValue(unsigned int x, unsigned int y, unsigned int isFirst, unsigned int isLast, unsigned int width, unsigned int height, PROBLEM_DATA_TYPE* firstRows, PROBLEM_DATA_TYPE* lastRows, PROBLEM_DATA_TYPE* data, unsigned long dataSize, unsigned long firstSize, unsigned long lastSize, unsigned int startLine)
{
	unsigned int print = 0;//(y < 4) || (y > height - 4);
	
	if (!isFirst && y < 2)
	{
		if (print)
			printf("Mapped (%d, %d) to firstRows (%d)\n", x, y, y);
		
		//CHECK_BOUNDS(firstRows, y * width + x, firstSize);
		return &firstRows[y * width + x];
	}
	else if (!isLast && y >= height - 2)
	{
		if (print)
			printf("Mapped (%d, %d) to lastRows (%d)\n", x, y, (y - (height - 2)));

		//CHECK_BOUNDS(lastRows, (y - (height - 2)) * width + x, lastSize);
		return &lastRows[(y - (height - 2)) * width + x];
	}
	else if (!isFirst)
	{
		/*if (((startLine + (y - 2) == 485) || (startLine + (y - 2) == 486)) && x >= 124)
			printf(WHERESTR "Value at (%d,%d) is: %lf\n", WHEREARG, x, startLine + (y - 2), data[(y - 2) * width + x]);*/
			 
		if (print)
			printf("Mapped (%d, %d) to data (%d)\n", x, y, y - 2);
		//CHECK_BOUNDS(data, (y - 2) * width + x, dataSize);
		return &data[(y - 2) * width + x];
	}
	else
	{
		if (print)
			printf("Mapped (%d, %d) to data (%d)\n", x, y, y);
		//CHECK_BOUNDS(data, y * width + x, dataSize);
		return &data[y * width + x];
	}
}


void solve(struct Work_Unit* work_item, unsigned long worksize)
{
	//printf(WHERESTR "SPU %d is solving %d\n", WHEREARG, spu_no, jobno);
	unsigned int x, y;
	unsigned int height, width;
	unsigned long firstSize;
	unsigned long lastSize;
	PROBLEM_DATA_TYPE* data;
	PROBLEM_DATA_TYPE old, new;

	PROBLEM_DATA_TYPE* firstRows = NULL;
	PROBLEM_DATA_TYPE* lastRows = NULL;

	height = work_item->heigth ;
	width = map_width;
    
	unsigned int isFirst = 1;
	unsigned int isLast = 1;
	unsigned int dataSize = work_item->buffer_size * sizeof(PROBLEM_DATA_TYPE);

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
				delta += ABS(old-new);
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
	
}

int main(long long id)
{
    struct Work_Unit* work_item;
    struct Job_Control* job;
    struct Barrier_Unit* barrier;
    unsigned long size;
    
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
	boot->spu_no++;
	release(boot);
	delta = epsilon + 1.0;

	//printf(WHERESTR "SPU %d is booting\n", WHEREARG, spu_no);
	
	while(1)
	{
	
		//printf(WHERESTR "SPU %d is waiting for job lock\n", WHEREARG, spu_no);
		job = acquire(JOB_LOCK, &size, ACQUIRE_MODE_WRITE);
		jobno = job->nextjob;
		
		while (job->nextjob >= job->count)
		{
			//printf(WHERESTR "SPU %d is releasing job lock\n", WHEREARG, spu_no);
			release(job);


			if (!red_round)
			{
				//printf(WHERESTR "SPU %d is waiting for manual barrier\n", WHEREARG, spu_no);
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

				//printf(WHERESTR "SPU %d is waiting for manual barrier, count: %d\n", WHEREARG, spu_no, barrier->lock_count);

	
				while(barrier->lock_count != 0)
				{ 
					release(barrier);
					barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
					//sleep(2);
					//printf(WHERESTR "SPU %d is waiting for manual barrier, count: %d\n", WHEREARG, spu_no, barrier->lock_count);

				}
				delta = barrier->delta;
				release(barrier);
				
				if (delta < epsilon)
					break;
			}
			else
			{
				//printf(WHERESTR "SPU %d is waiting for barrier\n", WHEREARG, spu_no);
				acquireBarrirer(EX_BARRIER_1);
			}
			
	
			//printf(WHERESTR "SPU %d is fetching job\n", WHEREARG, spu_no);
			job = acquire(JOB_LOCK, &size, ACQUIRE_MODE_WRITE);
			//TODO: If there is just one job and two SPU's, this does not work...
			if (job->nextjob == job->count)
			{
				job->nextjob = 0;
				job->red_round = !job->red_round;
			}
		}
		
		if (delta < epsilon)
			break;
		
		//printf(WHERESTR "SPU %d got job %d, red: %d\n", WHEREARG, spu_no, job->nextjob, job->red_round);
		jobno = job->nextjob;
		red_round = job->red_round;
		job->nextjob++;
		
		release(job);
		
		
		//printf(WHERESTR "SPU %d is starting\n", WHEREARG, spu_no);
		    
		//printf(WHERESTR "SPU %d is fetching work %d\n", WHEREARG, spu_no, WORK_OFFSET + jobno);
	    work_item = acquire(WORK_OFFSET + jobno, &size, ACQUIRE_MODE_WRITE);
	    solve(work_item, size);
		release(work_item);
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


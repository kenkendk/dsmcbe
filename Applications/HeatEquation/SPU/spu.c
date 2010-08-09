#include <dsmcbe_spu.h>
#include <debug.h>
#include "../PPU/Shared.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

//UPDATE_DELTA requires a global lock, so it cannot be used with INDIVIDUAL_BARRIERS

//#define UPDATE_DELTA
#define INDIVIDUAL_BARRIERS
#define PRINT_PROGRESS_COUNT 100

#ifdef GRAPHICS
	#define EXTRA_WAIT 1
	#ifndef UPDATE_DELTA 
		#define UPDATE_DELTA
	#endif
	#ifdef INDIVIDUAL_BARRIERS
		#undef INDIVIDUAL_BARRIERS
	#endif
#else
	#define EXTRA_WAIT 0
#endif

#ifdef UPDATE_DELTA
	#ifdef INDIVIDUAL_BARRIERS
		#undef INDIVIDUAL_BARRIERS
	#endif
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

#define PROGRESS(x1, y1, y2, y3) //printf(x1, y1, y2, y3)

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


void solve(struct Work_Unit* work_item, PROBLEM_DATA_TYPE** fr)
{
	//printf(WHERESTR "SPU %d is solving %d\n", WHEREARG, spu_no, work_item->block_no);
	unsigned int x, y;
	unsigned int height, width;
	unsigned long lastSize;
	PROBLEM_DATA_TYPE* data;
	PROBLEM_DATA_TYPE old, new;
	double itemDelta = 0.0;

	PROBLEM_DATA_TYPE* lastRows = NULL;
	PROBLEM_DATA_TYPE* firstRows = *fr;

	height = work_item->heigth;
	width = map_width;
    
	unsigned int isFirst;
	unsigned int isLast;

	data = (PROBLEM_DATA_TYPE*)&work_item->problem;

	isFirst = work_item->block_no == 0;
	isLast = work_item->block_no >= sharedCount;
	
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
				lastRows = dsmcbe_acquire(SHARED_ROW_OFFSET + work_item->block_no, &lastSize, ACQUIRE_MODE_WRITE);
			}
			
			if (firstRows != NULL && y == 3)
			{
			    //printf(WHERESTR "SPU %d is releasing top line %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
				dsmcbe_release(firstRows);
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
				lastRows = dsmcbe_acquire(SHARED_ROW_OFFSET + work_item->block_no, &lastSize, ACQUIRE_MODE_WRITE);
			}
			
			if (firstRows != NULL && y == 3)
			{
			    //printf(WHERESTR "SPU %d is releasing top line %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
				dsmcbe_release(firstRows);
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
	
	*fr = lastRows, 
	lastRows = NULL;

    //printf(WHERESTR "Epsilon: %lf\r\n", WHEREARG, epsilon);
	
	if (lastRows != NULL)
	{
		//printf(WHERESTR "SPU %d releasing lastrow %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no);
		dsmcbe_release(lastRows);
	}

	if (firstRows != NULL)
	{
		//printf(WHERESTR "SPU %d releasing firstRow %d\n", WHEREARG, spu_no, SHARED_ROW_OFFSET + work_item->block_no - 1);
		dsmcbe_release(firstRows);
	}

	//printf(WHERESTR "Delta for %s workblock %d was %lf\n", WHEREARG, red_round ? "red" : "black", work_item->block_no, itemDelta);
	
	delta += itemDelta;

	//printf(WHERESTR "SPU %d has solved %d\n", WHEREARG, spu_no, work_item->block_no);
}

int main(long long id)
{
    struct Work_Unit* work_item;
    unsigned long size;
    size_t i, j;
    unsigned int prefetch, prefetch_temp;
    PROBLEM_DATA_TYPE* shared_row;
    unsigned int maxJobs;
	PROBLEM_DATA_TYPE* firstRows;
    
#ifdef UPDATE_DELTA	
    struct Barrier_Unit* barrier;
    unsigned int barrier_alternation = 0;
#endif
    
    dsmcbe_initialize();
    
    rc = 0;
    delta = 0.0;
    deltasum = 0.0;

	struct Assignment_Unit* boot = dsmcbe_acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_WRITE);
	spu_no = boot->spu_no;
	spu_count = boot->spu_count;
	map_width = boot->map_width;
	map_height = boot->map_height;
	epsilon = boot->epsilon;
	sharedCount = boot->sharedCount;
	maxJobs = boot->maxjobs;
	
	//Make sure all items are same size to prevent fragmentation
	unsigned int slice_height = BLOCK_SIZE / (map_width * sizeof(PROBLEM_DATA_TYPE));

	unsigned int firstJob = boot->next_job_no;
	unsigned int jobCount = boot->job_count;
	if (firstJob + jobCount > boot->maxjobs)
		jobCount = boot->maxjobs - firstJob;
	
	boot->spu_no++;
	boot->next_job_no += jobCount;
	
	//printf(WHERESTR "SPU %d has %d jobs, starting at %d, map is: (%d x %d), slice is: %d\n", WHEREARG, spu_no, jobCount, firstJob, map_width, map_height, slice_height); 
	
	dsmcbe_release(boot);
	
	for(i = 0; i < jobCount; i++)
	{
		//printf(WHERESTR "Creating %d with items %d and %d\n", WHEREARG, i, WORK_OFFSET + i + firstJob, SHARED_ROW_OFFSET + i + firstJob);
		work_item = dsmcbe_create(WORK_OFFSET + i + firstJob, sizeof(struct Work_Unit) +  sizeof(PROBLEM_DATA_TYPE) * map_width * slice_height);
		
		unsigned int thisHeight;
		
		if (i + firstJob == maxJobs - 1)
			thisHeight = map_height - ((slice_height + 2) * (maxJobs - 1));
		else
			thisHeight = slice_height;
		
		//printf(WHERESTR "Height: %d (%d vs %d)\n", WHEREARG, thisHeight, i + firstJob, maxJobs);
		
		work_item->block_no = i + firstJob;
		work_item->buffer_size = map_width * thisHeight;
		work_item->heigth = thisHeight;
		work_item->line_start = ((slice_height + 2) * (i + firstJob));
		shared_row = &work_item->problem;

		memset(shared_row, 0, work_item->buffer_size * sizeof(PROBLEM_DATA_TYPE));
		
		if (i + firstJob == 0)
		{
			//Top row is 40.0 degrees
			for(j = 0; j < map_width; j++)
				shared_row[j] = 40.0;
		}

		if (i + firstJob == maxJobs - 1)
		{
			//Bottom row is -273.15
			for(j = 0; j < map_width; j++)
				shared_row[j + (map_width * (thisHeight - 1))] = -273.15;
		}
		
		for(j = 0; j < thisHeight; j++)
		{
			//Left side is -273.15, right side is 40.0
			shared_row[0 + (j * map_width)] = -273.15; 
			shared_row[map_width - 1 + (j * map_width)] = 40.0;
		}
		
		dsmcbe_release(work_item);
		
		shared_row = dsmcbe_create(SHARED_ROW_OFFSET + i + firstJob, sizeof(PROBLEM_DATA_TYPE) * map_width * 2);
		memset(shared_row, 0,  sizeof(PROBLEM_DATA_TYPE) * map_width * 2);

		if (i + firstJob == sharedCount)
		{
			shared_row[0] = -273.15; 
			shared_row[map_width - 1] = 40.0;
			 
			//Bottom row is -273.15
			for(j = 0; j < map_width; j++)
				shared_row[j + map_width] = -273.15;
		}
		else
		{
			shared_row[0] = shared_row[map_width] = -273.15; 
			shared_row[map_width - 1] = shared_row[(map_width * 2) - 1] = 40.0;
		}
		dsmcbe_release(shared_row);
		

	}
	
#ifdef INDIVIDUAL_BARRIERS	
	//Special shared barrier
	if (spu_no != 0)
		dsmcbe_createBarrier(BARRIER_LOCK_OFFSET + spu_no, 2);
#endif
	
	PROGRESS(WHERESTR "SPU %d is at master lock (%d)\n", WHEREARG, spu_no, ITTERATIONS);
	dsmcbe_release(dsmcbe_acquire(MASTER_START_LOCK, &size, ACQUIRE_MODE_READ));
	PROGRESS(WHERESTR "SPU %d is ready! (%d)\n", WHEREARG, spu_no, ITTERATIONS);

	//printf(WHERESTR "Done creating\n", WHEREARG); 

	delta = epsilon + 1;
	unsigned int itterations = ITTERATIONS;

	while(itterations-- > 0)
	{
		delta = 0.0;

		PROGRESS(WHERESTR "SPU %d is at red %d\n", WHEREARG, spu_no, itterations);
		//Red round
		red_round = 1;
		
		prefetch = dsmcbe_beginAcquire(WORK_OFFSET + firstJob, ACQUIRE_MODE_WRITE);

		if (work_item->block_no != 0)
			firstRows = dsmcbe_acquire(SHARED_ROW_OFFSET + work_item->block_no - 1, &size, ACQUIRE_MODE_WRITE);
			
		for(i = 0; i < jobCount; i++)
		{
		    if (i + 1 < jobCount)
		    	prefetch_temp = dsmcbe_beginAcquire(WORK_OFFSET + firstJob + i + 1, ACQUIRE_MODE_WRITE);
		    work_item = spu_dsmcbe_endAsync(prefetch, &size);
			//printf(WHERESTR "SPU %d fetched %d (%d)\n", WHEREARG, spu_no, i, WORK_OFFSET + firstJob + i);
		    prefetch = prefetch_temp;
	    	solve(work_item, &firstRows);
	    	dsmcbe_release(work_item);
		}
		
		if (firstRows != NULL)
			dsmcbe_release(firstRows);
		
		PROGRESS(WHERESTR "SPU %d is at red barrier %d\n", WHEREARG, spu_no, itterations);
#ifdef INDIVIDUAL_BARRIERS	
		//Wait
		if (spu_no != 0)
			dsmcbe_acquireBarrier(BARRIER_LOCK_OFFSET + spu_no);
		if (spu_no != spu_count - 1)
			dsmcbe_acquireBarrier(BARRIER_LOCK_OFFSET + spu_no + 1);
#else
		dsmcbe_acquireBarrier(EX_BARRIER_1);
#endif
		
		PROGRESS(WHERESTR "SPU %d is at black %d\n", WHEREARG, spu_no, itterations);
		//Black round
		red_round = 0;

		prefetch = dsmcbe_beginAcquire(WORK_OFFSET + firstJob, ACQUIRE_MODE_WRITE);

		if (work_item->block_no != 0)
			firstRows = dsmcbe_acquire(SHARED_ROW_OFFSET + work_item->block_no - 1, &size, ACQUIRE_MODE_WRITE);

		for(i = 0; i < jobCount; i++)
		{
		    if (i + 1 < jobCount)
		    	prefetch_temp = dsmcbe_beginAcquire(WORK_OFFSET + firstJob + i + 1, ACQUIRE_MODE_WRITE);
		    work_item = dsmcbe_endAsync(prefetch, &size);
			//printf(WHERESTR "SPU %d fetched %d (%d)\n", WHEREARG, spu_no, i, WORK_OFFSET + firstJob + i);
		    prefetch = prefetch_temp;
	    	solve(work_item, &firstRows);
	    	dsmcbe_release(work_item);
		}
		
		if (firstRows != NULL)
			dsmcbe_release(firstRows);
		
		
#ifdef UPDATE_DELTA		
		PROGRESS(WHERESTR "SPU %d is at black lock %d\n", WHEREARG, spu_no, itterations);
		//Gather delta sum
		barrier = dsmcbe_acquire(BARRIER_LOCK + barrier_alternation, &size, ACQUIRE_MODE_WRITE);
	
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
#endif /*GRAPHICS*/	
			//Reset lock for next round
			barrier->lock_count = 0;
		}
		else
			barrier->lock_count++;
			
		//printf(WHERESTR "SPU %d is releasing black lock: %d\n", WHEREARG, spu_no, barrier->lock_count);
		dsmcbe_release(barrier);
#endif /*UPDATE_DELTA*/
		
		PROGRESS(WHERESTR "SPU %d is at black barrier %d\n", WHEREARG, spu_no, itterations);
#ifdef INDIVIDUAL_BARRIERS	
		if (spu_no != 0)
			dsmcbe_acquireBarrier(BARRIER_LOCK_OFFSET + spu_no);
		if (spu_no != spu_count - 1)
			dsmcbe_acquireBarrier(BARRIER_LOCK_OFFSET + spu_no + 1);
#else
		dsmcbe_acquireBarrier(EX_BARRIER_2);
#endif

#ifdef UPDATE_DELTA		
		//PROGRESS(WHERESTR "SPU %d is spinning %d\n", WHEREARG, spu_no, itterations);
		barrier = dsmcbe_acquire(BARRIER_LOCK + barrier_alternation, &size, ACQUIRE_MODE_READ);
		//PROGRESS(WHERESTR "SPU %d is spinning val: %d\n", WHEREARG, spu_no, barrier->lock_count);
		while(barrier->lock_count != 0)
		{
#ifndef GRAPHICS
			//This should not happen...
			printf(WHERESTR "SPU %d reported bad timings\n", WHEREARG, spu_no);
			sleep(1);
#endif /*GRAPHICS*/
			//We should not get here unless were are displaying graphics
			dsmcbe_release(barrier);
			barrier = dsmcbe_acquire(BARRIER_LOCK + barrier_alternation, &size, ACQUIRE_MODE_READ);
		}
		delta = barrier->delta;

		if (spu_no == 0 && itterations % PRINT_PROGRESS_COUNT == 0)
			printf(WHERESTR "SPU %d is reporting delta: %lf, round: %d\n", WHEREARG, spu_no, barrier->delta, itterations);

		dsmcbe_release(barrier);
		
		//barrier_alternation = (barrier_alternation + 1) % 1;
#else /*UPDATE_DELTA*/
		if (spu_no == 0 && itterations % PRINT_PROGRESS_COUNT == 0)
			printf(WHERESTR "SPU %d is reporting progress: %d\n", WHEREARG, spu_no, itterations);
		
#endif /*UPDATE_DELTA*/
	}
	
	struct Results* res = dsmcbe_create(RESULT_OFFSET + spu_no, sizeof(struct Results));
	res->deltaSum = deltasum;
	res->rc = rc;
	dsmcbe_release(res);
	
	//printf(WHERESTR "SPU %d is terminating, delta was: %lf, epsilon was: %lf\n", WHEREARG, spu_no, delta, epsilon);
	dsmcbe_terminate();
	//printf(WHERESTR "SPU %d is done\n", WHEREARG, spu_no);
	  
	return id;  
}


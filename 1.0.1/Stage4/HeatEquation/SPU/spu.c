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

void dsmcbe_barrier(GUID id, unsigned int ref_count)
{
	unsigned long size;
	unsigned int* count = acquire(id, &size, ACQUIRE_MODE_WRITE);

	if (++(*count) == ref_count)
		*count = 0;

	while(*count != 0)
	{
		release(count);
		count = acquire(id, &size, ACQUIRE_MODE_READ);
	}
	release(count);
}

// Send last or/and first row to other(s) process
// Recieve last or/and first row from other(s) process	
void ExchangeRows(PROBLEM_DATA_TYPE* data, unsigned int height, unsigned int width, PROBLEM_DATA_TYPE** firstline, PROBLEM_DATA_TYPE** lastline, unsigned int spu_no, unsigned int spu_count)
{
//#define memcpy(a,b,c) /* a b c */

	unsigned long size;
	void* tmp1;
	void* tmp2;
	unsigned int bytewidth = sizeof(PROBLEM_DATA_TYPE) * width;


	/******* STEP 1: Release own copies *******/
	
	//Everyone but the first sends their updated first shared row 
    if (spu_no != 0)
    {
		//printf(WHERESTR "SPU %d is releasing firstline (%d)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no);
		memcpy(*firstline, &data[width], bytewidth);
    	release(*firstline);
    }
    
    //Everyone but the last, sends their second to last row 
    if (spu_no != spu_count - 1)
    {
		//printf(WHERESTR "SPU %d is releasing lastline (%d)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no);
		memcpy(*lastline, &data[(height-2) * width], bytewidth);
    	release(*lastline);
    } 

	//printf(WHERESTR "SPU %d is waiting for barrier (%d)\n", WHEREARG, spu_no, EX_BARRIER_2);
	/******* STEP 2: Acquire other copies *******/
    dsmcbe_barrier(EX_BARRIER_2, spu_count);
	//printf(WHERESTR "SPU %d is out of barrier (%d)\n", WHEREARG, spu_no, EX_BARRIER_2);
    
    //Everyone but the last, reads their updated last row
    if (spu_no != spu_count - 1)
    {
		//printf(WHERESTR "SPU %d is reading firstline (%d)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no + 1);
    	if ((tmp1 = acquire(FIRST_ROW_OFFSET + spu_no + 1, &size, ACQUIRE_MODE_WRITE)) == NULL)
    		printf(WHERESTR "SPU %d failed reading firstline (%d)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no + 1);
    	
    	//memcpy(&data[(height - 1) * width], tmp, bytewidth); 
    	release(tmp1);
		//printf(WHERESTR "SPU %d has updated firstline (%d, %ld)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no + 1, bytewidth);
    }
    
    //Everyone but the first, reads their updated first row
    if (spu_no != 0)
    {
		//printf(WHERESTR "SPU %d is reading lastline (%d)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no - 1);
    	if ((tmp2 = acquire(LAST_ROW_OFFSET + spu_no - 1, &size, ACQUIRE_MODE_WRITE)) == NULL)
			printf(WHERESTR "SPU %d failed reading lastline (%d)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no - 1);
    	memcpy(data, tmp2, bytewidth);
    	release(tmp2);
		//printf(WHERESTR "SPU %d has updated lastline (%d, %ld)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no - 1, bytewidth);
    }
    
	//printf(WHERESTR "SPU %d is waiting for barrier (%d)\n", WHEREARG, spu_no, EX_BARRIER_2);
	/******* STEP 3: Re-acquire own copies *******/
    dsmcbe_barrier(EX_BARRIER_3, spu_count);
	//printf(WHERESTR "SPU %d is out of barrier (%d)\n", WHEREARG, spu_no, EX_BARRIER_2);
    
    if (spu_no != spu_count - 1)
    {
		//printf(WHERESTR "SPU %d is trying to re-acquire lastline (%d)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no);
    	*lastline = acquire(LAST_ROW_OFFSET + spu_no, &size, ACQUIRE_MODE_WRITE);
    	if (*lastline == NULL)
			printf(WHERESTR "SPU %d failed to re-acquire lastline (%d)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no);
    	
    	memcpy(&data[(height - 1) * width], *lastline, bytewidth); 
		//printf(WHERESTR "SPU %d re-acquired lastline (%d, %ld)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no, bytewidth);
    }
    
    if (spu_no != 0)
    {
		//printf(WHERESTR "SPU %d is trying to re-acquire firstline (%d)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no);
    	*firstline = acquire(FIRST_ROW_OFFSET + spu_no, &size, ACQUIRE_MODE_WRITE);
    	if (*firstline == NULL)
			printf(WHERESTR "SPU %d failed to re-acquire firstline (%d)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no);
    	
        memcpy(data, *firstline, bytewidth);
		//printf(WHERESTR "SPU %d re-acquired firstline (%d, %ld)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no, bytewidth);
    }
//#undef memcpy
}

void solve(struct Work_Unit* work_item, unsigned int spu_no, unsigned int spu_count)
{
	unsigned int x, y, cnt, rc;
	double delta, old;
	unsigned int height, width;
	PROBLEM_DATA_TYPE* data;
	double deltasum;
	double epsilon;
	
	PROBLEM_DATA_TYPE* firstline;
	PROBLEM_DATA_TYPE* lastline; 
	unsigned int map_width;
	struct Barrier_Unit* barrier;
	unsigned long size;
	
	map_width = work_item->width;
	rc = 0;
	// Don't update last row and last column
	height = work_item->heigth ;
	width = work_item->width;
    //data = (*work_item).problem;
    
    printf(WHERESTR "SPU %d is creating lines\n", WHEREARG, spu_no);
	data = (PROBLEM_DATA_TYPE*)&work_item->problem;
    
    if (spu_no != 0)
    {
    	firstline = create(FIRST_ROW_OFFSET + spu_no, sizeof(PROBLEM_DATA_TYPE) * width);
	    //printf(WHERESTR "SPU %d created firstline (%d)\n", WHEREARG, spu_no, FIRST_ROW_OFFSET + spu_no);
	    memcpy(firstline, data, sizeof(PROBLEM_DATA_TYPE) * width);
    }
    else
    	firstline = NULL;
    	
    if (spu_no != spu_count - 1)
    {
    	lastline = create(LAST_ROW_OFFSET + spu_no, sizeof(PROBLEM_DATA_TYPE) * width );
	    //printf(WHERESTR "SPU %d created lastline (%d)\n", WHEREARG, spu_no, LAST_ROW_OFFSET + spu_no);
	    memcpy(lastline, &data[width * (height - 1)], sizeof(PROBLEM_DATA_TYPE) * width);
    }
    else
    	lastline = NULL;
    	
    printf(WHERESTR "SPU %d has created lines (%d:%d)", WHEREARG, spu_no, width, height);

    delta = (*work_item).epsilon + 1.0;
    epsilon = (*work_item).epsilon;
    cnt = UPDATE_FREQ - 1;
    deltasum = 0.0;

    while(delta>epsilon)
	{
        delta = 0.0;

		//printf(WHERESTR "SPU %d is solving red values\n", WHEREARG, spu_no);
		// Update red values
		for(y = 1; y < height - 1; y++)
			for (x = (y%2) + 1; x < width - 1; x=x+2)
			{
				old = MAPVALUE(x,y);
				MAPVALUE(x,y) = 0.2 * (
				    MAPVALUE(x,y) + 
				    MAPVALUE(x,y-1) + 
				    MAPVALUE(x,y+1) + 
				    MAPVALUE(x-1,y) + 
				    MAPVALUE(x+1,y));
				delta += ABS(old-MAPVALUE(x,y));
				rc++;
			}

		//printf(WHERESTR "SPU %d is done with red values\n", WHEREARG, spu_no);
       	ExchangeRows(data, height, width, &firstline, &lastline, spu_no, spu_count);

		//printf(WHERESTR "SPU %d is solving black values\n", WHEREARG, spu_no);
		
		// Update black values
		for(y = 1; y < height - 1; y++)
			for (x = 2 - (y%2); x < width - 1; x=x+2)
			{
				old = MAPVALUE(x,y);
				MAPVALUE(x,y) = 0.2 * (
				    MAPVALUE(x,y) + 
				    MAPVALUE(x,y-1) + 
				    MAPVALUE(x,y+1) + 
				    MAPVALUE(x-1,y) + 
				    MAPVALUE(x+1,y));
				delta += ABS(old-MAPVALUE(x,y));
				rc++;
			}	

		//printf(WHERESTR "SPU %d is done with black values\n", WHEREARG, spu_no);

        ExchangeRows(data, height, width, &firstline, &lastline, spu_no, spu_count);
		//printf(WHERESTR "SPU %d is waiting for barrier, delta: %lf\n", WHEREARG, spu_no, delta);

		barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_WRITE);
		if(barrier->lock_count == 0)
			barrier->delta = delta;
		else
			barrier->delta += delta;
		barrier->lock_count++;
		
#ifdef GRAPHICS
		release(work_item);
#endif		
		
		if (barrier->lock_count == spu_count + EXTRA_WAIT)
			barrier->lock_count = 0;
		
		while(barrier->lock_count != 0)
		{
			release(barrier);
			barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		}
		
		release(barrier);

#ifdef GRAPHICS
		work_item = acquire(WORK_OFFSET + spu_no, &size, ACQUIRE_MODE_WRITE);
#endif
        
		delta = barrier->delta;
        deltasum += barrier->delta;
		printf(WHERESTR "SPU %d is out of the barrier (%d), deltasum: %lf, delta: %lf, epsilon: %lf \n", WHEREARG, spu_no, spu_count + EXTRA_WAIT, deltasum, barrier->delta, epsilon);

	}
	
	printf(WHERESTR "SPU %d is done solving\n", WHEREARG, spu_no);
	
	if (firstline != NULL)
		release(firstline);
	if (lastline != NULL)
		release(lastline);
		
	work_item->epsilon = deltasum;
	work_item->width = rc;
}


int main(long long id)
{
    struct Work_Unit* work_item;
    unsigned long size;
    unsigned int spu_no;
    unsigned int spu_count;
    
    initialize();
    
	printf(WHERESTR "SPU %d is booting\n", WHEREARG, spu_no);

	struct Assignment_Unit* boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_WRITE);
	spu_no = boot->spu_no;
	spu_count = boot->spu_count;
	boot->spu_no++;
	release(boot);
	
	printf(WHERESTR "SPU %d is starting\n", WHEREARG, spu_no);
	    
    work_item = acquire(WORK_OFFSET + spu_no, &size, ACQUIRE_MODE_WRITE);

	printf(WHERESTR "SPU %d is solving\n", WHEREARG, spu_no);
	solve(work_item, spu_no, spu_count);
	printf(WHERESTR "SPU %d is finished solving\n", WHEREARG, spu_no);
	
	release(work_item);

	terminate();	
	  
	return id;  
}


#include "StopWatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>
#include "dsmcbe_ppu.h"
#include "common/debug.h"
#include "Shared.h"
#include "graphicsScreen.h"

#ifdef GRAPHICS
void show(PROBLEM_DATA_TYPE* data, unsigned int map_width, unsigned int map_height)
{
	unsigned int color, y, x;
	PROBLEM_DATA_TYPE value;

	gs_clear(WHITE);

	for(y = 0; y < map_height; y++)
        for(x = 0; x < map_width; x++)
		{
            value = MAPVALUE(x, y);
            if (value < 0)
				color = MIN(abs((int)(value)),255);
            else
				color = MIN(255,6*((int)(value)))*256*256; //#Max 40C so we increase
				
			gs_plot(x, y, color);
		}
	
	gs_update();
}
#endif

void UpdateMasterMap(PROBLEM_DATA_TYPE* data, unsigned int map_width, unsigned int rows, unsigned int sharedCount)
{
	size_t i;
	unsigned long size;
	struct Work_Unit* item;
	PROBLEM_DATA_TYPE* temp;
	
	for(i =0; i < rows; i++)
	{
		//printf(WHERESTR "Fetching work row: %d\n", WHEREARG, WORK_OFFSET + i);
		item = acquire(WORK_OFFSET + i, &size, ACQUIRE_MODE_READ);
		
        memcpy(&data[item->line_start * map_width], &item->problem, item->heigth * map_width * sizeof(PROBLEM_DATA_TYPE));

		//printf(WHERESTR "Updated from: %d, height: %d, width: %d\n", WHEREARG, item->line_start, item->heigth, map_width);
		
		/*if (item->line_start + item->heigth > 485)
		{
			size_t j;
			unsigned int y = 485 - item->line_start;
			for(j = 123; j <= 127; j++)
				printf(WHERESTR "Pixel at (%d,%d) is: %lf\n", WHEREARG, j, 485, (&item->problem)[y * map_width + j]);
			y++; 
			for(j = 123; j <= 127; j++)
				printf(WHERESTR "Pixel at (%d,%d) is: %lf\n", WHEREARG, j, 486, (&item->problem)[y * map_width + j]);
		}*/

		if (i < sharedCount)
		{
			//printf(WHERESTR "Fetching shared row: %d\n", WHEREARG, SHARED_ROW_OFFSET + i);
			temp = acquire(SHARED_ROW_OFFSET + i, &size, ACQUIRE_MODE_READ);
			memcpy(&data[(item->line_start + item->heigth) * map_width], temp, map_width * 2 * sizeof(PROBLEM_DATA_TYPE));
	        release(temp);
			//printf(WHERESTR "Updated from: %d, height: %d\n", WHEREARG, item->line_start + item->heigth, 2);
		}

        release(item);
	}
	
	//sleep(1);
	
}


//The coordinator calls this process
void Coordinator(unsigned int map_width, unsigned int map_height, unsigned int spu_count)
{
	size_t i, j;
	unsigned int rc;
	PROBLEM_DATA_TYPE* data;
	PROBLEM_DATA_TYPE* temp;
	char buf[256];
	struct Work_Unit* send_buffer = NULL;
	unsigned long size;
	double deltasum;
#ifdef GRAPHICS
	unsigned int cnt = 0;
	double delta;
#endif
   
   	unsigned int map_size = map_width * map_height;
	double epsilon = 0.001 * (map_width - 1) * (map_height - 1);

	//printf(WHERESTR "Boot and barrier done\n", WHEREARG);

	data = (PROBLEM_DATA_TYPE*)malloc(sizeof(PROBLEM_DATA_TYPE) * map_size);
    memset(data, 0, map_size * sizeof(PROBLEM_DATA_TYPE));

	for( i = 0; i < map_width; i++)
	{
		MAPVALUE(i, 0) = 40.0;
		MAPVALUE(i, map_height - 1) = -273.15;
	}

	for( j = 0; j < map_height; j++)
	{
		MAPVALUE(0, j) = -273.15;
		MAPVALUE(map_width - 1, j) = 40.0;
	}

	unsigned int slice_height = BLOCK_SIZE / (map_width * sizeof(PROBLEM_DATA_TYPE));

#ifdef GRAPHICS
	printf(WHERESTR "Displaying map (%d x %d)\n", WHEREARG, map_width, map_height);
	gs_init(map_width, map_height);	
	show(data, map_width, map_height);
#endif	

	//printf(WHERESTR "Starting timer\n", WHEREARG);

	sw_init();
	sw_start();
	
	unsigned int rows = 0;
	unsigned int sharedCount = 0;
	unsigned int remainingLines = map_height;
	unsigned int lineOffset = 0;
	unsigned int workUnits = 0;
	unsigned int barrier_alternation = 0;
	
	if (DSMCBE_MachineCount() != 0)
		spu_count *= DSMCBE_MachineCount(); 
		
	sharedCount = workUnits = map_height / (slice_height + 2);
	if ((map_height % (slice_height + 2)) > 0)
		workUnits++;
	
	if ((map_height % (slice_height + 2)) > slice_height)
		sharedCount++; 
	

	//Let all SPU's compete for a number
	struct Assignment_Unit* boot = create(ASSIGNMENT_LOCK, sizeof(struct Assignment_Unit));
	boot->map_width = map_width;
	boot->map_height = map_height;
	boot->spu_no = 0;
	boot->spu_count = spu_count;
	boot->epsilon = epsilon;
	boot->next_job_no = 0;
	//TODO: This is may not be the best division possible
	boot->job_count = (workUnits + (spu_count - 1)) / spu_count;
	boot->maxjobs = workUnits;
	boot->sharedCount = sharedCount;

	//printf(WHERESTR "Created %d jobs, and each of the %d SPU's get %d items\n", WHEREARG, boot->maxjobs, spu_count, boot->job_count); 

	release(boot);
	
	sharedCount = 0;
	workUnits = 0;
	
	//Set up the barrier
	struct Barrier_Unit* barrier = create(BARRIER_LOCK, sizeof(struct Barrier_Unit));
	barrier->delta = 0;
	barrier->lock_count = 0;
	barrier->print_count = 0;
	release(barrier);
	
	createBarrier(EX_BARRIER_1, spu_count);
	createBarrier(EX_BARRIER_2, spu_count);
	
	
	while(remainingLines > 0)
	{
		unsigned int thisHeight = MIN(slice_height, remainingLines);
		//printf(WHERESTR "Created work %d, height: %d\n", WHEREARG, WORK_OFFSET + rows, thisHeight);
		
		remainingLines -= thisHeight;

#ifdef CREATE_LOCALLY
		send_buffer = acquire(WORK_OFFSET + rows, &size, ACQUIRE_MODE_WRITE);
		if (size != (sizeof(struct Work_Unit) +  sizeof(PROBLEM_DATA_TYPE) * map_width * slice_height))
			printf(WHERESTR "Invalid size %ld vs %d, width: %d, slice: %d\n", WHEREARG, size, sizeof(struct Work_Unit) +  sizeof(PROBLEM_DATA_TYPE) * map_width * slice_height, map_width, slice_height);
#else
		//We do not use the real height, but rather the map_height.
		//This causes unwanted data transfer, but reduces fragmentation
		send_buffer = create(WORK_OFFSET + rows, sizeof(struct Work_Unit) +  sizeof(PROBLEM_DATA_TYPE) * map_width * slice_height);
#endif
		send_buffer->block_no = rows;
		send_buffer->buffer_size = map_width * thisHeight;
		send_buffer->heigth = thisHeight;
		send_buffer->line_start = lineOffset;
		workUnits++;

		memcpy(&send_buffer->problem, &data[send_buffer->line_start * map_width], send_buffer->buffer_size * sizeof(PROBLEM_DATA_TYPE));
		
		release(send_buffer);
		
		lineOffset += thisHeight;
		
		if (remainingLines > 0)
		{
			//printf(WHERESTR "Creating shared row %d\n", WHEREARG, SHARED_ROW_OFFSET + rows);
#ifdef CREATE_LOCALLY
			temp = acquire(SHARED_ROW_OFFSET + rows, &size, ACQUIRE_MODE_WRITE);
#else
			temp = create(SHARED_ROW_OFFSET + rows, sizeof(PROBLEM_DATA_TYPE) * map_width * 2);
#endif
			memcpy(temp, &data[lineOffset * map_width], sizeof(PROBLEM_DATA_TYPE) * map_width * 2);
			release(temp);
			
			lineOffset += 2;
			remainingLines -= 2;
			sharedCount++;
		}

		rows++;
	}
	
#ifdef CREATE_LOCALLY
	release(create(MASTER_START_LOCK, 1));
#endif


//Periodically update window?
#ifdef GRAPHICS

	delta = epsilon + 1;
	
	unsigned int zprint = 0;
	
	while(delta > epsilon)
	{
		//printf(WHERESTR "Waiting for manual barrier\n", WHEREARG);
		barrier = acquire(BARRIER_LOCK + barrier_alternation, &size, ACQUIRE_MODE_READ);
		while(barrier->lock_count != spu_count)
		{
			release(barrier);
			barrier = acquire(BARRIER_LOCK + barrier_alternation, &size, ACQUIRE_MODE_READ);
			zprint++;
			if (zprint == 1000000)
			{
				zprint = 0;
				printf(WHERESTR "Waiting for barrier lock, count: %d, exp: %d\n", WHEREARG, barrier->lock_count, spu_count);
			}
		}



        if((cnt + 1) == UPDATE_FREQ)
			printf(WHERESTR "Updating graphics, delta: %lf\n", WHEREARG, barrier->delta);
		else
			printf(WHERESTR "manual barrier done, delta: %lf\n", WHEREARG, barrier->delta);
		
		release(barrier);
		
        cnt++;
        if(cnt == UPDATE_FREQ)
        {
        	UpdateMasterMap(data, map_width, rows, sharedCount);
            show(data, map_width, map_height);
            cnt = 0;    
        }
    
    	if (delta <= epsilon)
    		release(create(DELTA_THRESHOLD_EXCEEDED));
       
        barrier = acquire(BARRIER_LOCK + barrier_alternation, &size, ACQUIRE_MODE_WRITE);
		//printf(WHERESTR "manual barrier done, setting lock count to 0\n", WHEREARG);
		barrier->lock_count = 0;
        delta = barrier->delta;
        release(barrier);
        //barrier_alternation = (barrier_alternation + 1) % 1;
	}
	
	
	
#endif /* GRAPHICS */

    deltasum = 0.0;
    rc = 0;
    for(i = 0; i< spu_count; i++)
    {
    	struct Results* results = acquire(RESULT_OFFSET + i, &size, ACQUIRE_MODE_READ);
        deltasum += results->deltaSum;
        rc += results->rc;
        release(send_buffer);
    }

         
#ifdef GRAPHICS
	show(data, map_width, map_height);
#endif	

    free(data);    

    printf("Sum of delta: %lf (%lf)\n", deltasum, epsilon);
    printf("Rc: %i\n", rc);

	sw_stop();
	sw_timeString(buf);
	printf("Time taken on %i processors with type %s (%ix%i:%i): %s\n", spu_count, (sizeof(PROBLEM_DATA_TYPE) == sizeof(float) ? "float" : "double") , map_width, map_height, UPDATE_FREQ, buf);


#ifdef GRAPHICS
    sleep(5);
	gs_exit();
#endif

}

int main(int argc, char* argv[])
{
	int spu_count;
	char* filename;
	int machineid;
	pthread_t* pthreads;
	
	dsmcbe_display_network_startup(1);
	
	/*printf("Size: %d\n", sizeof(struct Assignment_Unit));
	
	struct Assignment_Unit* x = malloc(sizeof(struct Assignment_Unit));
	x->epsilon = 0.0;
	x->map_height = 0;
	x->map_width = 0;
	x->sharedCount = 0;
	x->spu_count = 0;
	x->spu_no = 0;
	
	size_t i;
	for(i = 0; i < sizeof(struct Assignment_Unit); i++)
	{
		printf("Byte %d is %d\n", i, ((char*)x)[i]);	
	}
	
	sleep(5);*/
	
	if (argc == 2)
	{
		spu_count = atoi(argv[1]);
		machineid = 0;
		filename = NULL;
	}
	else if (argc == 4)
	{
		machineid = atoi(argv[1]);
		filename = argv[2];
		spu_count = atoi(argv[3]);
	}
	else
	{
		fprintf(stderr, "Wrong number of parameters, please use either:\n\"./PPU <spu_count>\"\n or \n\"./PPU <machineid> <filename> <spu_count>\"\n\n");
		exit(-1);
	}

	pthreads = simpleInitialize(machineid, filename, spu_count);
	
	//printf(WHERESTR "Starting machine %d\n", WHEREARG, machineid);
	
	if (machineid == 0)
	{
		Coordinator(128, 96 * 66, spu_count);
		release(create(MASTER_COMPLETION_LOCK, 1));
	}
	else
	{
		unsigned long size;
		release(acquire(MASTER_COMPLETION_LOCK, &size, ACQUIRE_MODE_READ));
	}
	
	//printf(WHERESTR "Shutting down machine %d\n", WHEREARG, machineid);
	
	//For some reason it won't die :(
	/*for(i = 0; i < (size_t)spu_count; i++)
		pthread_join(pthreads[i], NULL);*/
		
	//terminate();
	
    return 0;
}

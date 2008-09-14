
#include "StopWatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>
#include "../../DSMCBE/dsmcbe_ppu.h"
#include "../../DSMCBE/common/debug.h"
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

void UpdateMasterMap(PROBLEM_DATA_TYPE* data, unsigned int map_width, unsigned int rows)
{
	size_t i;
	unsigned long size;
	struct Work_Unit* item;
	PROBLEM_DATA_TYPE* temp;
	
	for(i =0; i < rows; i++)
	{
		printf(WHERESTR "Fetching work row: %d\n", WHEREARG, WORK_OFFSET + i);
		item = acquire(WORK_OFFSET + i, &size, ACQUIRE_MODE_READ);
        memcpy(&data[item->line_start * map_width], &item->problem, item->heigth * map_width * sizeof(PROBLEM_DATA_TYPE));

		if (i != rows-1)
		{
			printf(WHERESTR "Fetching shared row: %d\n", WHEREARG, SHARED_ROW_OFFSET + i);
			temp = acquire(SHARED_ROW_OFFSET + i + 1, &size, ACQUIRE_MODE_READ);
	        memcpy(&data[(item->line_start + item->heigth - 3) * map_width], temp, map_width * 2 * sizeof(PROBLEM_DATA_TYPE));
	        release(temp);
		}
		
        release(item);
	}
	
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
	unsigned int rows = (map_height + (SLICE_HEIGHT - 1)) / SLICE_HEIGHT;

	//Let all SPU's compete for a number
	struct Assignment_Unit* boot = create(ASSIGNMENT_LOCK, sizeof(struct Assignment_Unit));
	boot->map_width = map_width;
	boot->map_height = map_height;
	boot->spu_no = 0;
	boot->spu_count = spu_count;
	boot->epsilon = epsilon;
	release(boot);
	
	struct Job_Control* jobs = create(JOB_LOCK, sizeof(struct Job_Control));
	jobs->count = rows;
	jobs->nextjob = 0;
	jobs->red_round = 1;
	release(jobs);
	
	//Set up the barrier
	struct Barrier_Unit* barrier = create(BARRIER_LOCK, sizeof(struct Barrier_Unit));
	barrier->delta = 0;
	barrier->lock_count = 0;
	barrier->print_count = 0;
	release(barrier);
	
	createBarrier(EX_BARRIER_1, spu_count);

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
		MAPVALUE(map_width - 1, j) = -273.15;
	}


#ifdef GRAPHICS
	printf(WHERESTR "Displaying map (%d x %d)\n", WHEREARG, map_width, map_height);
	gs_init(map_width, map_height);	
	show(data, map_width, map_height);
#endif	

	//printf(WHERESTR "Starting timer\n", WHEREARG);

	sw_init();
	sw_start();

	for(i = 0 ;i < rows; i++)
	{
		printf(WHERESTR "Creating work %d\n", WHEREARG, WORK_OFFSET + i);
		
		unsigned int curh = i == rows-1 ? map_height % SLICE_HEIGHT + 1 : SLICE_HEIGHT;
		send_buffer = create(WORK_OFFSET + i, sizeof(struct Work_Unit) +  sizeof(PROBLEM_DATA_TYPE) * map_width * curh);

		if (i == 0)
			send_buffer->line_start = 0;
		else
			send_buffer->line_start = i * SLICE_HEIGHT;
		 
		send_buffer->buffer_size = map_width * curh;
		send_buffer->heigth = curh;
		send_buffer->block_no = i;

		memcpy(&send_buffer->problem, &data[send_buffer->line_start * map_width], send_buffer->buffer_size * sizeof(PROBLEM_DATA_TYPE));

		release(send_buffer);

		if (i != 0)
		{
			printf(WHERESTR "Creating shared row %d\n", WHEREARG, SHARED_ROW_OFFSET + i);
			temp = create(SHARED_ROW_OFFSET + i, sizeof(PROBLEM_DATA_TYPE) * map_width * 2);
			memcpy(temp, &data[(send_buffer->line_start + send_buffer->heigth) * map_width], sizeof(PROBLEM_DATA_TYPE) * map_width * 2);
			release(temp);
		}

	}


	//printf(WHERESTR "Waiting for %d SPU's to boot up\n", WHEREARG, spu_count);
	
	/*boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_READ);
	while(boot->spu_no != spu_count)
	{
		release(boot);
		boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_READ);
	}
	release(boot);*/

	//printf(WHERESTR "All %d SPU's are booted\n", WHEREARG, spu_count);

//Periodically update window?
#ifdef GRAPHICS

	delta = epsilon + 1;
	
	while(delta > epsilon)
	{
		printf(WHERESTR "Waiting for manual barrier\n", WHEREARG);
		barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		while(barrier->lock_count != spu_count)
		{
			release(barrier);
			barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		}

		printf(WHERESTR "manual barrier done\n", WHEREARG);


        if((cnt + 1) == UPDATE_FREQ)
			printf(WHERESTR "Updating graphics, delta: %lf\n", WHEREARG, barrier->delta);

		release(barrier);
		
        cnt++;
        if(cnt == UPDATE_FREQ)
        {
        	UpdateMasterMap(data, map_width, rows);
            show(data, map_width, map_height);
            cnt = 0;    
        }
       
        barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_WRITE);
		printf(WHERESTR "manual barrier done, setting lock count to 0\n", WHEREARG);
		barrier->lock_count = 0;
        delta = barrier->delta;
        release(barrier);
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
		fprintf(stderr, "Wrong number of parameters, please use either \"./PPU <spu_count>\" or \"./PPU <machineid> <filename> <spu_count>\"\n\n");
		exit(-1);
	}

	pthreads = simpleInitialize(machineid, filename, spu_count);
	
	//printf(WHERESTR "Starting machine %d\n", WHEREARG, machineid);
	
	if (machineid == 0)
		Coordinator(128, 512, spu_count);
	
	//printf(WHERESTR "Shutting down machine %d\n", WHEREARG, machineid);
	
	//For some reason it won't die :(
	/*for(i = 0; i < (size_t)spu_count; i++)
		pthread_join(pthreads[i], NULL);*/
		
	//terminate();
	
    return 0;
}

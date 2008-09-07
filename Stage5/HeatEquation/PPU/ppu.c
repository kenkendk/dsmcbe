
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

#ifdef USE_BARRIER
void dsmcbe_barrier(GUID id, unsigned int ref_count)
{
	acquireBarrier(id);
}

#else
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
#endif

//The coordinator calls this process
void Coordinator(unsigned int map_width, unsigned int map_height, unsigned int spu_count)
{
	size_t i, j;
	int rest, divisor, rc;
	PROBLEM_DATA_TYPE* data;
	char buf[256];
	unsigned int lineno, lines_to_send;
	int line_start, line_end;
	int buffer_index;
	struct Work_Unit* send_buffer;
	unsigned long size;
	double deltasum;
	unsigned int buffer_size;
#ifdef GRAPHICS
	unsigned int cnt = 0;
	double delta;
#endif
   
   	unsigned int map_size = map_width * map_height;
	double epsilon = 0.001 * (map_width - 1) * (map_height - 1);

	//Let all SPU's compete for a number
	struct Assignment_Unit* boot = create(ASSIGNMENT_LOCK, sizeof(struct Assignment_Unit));
	boot->spu_no = 0;
	boot->spu_count = spu_count;
	release(boot);
	
	//Set up the barrier
	struct Barrier_Unit* barrier = create(BARRIER_LOCK, sizeof(struct Barrier_Unit));
	barrier->delta = 0;
	barrier->lock_count = 0;
	barrier->print_count = 0;
	release(barrier);
	
#ifdef USE_BARRIER
	createBarrier(EX_BARRIER_1, spu_count);
	createBarrier(EX_BARRIER_2, spu_count);
	createBarrier(EX_BARRIER_3, spu_count);
#ifdef GRAPHICS
	createBarrier(BARRIER_LOCK_EXTRA, spu_count + 1);
#else	
	createBarrier(BARRIER_LOCK_EXTRA, spu_count);
#endif

#else	
	release(create(EX_BARRIER_1, sizeof(unsigned int)));
	release(create(EX_BARRIER_2, sizeof(unsigned int)));
	release(create(EX_BARRIER_3, sizeof(unsigned int)));
#endif

	//printf(WHERESTR "Boot and barrier done\n", WHEREARG);

	divisor = (int)(map_height / spu_count);
	rest = (int)(map_height % spu_count);
	
	if (rest == 0)
		buffer_size = (divisor + 2) * map_width;
	else
		buffer_size = (divisor + 3) * map_width;
   

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

    /* distribute */
    lineno = 0;
    buffer_index = 0;
    
    for(i = 0; i< spu_count; i++)
    {
        lines_to_send = divisor;
        if (rest != 0)
        {
            lines_to_send++;
            rest--;
        }
        
        //printf(WHERESTR "Building buffer %d\n", WHEREARG, i);
        
        //If this is anything but the very first line, we also send the previous
        line_start = (lineno == 0 ? lineno : lineno - 1);
        //If this is anything but the very last line, we also send the next
        line_end = ((lineno + lines_to_send) == (map_height) ? (map_height) : (lineno + lines_to_send) + 1);

        send_buffer = create(WORK_OFFSET + i, sizeof(struct Work_Unit) + ((line_end - line_start) * map_width * sizeof(PROBLEM_DATA_TYPE)));
        
        if (send_buffer == NULL) REPORT_ERROR("bad create");
        
        send_buffer->line_start = line_start;
        send_buffer->width = map_width;
        send_buffer->heigth = line_end - line_start;
        send_buffer->epsilon = epsilon;
        send_buffer->buffer_size = (line_end - line_start) * map_width;
        memcpy(&send_buffer->problem, &data[line_start * map_width], send_buffer->buffer_size * sizeof(PROBLEM_DATA_TYPE));

		release(send_buffer);
		                
		//printf(WHERESTR "Done with buffer %d\n", WHEREARG, i);
		                
        lineno += lines_to_send;
    }


	//printf(WHERESTR "Waiting for %d SPU's to boot up\n", WHEREARG, spu_count);
	
	boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_READ);
	while(boot->spu_no != spu_count)
	{
		release(boot);
		boot = acquire(ASSIGNMENT_LOCK, &size, ACQUIRE_MODE_READ);
	}
	release(boot);

	//printf(WHERESTR "All %d SPU's are booted\n", WHEREARG, spu_count);

//Periodically update window?
#ifdef GRAPHICS

	delta = epsilon + 1;
	
	while(delta > epsilon)
	{
		barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		while(barrier->lock_count != spu_count)
		{
			release(barrier);
			barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_READ);
		}

        if((cnt + 1) == UPDATE_FREQ)
			printf(WHERESTR "Updating graphics, delta: %lf\n", WHEREARG, barrier->delta);

		release(barrier);
		
        cnt++;
        if(cnt == UPDATE_FREQ)
        {

        	for(i = 0; i < spu_count; i++)
        	{    
	        	send_buffer = acquire(WORK_OFFSET + i, &size, ACQUIRE_MODE_READ);
    	    	memcpy(&data[send_buffer->line_start * map_width], &send_buffer->problem, send_buffer->heigth * map_width * sizeof(PROBLEM_DATA_TYPE));
        		release(send_buffer);
        	}

            cnt = 0;    
            show(data, map_width, map_height);
        }
        
        barrier = acquire(BARRIER_LOCK, &size, ACQUIRE_MODE_WRITE);
        barrier->lock_count = 0;
        delta = barrier->delta;
        release(barrier);
#ifdef USE_BARRIER
		dsmcbe_barrier(BARRIER_EXTRA_LOCK, 0);
#endif        
	}
      
#else
	//Make sure the SPU's have taken their locks
	sleep(2);  
#endif

    deltasum = 0.0;
    rc = 0;
    for(i = 0; i< spu_count; i++)
    {
    	send_buffer = acquire(WORK_OFFSET + i, &size, ACQUIRE_MODE_READ);
        deltasum += send_buffer->epsilon;
        rc += send_buffer->width;
        memcpy(&data[send_buffer->line_start * map_width], &send_buffer->problem, send_buffer->heigth * map_width * sizeof(PROBLEM_DATA_TYPE));
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
	size_t i;
	
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
		Coordinator(128, 128 * spu_count, spu_count);
	
	//printf(WHERESTR "Shutting down machine %d\n", WHEREARG, machineid);
	
	//For some reason it won't die :(
	/*for(i = 0; i < (size_t)spu_count; i++)
		pthread_join(pthreads[i], NULL);*/
		
	//terminate();
	
    return 0;
}

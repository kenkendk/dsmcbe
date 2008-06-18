#include "PrototeinShared.h"
#include "ppu.h"
#include <stdio.h>
#include <string.h>
#include <malloc_align.h>
#include <free_align.h>
#include <memory.h>
#include <dsmcbe_ppu.h>
#include <pthread.h>
#include <common/debug.h>

#define JOBS_PR_PROCESSOR 10
#define REQUIRED_JOB_COUNT (10000 * prototein_length)

void fold_broad(struct coordinate place, unsigned int required_jobs);

unsigned int job_queue_length;
unsigned int current_job;
struct coordinate* job_queue;

unsigned int job_queue_tree_depth;
unsigned int job_queue_tree_break;
int bestscore;
struct coordinate* winner;

void PrepareWorkBlock(struct workblock* w, unsigned int current_job)
{
	int trn_size;
	unsigned int joboffset;
	
	(*w).winner_score = bestscore;
	(*w).item_length = current_job > job_queue_tree_break ? job_queue_tree_depth - 1: job_queue_tree_depth;
	
	trn_size = sizeof(struct coordinate) * (*w).item_length;
	//Estimate the maxsize
	(*w).worksize = ((BUFFER_SIZE - sizeof(struct workblock)) / trn_size);
	//printf("current_job: %d, worksize: %d, job_queue_length: %d\n", current_job, (*w).worksize, job_queue_length);
	if ((current_job + (*w).worksize) > job_queue_length)
		(*w).worksize = job_queue_length - current_job;
	
	if (current_job > job_queue_tree_break)
		joboffset = ((job_queue_tree_depth) * current_job) - ((current_job - job_queue_tree_break));
	else
		joboffset = (job_queue_tree_depth) * current_job;
		
	if (current_job < job_queue_tree_break && (current_job + (*w).worksize) > job_queue_tree_break)
		(*w).worksize_delta = job_queue_tree_break - current_job;
	else
		(*w).worksize_delta = (*w).worksize + 1;
		
	memcpy(((void*)w) + sizeof(struct workblock), &job_queue[joboffset], trn_size * (*w).worksize);
	
	/*printf("Made a new workblock with %d elements and break at %d\n", (*w).worksize, (*w).worksize_delta);
	for(i = 0; i < 4; i++)
		printmap(&job_queue[joboffset+((*w).item_length*i)], (*w).item_length, 0);
	printf("memdump: ");
	for(i = 80; i < 120; i++)
		printf("%x ", ((unsigned char*)w)[i]);
	printf("\n");*/
}

void FoldPrototein(char* proto, int machineid, char* networkfile, int spu_count)
{
	size_t i;
	void* prototein_object;
	void* tempobj;
	unsigned int* work_counter;
	unsigned long size;
	struct coordinate cord;
	pthread_t* threads;
	struct workblock* wb;
	unsigned int* count_obj;
	unsigned int winner_count;
	
	unsigned int total_jobcount;
	unsigned int reported_jobcount;

	bestscore = -9999999;
	
	prototein_length = strlen(proto);
	prototein = proto;
	
	if (networkfile == NULL)
		threads = simpleInitialize(0, NULL, spu_count);
	else
		threads = simpleInitialize(machineid, networkfile, spu_count);
	
	if (machineid == 0 || networkfile == NULL)
	{
	    //printf(WHERESTR "PPU is broadcasting Prototein info, spu_count: %d\n", WHEREARG, spu_count);
		//Broadcast info about the prototein
		prototein_object = create(PROTOTEIN, (sizeof(unsigned int) * 2) + prototein_length);
		((unsigned int*)prototein_object)[0] = 0;
		((unsigned int*)prototein_object)[1] = prototein_length;
		memcpy(prototein_object + (sizeof(unsigned int) * 2), proto, prototein_length);
		release(prototein_object);
	
	    //printf(WHERESTR "PPU is setting up result buffers\n", WHEREARG);
		//Allocate result buffers
		
	    //printf(WHERESTR "PPU is building work tree\n", WHEREARG);
		//Allocate the consumer syncroniation primitive
		work_counter = (unsigned int*)create(PACKAGE_ITEM, sizeof(unsigned int) * 2);
		work_counter[0] = 0;
		
		//Make a bag of tasks
		cord.x = cord.y = prototein_length;
		fold_broad(cord, REQUIRED_JOB_COUNT);
	
	    //printf(WHERESTR "PPU is building tasks\n", WHEREARG);
		//Now create all actual tasks, this is a bit wastefull in terms of memory usage
		total_jobcount = 0;
		while(current_job < job_queue_length)
		{
			wb = (struct workblock*)create(WORKITEM_OFFSET + total_jobcount, BUFFER_SIZE);
			PrepareWorkBlock(wb, current_job);
			current_job += wb->worksize;
			release(wb);
			total_jobcount++;
		}
	
	    //printf(WHERESTR "PPU has completed building tasks\n", WHEREARG);
		free(job_queue);
		
		//Let the SPU's begin their work
		work_counter[1] = total_jobcount;
		release(work_counter);
	}

    //printf(WHERESTR "PPU is waiting for SPU completion\n", WHEREARG);
	//Just wait for them all to complete
	for(i = 0; i < (unsigned int)spu_count; i++)
	{
	    //printf(WHERESTR "waiting for SPU %i\n", WHEREARG, i);
		pthread_join(threads[i], NULL);
	}
	
	if (machineid == 0)
	{
	    //printf(WHERESTR "PPU is reading results\n", WHEREARG);
		count_obj = acquire(PROTOTEIN, &size, ACQUIRE_MODE_WRITE);
    	winner_count = *count_obj;
    	release(count_obj);
		
	    //printf(WHERESTR "PPU is reading results\n", WHEREARG);
		reported_jobcount = 0;
		winner = (struct coordinate*)malloc(sizeof(struct coordinate) * prototein_length);
	    //printf(WHERESTR "PPU is reading results\n", WHEREARG);
		//Pick up the results
		for(i = 0; i < winner_count; i++)
		{
		    //printf(WHERESTR "PPU is reading result for %i\n", WHEREARG, i);
			tempobj = acquire(WINNER_OFFSET + i, &size, ACQUIRE_MODE_READ);
			if (tempobj == NULL)
				printf(WHERESTR "winner buffer failed\n", WHEREARG);
			else
			{
				//printf(WHERESTR "SPU %d result was %d\n", WHEREARG,i, ((int*)tempobj)[0]); 
				reported_jobcount += ((int*)tempobj)[1];
				if (((int*)tempobj)[0] > bestscore)
				{
					bestscore = ((int*)tempobj)[0];
					memcpy(winner, tempobj + (sizeof(int) * 2), prototein_length * sizeof(struct coordinate));
				}
				release(tempobj);
			}
		}
		
		//printf("Optimal folding is (%d):\n", bestscore);
		printmap(winner, prototein_length);
		printf("Fibers: %d\n", SPU_FIBERS);
		
		release(create(MASTER_COMPLETION_LOCK, sizeof(unsigned int)));
	
	}
	else
	{
		release(acquire(MASTER_COMPLETION_LOCK, &size, ACQUIRE_MODE_READ));
	}
	
	
	
	printf("The SPU's reported processing %d jobs out of %d\n", reported_jobcount, total_jobcount);
	
}

void fold_broad(struct coordinate place, unsigned int required_jobs)
{
    struct coordinate* prev_places;
    struct coordinate* new_places;
    
    unsigned int prev_place_length;
    unsigned int new_place_length;
    unsigned int tree_depth;
    
    size_t i;
	
	int x, y;
	struct coordinate* prev_offset;
	
	int t1;
    
    tree_depth = 1;
    
    prev_places = (struct coordinate*) malloc(sizeof(struct coordinate));
	if (prev_places == NULL)
	{
		printf("Memory error\n");
		exit(-4);
	}
	memcpy(prev_places, &place, sizeof(struct coordinate));
    prev_place_length = 1;
    
    while(1)
    {
        //1. Determine needed space
        new_place_length = 0;
        for(i = 0; i < prev_place_length; i++)
        {
			x = prev_places[(i * (tree_depth)) + (tree_depth - 1)].x;
			y = prev_places[(i * (tree_depth)) + (tree_depth - 1)].y;
			prev_offset = &prev_places[i * (tree_depth)];
			
        	if (tree_depth>3 && get_map_char_raw(x - 1, y, prev_offset, tree_depth) == ' ')
        	    new_place_length++;
        	if (tree_depth>2 && get_map_char_raw(x, y - 1, prev_offset, tree_depth) == ' ')
        	    new_place_length++;
        	if (tree_depth>1 && get_map_char_raw(x + 1, y, prev_offset, tree_depth) == ' ')
        	    new_place_length++;
        	if (get_map_char_raw(x, y + 1, prev_offset, tree_depth) == ' ')
        	    new_place_length++;        	    

            if ((prev_place_length - (i+1)) + new_place_length >= required_jobs)
                break;
        }
                
        //2. Allocate space        
        new_places = (struct coordinate*)malloc(sizeof(struct coordinate) * new_place_length * (tree_depth + 1));
		if (new_places == NULL)
		{
			printf("Memory error\n");
			exit(-5);
		}
        new_place_length = 0;

        //3. Assign the new combinations        
        for(i = 0; i < prev_place_length; i++)
        {
			t1 = new_place_length;

			x = prev_places[(i * (tree_depth)) + (tree_depth - 1)].x;
			y = prev_places[(i * (tree_depth)) + (tree_depth - 1)].y;
			prev_offset = &prev_places[i * (tree_depth)];
			
        	if (tree_depth>3 && get_map_char_raw(x - 1, y, prev_offset, tree_depth) == ' ')
        	{
        	    memcpy(&new_places[new_place_length * (tree_depth+1)], prev_offset, sizeof(struct coordinate) * (tree_depth));
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].x = x - 1;
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].y = y;
        	    new_place_length++;
        	}
        	if (tree_depth>2 && get_map_char_raw(x, y - 1, prev_offset, tree_depth) == ' ')
        	{
        	    memcpy(&new_places[new_place_length * (tree_depth+1)], prev_offset, sizeof(struct coordinate) * (tree_depth));
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].x = x;
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].y = y-1;
        	    new_place_length++;
        	}
        	if (tree_depth>1 && get_map_char_raw(x + 1, y, prev_offset, tree_depth) == ' ')
        	{
        	    memcpy(&new_places[new_place_length * (tree_depth+1)], prev_offset, sizeof(struct coordinate) * (tree_depth));
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].x = x + 1;
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].y = y;
        	    new_place_length++;
        	}
        	if (get_map_char_raw(x, y + 1, prev_offset, tree_depth) == ' ')
        	{
        	    memcpy(&new_places[new_place_length * (tree_depth+1)], prev_offset, sizeof(struct coordinate) * (tree_depth));
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].x = x;
        	    new_places[(new_place_length * (tree_depth+1)) + (tree_depth)].y = y + 1;
        	    new_place_length++;
        	}
			        				
            if ((prev_place_length - (i+1)) + new_place_length >= required_jobs)
                break;        	
        }

        //If we are done, gather a complete list        
        if ((prev_place_length - i) + new_place_length >= required_jobs)
        {
        	//was _malloc_align
            job_queue = (struct coordinate*) 
                malloc(
                    sizeof(struct coordinate) * (
                        ((prev_place_length - i) * (tree_depth)) 
                        + 
                        (new_place_length * (tree_depth+1))
                        )/*, 7*/);
          if (job_queue == NULL)
		  {
			  printf("Memory error\n");
			  exit(-4);
		  }
		  
		  i++;
		  
          //copy from i an onwards
          memcpy(job_queue, &prev_places[i * (tree_depth)], sizeof(struct coordinate) * ((prev_place_length - i) * (tree_depth)));
          
          //copy all new ones
          memcpy(&job_queue[((prev_place_length - i) * (tree_depth))], new_places, sizeof(struct coordinate) * (new_place_length * (tree_depth+1)));
            
          job_queue_length = (prev_place_length - i) + new_place_length;
          
          job_queue_tree_depth = tree_depth;
          job_queue_tree_break = (prev_place_length - i)-1;
		            
          break;
          
        }
        else
        {
            //Prepare for a new itteration
            free(prev_places);
            prev_place_length = new_place_length;
            prev_places = new_places;
            tree_depth++;
			            
            //Break out if we are done, only happens for short prototeins
            if (tree_depth == prototein_length)
            {
                job_queue = prev_places;
                job_queue_length = prev_place_length;
                
                job_queue_tree_depth = tree_depth;
                job_queue_tree_break = prev_place_length;
                break;
            }        
        }		
    }
}


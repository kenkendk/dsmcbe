#include "PrototeinShared.h"
#include "ppu.h"
#include <stdio.h>
#include <string.h>
#include <malloc_align.h>
#include <free_align.h>
#include <memory.h>

#define JOBS_PR_PROCESSOR 10
#define REQUIRED_JOB_COUNT (spu_count * 1000)

void fold_broad(struct coordinate place, unsigned int required_jobs);

unsigned int job_queue_length;
unsigned int current_job;
struct coordinate* job_queue;

unsigned int job_queue_tree_depth;
unsigned int job_queue_tree_break;
int bestscore;
struct coordinate* winner;

int GetWaitingSPU(speid_t* ids, int spu_count, struct coordinate** winners, unsigned int* incomplete)
{
	int res, i, score;
	while(1)
		for(i = 0; i < spu_count; i++)
		{
			if (incomplete[i] > 0)
			{
				res = spe_out_mbox_status(ids[i]);
				if (res != 0)
				{
					spe_out_mbox_read(ids[i], (unsigned int*)&score, 1);
					printf("Read a score %d\n", score);	        
					
					if (score > bestscore)
					{
						winner = winners[i];
						bestscore = score;
					}
					incomplete[i]--;
					return i;
				}
			}
		}
}

void PrepareWorkBlock(struct workblock* w, unsigned int current_job)
{
	int trn_size;
	unsigned int joboffset;
	
	(*w).winner_score = bestscore;
	(*w).item_length = current_job > job_queue_tree_break ? job_queue_tree_depth - 1: job_queue_tree_depth;
	
	trn_size = sizeof(struct coordinate) * (*w).item_length;
	//Estimate the maxsize
	(*w).worksize = ((BUFFER_SIZE - sizeof(struct workblock)) / trn_size);
	
	if (current_job > job_queue_tree_break)
		joboffset = (job_queue_tree_depth) * current_job + ((current_job - job_queue_tree_break));
	else
		joboffset = (job_queue_tree_depth - 1) * current_job;
		
	if (current_job < job_queue_tree_break && (current_job + (*w).worksize) < job_queue_tree_break)
		(*w).worksize_delta = job_queue_tree_break - current_job;
	else
		(*w).worksize_delta = (*w).worksize + 1;

	memcpy(((void*)w) + sizeof(struct workblock), &job_queue[joboffset], trn_size);
}

void FoldPrototein(char* proto, speid_t* ids, pthread_t* threads, int spu_count)
{
	int i, j, score, remainder;
	unsigned int current_spu, prototein_buffer_length, winner_buffer_length;
	struct workblock** workblocks; 
	struct coordinate cord;
	struct coordinate** winners;
	unsigned int* inprogress;

	bestscore = -9999999;
	
	prototein_length = strlen(proto);
	prototein_buffer_length = prototein_length + (16 - prototein_length % 16);
	prototein = (char*)_malloc_align(sizeof(char) * prototein_buffer_length, 7);
	prototein = memcpy(prototein, proto, prototein_length);
	winners = malloc(sizeof(struct coordinate*) * spu_count);
	workblocks = malloc(sizeof(struct workblock*));
	inprogress = (unsigned int*)malloc(sizeof(unsigned int) * spu_count);
	memset(inprogress, 0, sizeof(unsigned int) * spu_count);
	current_spu = 0;
	
	cord.x = cord.y = prototein_length;
	fold_broad(cord, REQUIRED_JOB_COUNT);
	winner_buffer_length = (sizeof(struct coordinate) * prototein_length);
	winner_buffer_length += 16 - winner_buffer_length % 16;
		
	for(i = 0; i < spu_count ; i++)
	{
		send_mailbox_message_to_spe(ids[i], 1, &prototein_length);
		send_mailbox_message_to_spe(ids[i], 1, (unsigned int*)&prototein);
		winners[i] = (struct coordinate*)_malloc_align(winner_buffer_length, 7);
		workblocks[i] = (struct workblock*)_malloc_align(sizeof(struct workblock), 7);
		send_mailbox_message_to_spe(ids[i], 1, (unsigned int*)&winners[i]);
	}

	//Fill up with two jobs for async processing
	for(i = 0; i < spu_count * 2; i++)
	{
		PrepareWorkBlock(workblocks[i % spu_count], current_job);
	
		current_job += (*workblocks[i % spu_count]).worksize;		 		
		send_mailbox_message_to_spe(ids[i % spu_count], 1, (unsigned int*)&workblocks[i % spu_count]);
		inprogress[i % spu_count]++;
		
		if (current_job > job_queue_length)
			break;
	}

	while(current_job < job_queue_length)
	{
		i = GetWaitingSPU(ids, spu_count, winners, inprogress);
		PrepareWorkBlock(workblocks[i], current_job);

		current_job += (*workblocks[i]).worksize;		 		
		send_mailbox_message_to_spe(ids[i], 1,(unsigned int*) &workblocks[i]);
		inprogress[i]++;
	}
	
	//Tell the rest to die after the current work
	j = 0;
	for(i = 0; i < spu_count; i++)
		send_mailbox_message_to_spe(ids[i], 1, (unsigned int *)&j);
		
	printf("Written stop for all, harvesting remaining results\n");
	
	remainder = 0;
	for(i = 0; i < spu_count; i++)
		remainder += inprogress[i];

	while(remainder > 0)
		for(i = 0; i < spu_count; i++)
			if(inprogress[i] > 0)
				while(spe_out_mbox_status(ids[i]) > 0)
				{
					spe_out_mbox_read(ids[i], (unsigned int*)&score, 1);
					printf("Read a score %d\n", score);
					if (score > bestscore)
					{
						bestscore = score;
						winner = winners[i];
					}
					inprogress[i]--;
					remainder--;
				}

	WaitForSPUCompletion(threads, spu_count);

	printf("Optimal folding is (%d):\n", bestscore);
	printmap(winner, prototein_length);
	

	for(i = 0; i < spu_count; i++)
	{
		_free_align(workblocks[i]);
		_free_align(winners[i]);
	}
	
	free(workblocks);
	free(winners);
	_free_align(prototein);
		
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
            job_queue = (struct coordinate*) 
                _malloc_align(
                    sizeof(struct coordinate) * (
                        ((prev_place_length - i) * (tree_depth)) 
                        + 
                        (new_place_length * (tree_depth+1))
                        ), 7);
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


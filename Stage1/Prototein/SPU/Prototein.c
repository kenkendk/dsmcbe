#include "../PPU/PrototeinShared.h"
#include "DMATransfer.h"

#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include <errno.h>
#include <malloc_align.h>
#include <string.h>

#define MAP_WIDTH (prototein_length * 2 + 1) 
#define MAP_HEIGTH (prototein_length * 2 + 1)
#define MAP_SIZE (MAP_WIDTH * MAP_HEIGTH)
#define MAP_COORDINATES(x, y) (((y)*MAP_WIDTH)+(x))

void fold(struct coordinate* place, unsigned int places_length);

#define GET_MAP_CHAR(x,y) (map[MAP_COORDINATES((x),(y))])
#define UPDATE_MAP(x,y,c) (GET_MAP_CHAR((x),(y)) = (c))

int bestscore;
int winner_length;
struct coordinate* winner;
char* map;

int calc_score_run;

void initialize_map(struct coordinate* place, unsigned int places_length)
{
    size_t i; 
    int z;
	for(i = 0; i< MAP_SIZE; i++)
		map[i] = ' ';

    for (i = 0; i < places_length; i++)
	{
		z = place[i].x;
		z = place[i].y;
		z = MAP_COORDINATES(place[i].x,place[i].y);
        UPDATE_MAP(place[i].x, place[i].y, prototein[i]);
	}
}


int FoldPrototein(unsigned long long id)
{
	unsigned int ea;
	unsigned int ea_winner;
	unsigned int prototein_buffer_length;
	unsigned int winner_buffer_length;
	
	void* b0;
	void* b1;
	void* current_buffer;  

    struct coordinate* places;
    struct coordinate* queue;
    struct workblock* work;
    
    size_t i;
    
    bestscore = -9999999;
   	b0 = (void*)malloc_align(BUFFER_SIZE, 7); 
   	b1 = (void*)malloc_align(BUFFER_SIZE, 7);
   	current_buffer = b0; 
    
    prototein_length = spu_read_in_mbox();
    ea = spu_read_in_mbox();
    ea_winner = spu_read_in_mbox();
    map = (char*) malloc_align(MAP_SIZE, 7);
    
    //Must transfer in multiples of 16
    prototein_buffer_length = prototein_length + (16 - prototein_length % 16);
    
    prototein = (char*)malloc_align(sizeof(char) * prototein_buffer_length, 7);
    StartDMAReadTransfer(prototein, ea, prototein_buffer_length, 2);
    
    winner_buffer_length = sizeof(struct coordinate) * prototein_length;
    winner_buffer_length += 16 - winner_buffer_length % 16;
    
    ea = spu_read_in_mbox();
    winner = (struct coordinate*)malloc_align(winner_buffer_length, 7);
    if (winner == NULL)
    	printf("Failed to allocate memory %d\n", errno);
    places = (struct coordinate*)malloc_align(sizeof(struct coordinate) * prototein_length, 7);
    if (places == NULL)
    	printf("Failed to allocate memory %d\n", errno);
    
    StartDMAReadTransfer(current_buffer, ea, BUFFER_SIZE, GetDMAGroupID(b0, b1, current_buffer));
    WaitForDMATransferByGroup(2);

    while(ea != 0)
    {
	    WaitForDMATransfer(b0, b1, current_buffer); 

		work = (struct workblock*)current_buffer;  
	    queue = (struct coordinate*)(current_buffer + sizeof(struct workblock));
    
    	//This reduces copying of the winner
    	if ((*work).winner_score < bestscore)
    		bestscore = (*work).winner_score;
    		  
		current_buffer = current_buffer == b0 ? b1 : b0;

	    ea = spu_read_in_mbox();
	    if ((void*)ea != NULL)
	       	StartDMAReadTransfer(current_buffer, ea, BUFFER_SIZE, GetDMAGroupID(b0, b1, current_buffer));
	    	    	    
	   	//printf("SPU recieved a work block with %d items\n", (*work).worksize);
	    for(i = 0; i < (*work).worksize; i++)
	    {
	    	if (i == (*work).worksize_delta)
	    	{
	    		(*work).item_length--;
	    		//printf("Decreasing item length to: %d\n", (*work).item_length);
	    	}
	    		
	        calc_score_run = 0;
	        memcpy(places, queue, sizeof(struct coordinate) * (*work).item_length);
	
			/*if (((i % 20) == 0) || (i >= 220))
			{
				printf("SPU is folding: (%d)\n", i);
				printmap(places, (*work).item_length);
			}*/
			
			initialize_map(places, (*work).item_length);
	        fold(places, (*work).item_length);
	        queue += (*work).item_length;
    	}
    	
    	//printf("Done folding work block at SPU, score: %d\n", bestscore);
    	//printmap(winner, prototein_length);
        StartDMAWriteTransfer(winner, ea_winner, winner_buffer_length, 2);

		WaitForDMATransferByGroup(2);
		//printf("Writing a score %d\n", bestscore);	        
        spu_write_out_mbox(bestscore);
    }

    free_align(prototein);
    free_align(winner);
    free_align(places);
    free(queue);
    free_align(b0);
    free_align(b1);
    free(map);

	return 0;
}

inline void recurse(int newx, int newy, struct coordinate* place, int places_length)
{
    if (GET_MAP_CHAR(newx, newy) == ' ')
	{			
	    place[places_length].x = newx;
	    place[places_length].y = newy;
	    UPDATE_MAP(newx,newy,prototein[places_length]);
        fold(place, places_length +1);
        UPDATE_MAP(newx,newy,' ');
	}
}

void calc_score(struct coordinate* place, unsigned int places_length)
{
	size_t i;
	int score, x, y;	
	score = 0;
	
	calc_score_run++;
	
	for(i = 0; i < prototein_length; i++)
	{
		x = place[i].x;
		y = place[i].y;
    
        if (GET_MAP_CHAR(x, y)=='H')
		{	
            if (GET_MAP_CHAR(x-1, y)==' ') score--;
	        if (GET_MAP_CHAR(x+1, y)==' ') score--;
            if (GET_MAP_CHAR(x, y-1)==' ') score--;
            if (GET_MAP_CHAR(x, y+1)==' ') score--;
    	}
	}
	
	//printf("Comparing scores %d vs. %d\n", score, bestscore);
	
    if(score>bestscore)
	{
		//printf("New best score: (%d)\n", score);
		//printmap(place, places_length);

		//printf("Prev winner: (%d)\n", bestscore);
		//printmap(winner, winner_length);
	
        bestscore=score;
		winner_length = places_length;
		for(i = 0; i<places_length; i++)
			winner[i] = place[i];

		//printf("New winner: (%d)\n", bestscore);
		//printmap(winner, winner_length);
		//i++;
	}
	
}

void fold(struct coordinate* place, unsigned int places_length)
{
	if (places_length == prototein_length)
	{
		calc_score(place, places_length);
		return;
	}
	
	if (places_length > prototein_length)
	{
		printf("!!!! Error.. out of bounds...\n\n");
		return;	
	}

    //Since this will always be called at level > 3, there is no optimization checks here
	recurse(place[places_length-1].x - 1, place[places_length-1].y, place, places_length);
	recurse(place[places_length-1].x, place[places_length-1].y - 1, place, places_length);
	recurse(place[places_length-1].x + 1, place[places_length-1].y, place, places_length);
	recurse(place[places_length-1].x, place[places_length-1].y + 1, place, places_length);
}

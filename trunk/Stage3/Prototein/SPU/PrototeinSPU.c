#include "../PPU/PrototeinShared.h"

#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include <errno.h>
#include <malloc_align.h>
#include <string.h>
#include <dsmcbe_spu.h>
#include <common/debug.h>
#include <common/datastructures.h>

#define MAP_WIDTH (prototein_length * 2 + 1) 
#define MAP_HEIGTH (prototein_length * 2 + 1)
#define MAP_SIZE (MAP_WIDTH * MAP_HEIGTH)
#define MAP_COORDINATES(x, y) (((y)*MAP_WIDTH)+(x))

void fold(struct coordinate* place, unsigned int places_length);

#define GET_MAP_CHAR(x,y) (map[MAP_COORDINATES((x),(y))])
#define UPDATE_MAP(x,y,c) (GET_MAP_CHAR((x),(y)) = (c))

char* map = NULL;
struct coordinate* winner;
int bestscore;

static unsigned int totalwork = 0;
static unsigned int totaljobs = 0;

void initialize_map(struct coordinate* place, unsigned int places_length)
{
	size_t i;
	for(i = 0; i< MAP_SIZE; i++)
		map[i] = ' ';

    for (i = 0; i < places_length; i++)
        UPDATE_MAP(place[i].x, place[i].y, prototein[i]);
}


int FoldPrototein(unsigned long long id)
{
    struct coordinate* places;
    struct coordinate* queue;
    struct workblock* work;
    void* prototein_object;
    void* winner_object;
    unsigned int* synclock;
    unsigned long size;
    GUID itemno;
    unsigned int thread_id;
    int threadNo;
    unsigned int total;
    
    size_t i;
    
    //Remove warning about unused parameter
    id = 0;
    bestscore = -9999999;
    prototein = NULL;
    
    //printf(WHERESTR "Started SPU\n", WHEREARG);
    initialize();
    
    prototein_object = acquire(PROTOTEIN, &size, WRITE);
    //printf(WHERESTR "SPU got prototein\n", WHEREARG);
    
    thread_id = ((unsigned int*)prototein_object)[0];
    ((unsigned int*)prototein_object)[0]++;
    prototein_length = ((unsigned int*)prototein_object)[1];
	prototein = (char*)MALLOC(sizeof(char) * prototein_length);
	memcpy(prototein, prototein_object + (sizeof(unsigned int) * 2), prototein_length);

    map = (char*) MALLOC(MAP_SIZE);
    winner = MALLOC((sizeof(struct coordinate) * prototein_length));

	places = (struct coordinate*)MALLOC(sizeof(struct coordinate) * prototein_length);
    //printf(WHERESTR "SPU read prototein: %s, and got ID: %d\n", WHEREARG, prototein, thread_id);
    release(prototein_object);
    
    if (places == NULL)
    	printf("Failed to allocate memory %d\n", errno);

	if (SPU_FIBERS > 1)
    	threadNo = CreateThreads(SPU_FIBERS);
    else
    	threadNo = 0;
    	
    if (threadNo >= 0)
    {
	    while(1)
	    {
		    //printf(WHERESTR "thread %d:%d is waiting for work\n", WHEREARG, thread_id, threadNo);
		    synclock = acquire(PACKAGE_ITEM, &size, WRITE);
		    if (synclock[0] >= synclock[1])
		    {
			    //printf(WHERESTR "thread %d:%d is terminating\n", WHEREARG, thread_id, threadNo);
		    	release(synclock);
		    	break;
		    }
		    	
		    itemno = WORKITEM_OFFSET + synclock[0];
		    total = synclock[1];
		    totaljobs = synclock[1];
		    //printf(WHERESTR "thread %d:%d acquired work %d of %d\n", WHEREARG, thread_id, threadNo, synclock[0], synclock[1]);
		    synclock[0]++;
	    	release(synclock);
	
			work = (struct workblock*)acquire(itemno, &size, READ);	    
		    queue = (struct coordinate*)(((void*)work) + sizeof(struct workblock));
		   	//printf("SPU recieved a work block with %d items\n", (*work).worksize);
		    for(i = 0; i < (*work).worksize; i++)
		    {
		    	if (i == (*work).worksize_delta)
		    		(*work).item_length--;
		    		
		        memcpy(places, queue, sizeof(struct coordinate) * (*work).item_length);
		
				/*if (((i % 20) == 0) || (i >= 220))
				{
					printf("SPU is folding: (%d)\n", i);
					printmap(places, (*work).item_length);
					//sleep(5);
				}*/
				
				initialize_map(places, (*work).item_length);
		        fold(places, (*work).item_length);
		        queue += (*work).item_length;
	    	}
	    	release(work);
		    //printf(WHERESTR "thread %d:%d has completed work %d of %d\n", WHEREARG, thread_id, threadNo, itemno - WORKITEM_OFFSET, total);
	    	totalwork++;
	    	//printf("Done folding work block at SPU, score: %d\n", bestscore);
	    	//printmap(winner, prototein_length);
	
			//printf("Writing a score %d\n", bestscore);	        
	    }
		
		if (SPU_FIBERS > 1)
			TerminateThread();
	}

    printf(WHERESTR "SPU %d has completed %d jobs (of %d)\n", WHEREARG, thread_id, totalwork, totaljobs);
    //printf(WHERESTR "SPU %d is writing back results (ls: %d)\n", WHEREARG, thread_id, (int)winner_object);
    winner_object = (struct coordinate*)create(WINNER_OFFSET + thread_id, (sizeof(struct coordinate) * prototein_length) + sizeof(int));
    memcpy(winner_object + sizeof(int), winner, sizeof(struct coordinate) * prototein_length);
    ((int*)winner_object)[0] = bestscore;
	release(winner_object);
	
   	FREE(prototein);
    FREE(places);
   	FREE(map);

    //printf(WHERESTR "thread %d completed\n", WHEREARG, thread_id);
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
	int score, x, y;	
	size_t i;
	score = 0;
	
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
		for(i = 0; i<places_length; i++)
			winner[i] = place[i];

		//printf("New winner: (%d)\n", bestscore);
		//printmap(winner, winner_length);
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

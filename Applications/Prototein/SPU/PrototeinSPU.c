#include "../PPU/PrototeinShared.h"

#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include <errno.h>
#include <malloc_align.h>
#include <string.h>
#include <dsmcbe_spu.h>
#include <dsmcbe_csp.h>
#include <debug.h>
#include <dsmcbe.h>

//#define DOUBLE_BUFFER

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

void initialize_map(struct coordinate* place, unsigned int places_length)
{
	size_t i;
	for(i = 0; i< MAP_SIZE; i++)
		map[i] = ' ';

    for (i = 0; i < places_length; i++)
        UPDATE_MAP(place[i].x, place[i].y, prototein[i]);
}

unsigned int thread_id;
int thread_no;

int FoldPrototein(unsigned long long id)
{
    struct coordinate* places;
    struct coordinate* queue;
    struct workblock* work;
    void* prototein_object;
    void* winner_object;
    unsigned long size;
#ifndef USE_CHANNEL_COMMUNICATION
    unsigned int* synclock;
    GUID itemno;
#endif

    int threadNo;

#ifdef DOUBLE_BUFFER
	unsigned int work2 = UINT_MAX;
	unsigned int item2 = UINT_MAX;
	unsigned int lastOne = 0;
	unsigned int firstOne = 1;
#endif

    size_t i;
    
    //Remove warning about unused parameter
    id = 0;
    bestscore = -9999999;
    prototein = NULL;
    
    //printf(WHERESTR "Started SPU\n", WHEREARG);
    dsmcbe_initialize();

#ifdef USE_CHANNEL_COMMUNICATION
    CSP_SAFE_CALL("read setup", dsmcbe_csp_channel_read(PROTOTEIN, &size, &prototein_object));
#else
    prototein_object = dsmcbe_acquire(PROTOTEIN, &size, ACQUIRE_MODE_WRITE);
#endif
    //printf(WHERESTR "SPU got prototein @: %d\n", WHEREARG, (unsigned int)prototein_object);
    
    thread_id = ((unsigned int*)prototein_object)[0];
    ((unsigned int*)prototein_object)[0]++;
    prototein_length = ((unsigned int*)prototein_object)[1];
    
    //printf(WHERESTR "SPU %d is calling malloc for %d\n", WHEREARG, thread_id,(unsigned int)(sizeof(char) * prototein_length));
	prototein = (char*)MALLOC(sizeof(char) * prototein_length);
	memcpy(prototein, prototein_object + (sizeof(unsigned int) * 2), prototein_length);
    //printf(WHERESTR "SPU called malloc\n", WHEREARG);

    map = (char*) MALLOC(MAP_SIZE);
#ifdef USE_CHANNEL_COMMUNICATION
    CSP_SAFE_CALL("allocate winner", dsmcbe_csp_item_create(&winner_object, (sizeof(struct coordinate) * prototein_length) + (sizeof(int) * 2)));
    winner = (struct coordinate*)(&winner_object + (sizeof(int) * 2));
#else
    winner = MALLOC((sizeof(struct coordinate) * prototein_length));
#endif

	places = (struct coordinate*)MALLOC(sizeof(struct coordinate) * prototein_length);
    //printf(WHERESTR "SPU read prototein: %s, and got ID: %d\n", WHEREARG, prototein, thread_id);
#ifdef USE_CHANNEL_COMMUNICATION
	CSP_SAFE_CALL("forward prototein", dsmcbe_csp_channel_write(PROTOTEIN, prototein_object));
#else
	dsmcbe_release(prototein_object);
#endif
	prototein_object = NULL;

    //printf(WHERESTR "Released GUID 2\n", WHEREARG);
    
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
#ifndef USE_CHANNEL_COMMUNICATION
#ifdef DOUBLE_BUFFER
		    
		    if (lastOne)
		    	break;

		    synclock = dsmcbe_acquire(PACKAGE_ITEM, &size, ACQUIRE_MODE_WRITE);
		    	
		    if (synclock[0] >= synclock[1])
		    	lastOne = 1;

			if (firstOne && synclock[0] < synclock[1])
			{
				//printf(WHERESTR "thread %d is pre_reading %d\n", WHEREARG, thread_id,  WORKITEM_OFFSET + synclock[0]);
				firstOne = 0;
				item2 = WORKITEM_OFFSET + synclock[0];
				work2 = dsmcbe_beginAcquire(item2, ACQUIRE_MODE_READ);
				synclock[0]++;
			}

		    itemno = WORKITEM_OFFSET + synclock[0];
		    total = synclock[1];
		    synclock[0]++;
#else
		    synclock = dsmcbe_acquire(PACKAGE_ITEM, &size, ACQUIRE_MODE_WRITE);

		    if (synclock[0] >= synclock[1])
		    {
			    //printf(WHERESTR "thread %d:%d is terminating\n", WHEREARG, thread_id, threadNo);
		    	dsmcbe_release(synclock);
		    	break;
		    }
		    	
		    itemno = WORKITEM_OFFSET + synclock[0];
		    total = synclock[1];
		    synclock[0]++;
#endif 
		    dsmcbe_release(synclock);
#endif
	    	//printf("thread %d:%d End - Release write SYNCLOCK\n", thread_id, threadNo);
	
		    thread_no = threadNo;
		    //printf("thread %d:%d Start - Acquire read WORK\n", thread_id, threadNo);
#ifdef DOUBLE_BUFFER

			if (work2 != UINT_MAX)
			{
				//printf(WHERESTR "thread %d is finalizing acquire of %d\n", WHEREARG, thread_id,  item2);
				work = dsmcbe_endAsync(work2, &size);
			}
			if (!lastOne)
			{
				//printf(WHERESTR "thread %d is pre_reading %d\n", WHEREARG, thread_id,  itemno);
				item2 = itemno;
				work2 = dsmcbe_beginAcquire(item2, ACQUIRE_MODE_READ);
			}
#else
#ifdef USE_CHANNEL_COMMUNICATION
			//printf(WHERESTR "thread %d is reading %d\n", WHEREARG, thread_id,  WORKITEM_CHANNEL);
			if (dsmcbe_csp_channel_read(WORKITEM_CHANNEL, &size, (void**)&work) == CSP_CALL_POISON)
			{
				//printf(WHERESTR "thread %d has got poison from %d\n", WHEREARG, thread_id,  WORKITEM_CHANNEL);
				break;
			}

			//printf(WHERESTR "thread %d has read %d, with size: %d\n", WHEREARG, thread_id,  WORKITEM_CHANNEL, size);
#else
			work = (struct workblock*)dsmcbe_acquire(itemno, &size, ACQUIRE_MODE_READ);
#endif
#endif
			//printf("thread %d:%d End - Acquire read WORK\n", thread_id, threadNo);	    
		    thread_no = threadNo;
		    queue = (struct coordinate*)(((void*)work) + sizeof(struct workblock));

		    //printf(WHERESTR "SPU recieved a work block with %d items\n", WHEREARG, (*work).worksize);

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
		    thread_no = threadNo;

		    //printf("thread %d:%d Start - Release read WORK\n", thread_id, threadNo);
#ifdef USE_CHANNEL_COMMUNICATION
		    CSP_SAFE_CALL("release work", dsmcbe_csp_item_free(work));
#else
		    dsmcbe_release(work);
#endif
	    	//printf("thread %d:%d End - Release read WORK\n", thread_id, threadNo);
		    thread_no = threadNo;
		    //printf(WHERESTR "thread %d:%d has completed work %d of %d\n", WHEREARG, thread_id, threadNo, itemno - WORKITEM_OFFSET, total);
	    	totalwork++;
	    	//printf("Done folding work block at SPU, score: %d\n", bestscore);
	    	//printmap(winner, prototein_length);
	
			//printf("Writing a score %d\n", bestscore);	        
	    }
		
		if (SPU_FIBERS > 1)
			TerminateThread();
	}

    //printf(WHERESTR "SPU %d has completed %d jobs\n", WHEREARG, thread_id, totalwork);
    //printmap(winner, prototein_length);

    //printf(WHERESTR "SPU %d is writing back results (ls: %d)\n", WHEREARG, thread_id, (int)winner_object);
	//sleep((thread_id * 0.5) + 1);
#ifndef USE_CHANNEL_COMMUNICATION
#else
    CSP_SAFE_CALL("create winner object", dsmcbe_csp_item_create(&winner_object, (sizeof(struct coordinate) * prototein_length) + (sizeof(int) * 2)));
    memcpy(winner_object + (sizeof(int) * 2), winner, sizeof(struct coordinate) * prototein_length);
#endif
    //printf(WHERESTR "SPU %d is writing back results (ls: %d)\n", WHEREARG, thread_id, (int)winner_object);
    ((int*)winner_object)[0] = bestscore;
    ((int*)winner_object)[1] = totalwork;

#ifdef USE_CHANNEL_COMMUNICATION
    CSP_SAFE_CALL("write result back", dsmcbe_csp_channel_write(WINNER_CHANNEL, winner_object));
#else
    dsmcbe_release(winner_object);
#endif
    //printf(WHERESTR "SPU %d has written back results (ls: %d)\n", WHEREARG, thread_id, (int)winner_object);
	

    FREE(prototein);
    FREE(places);
   	FREE(map);
	
    //printf(WHERESTR "thread %d completed\n", WHEREARG, thread_id);
    dsmcbe_terminate();
    //printf(WHERESTR "thread %d terminating\n", WHEREARG, thread_id);

    //There seems to be some stack corruption here, so the SPE hangs if we return
    exit(0);

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

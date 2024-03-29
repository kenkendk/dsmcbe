#include <stdlib.h>
#include <spu_mfcio.h>
#include <math.h>
#include <libmisc.h>
#include <dsmcbe_spu.h>
#include "../PPU/guids.h"
#include "../Common/Common.h"
#include <debug.h>

//define PRINT


#define Y 3
#define X 3

#define RANDOM(max) ((float)(((float)rand() / (float)RAND_MAX) * (float)(max)))
#define CEIL(x) (((int)((x)/GRIDWIDTH))+1)
#define FALSE 0
#define TRUE 1
#define DEAD 2
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define STATIC
#define DOUBLEBUFFER

unsigned int speID;

struct CURRENT_GRID
{
	unsigned char x;
	unsigned char y;	
};

unsigned int CTWIDTH;
unsigned int CTHEIGTH;

unsigned int MEMORYWIDTH()
{
	return (CTWIDTH + (128 - CTWIDTH % 128));
}

float random_normal_variant(float mean, float variant)
{
	float V1, V2, fac;
	float r = 1.0;
	
	while (r >= (float)1.0 && r != (float)0.0)
	{
		V1 = (float)2.0 * RANDOM(1.0) - (float)1.0;
		V2 = (float)2.0 * RANDOM(1.0) - (float)1.0;
		r = (V1 * V1) + (V2 * V2);
	}
	fac = sqrtf((float)-2.0 * logf(r) / r);

	return (float)((V2 * fac * variant) + mean);
}

int canon(struct POINTS* points, float ax, float ay, int pcnt, unsigned char* buffer, struct CURRENT_GRID current_grid)
{
	int i;
	int more = FALSE;
	int skip;
					
						
	//printf("Canon firering %i shots in grid(%i,%i)\n", pcnt, current_grid.x, current_grid.y);
		
	int width = MIN(GRIDWIDTH, CTWIDTH-(current_grid.x * GRIDWIDTH));	
	int heigth = MIN(GRIDHEIGTH, CTHEIGTH-(current_grid.y * GRIDHEIGTH));
	
	int memory_width = GRIDWIDTH + ((128 - GRIDWIDTH) % 128);

	int minx = current_grid.x * GRIDWIDTH;		
	int maxx = minx + width; 
		 
	int miny = current_grid.y * GRIDHEIGTH;
	int maxy = miny + heigth;
	
	float x, y, sx, sy, dx, dy;
			
	int xx, yy;
	
	for(i=0; i<pcnt; i++)
	{		
		if(points[i].alive == TRUE)
		{		
			x = points[i].x;
			y = points[i].y;
					
			xx = (int)x;
			yy = (int)y;
	
			sx = random_normal_variant(0.0, 0.15);
			sy = random_normal_variant(0.0, 0.15);	
				
			dx = ax + sx;
			dy = ay + sy;
						
			skip = FALSE;						
			
			while(xx >= minx && yy >= miny && xx < maxx && yy < maxy)
			{					
				if ((RANDOM(10) * (256-buffer[((yy-miny)*memory_width)+(xx-minx)])) > (float)1700.0)
				{				
					points[i].alive = FALSE;
					points[i].x = xx;
					points[i].y = yy;
					skip = TRUE;												
					break;
				}

				x = x + dx;
				y = y + dy;

				xx = (int)x;
				yy = (int)y;		
			}
						 
			if (!skip)
			{			 							 
				if(xx < 0 || yy < 0 || xx >= (int)CTWIDTH || yy >= (int)CTHEIGTH)
				{
					points[i].alive = DEAD;
					points[i].x = 0;
					points[i].y = 0;
					continue;				
				}
								
				if (xx == minx - 1)
					xx = xx - 1;
				else if (xx == maxx)
					xx = xx + 1;
				else if (yy == miny -1)
					yy = yy - 1;
				else if (yy == maxy)
					yy = yy + 1;		
				
				points[i].x = xx;
				points[i].y = yy;
				
				more = TRUE;
			}
		}
	}

	//printf("Canon firered %i shots in grid(%i,%i)\n", pcnt, current_grid.x, current_grid.y);
	return more;
}

void calc(int id, unsigned char* buffer) {

	int x, y;
	int sum = 0;
	id = id	; 
					
	for(y = 0; y < GRIDHEIGTH; y++) {
		for(x = 0; x < GRIDWIDTH; x++) {
			sum += buffer[(y * GRIDWIDTH)+x];
		}
	}
	//printf("SPU: Buffer with id: %i value is: %i\n", id, sum);
}

int main(unsigned long long speid, unsigned long long argp, unsigned long long envp)
{
	srand(1);
	unsigned int i,j;
	
	
	dsmcbe_initialize();

	unsigned long size;
	unsigned int speID;	
	unsigned int* speIDs = dsmcbe_acquire(SPEID + argp, &size, ACQUIRE_MODE_WRITE);
	speID = *speIDs;
	*speIDs = *speIDs + 1;
	dsmcbe_release(speIDs);
	unsigned int largestPoints = 0; 
			
	int jobID = 0;
	
	struct PACKAGE* package; 

	while(1) 
	{

		//printf(WHERESTR "SPU %i: Starting acquire package with id %i\n", WHEREARG, speID, JOB+jobID);	
		package = dsmcbe_acquire(JOB+jobID, &size, ACQUIRE_MODE_READ);
		//printf(WHERESTR "SPU %i: Acquire for package returned pointer id %i\n", WHEREARG, speID, package);

		//printf("SPU %i: Ready to start\n", speID);
			
		// Get canon information
		// Position(x,y) Angel(ax,ay), Shots(S)	
		unsigned int maxpid = package->maxid;
		unsigned int canonS = package->shots_spu;
		unsigned int canonX = package->canonX;
		unsigned int canonY = package->canonY;
		unsigned int rounds = package->tot_shots_spu;
		unsigned int pid = 0;
				
#ifdef STATIC
		unsigned int firstPoint = 0;
		unsigned int lastPoint = 0;
#else
		unsigned int firstPoint = (((RESULTEND - RESULT + 1) /  rounds) *  speID) + RESULT;
		unsigned int lastPoint = firstPoint;
		//printf("SPU %i: first %i\n", speID, firstPoint);		
#endif		
		float canonAX = package->canonAX;
		float canonAY = package->canonAY;
			
		CTWIDTH = package->width;
		CTHEIGTH = package->heigth;			

		dsmcbe_release(package);
				
		//printf("SPU %i: rounds %i\n", speID, rounds);

#ifdef STATIC						
		for(j = 0; j < rounds; j++)
		{
			// Make points buffer
			pid = (speID * rounds) + j;
			lastPoint = pid + RESULT;
#else
		while(pid < maxpid)
		{
			//printf("spu %i: Trying acquire\n", speID);
			package = dsmcbe_acquire(JOB+jobID, &size, ACQUIRE_MODE_WRITE);
			//printf("spu %i: Releasing\n", speID);
			pid = package->id;
			package->id = pid + 1;
			dsmcbe_release(package);
			//printf("spu %i: Done\n", speID);
				
#endif
			if (pid >= maxpid)		
				break;						
						
			struct POINTS* points; 
			
			if (lastPoint > largestPoints)
			{
				points = dsmcbe_create(lastPoint, sizeof(struct POINTS) * canonS);
				largestPoints = lastPoint;
			}
			else
				points = dsmcbe_acquire(lastPoint, &size, ACQUIRE_MODE_WRITE);
				
			//printf(WHERESTR "SPU %i: Acquire for points returned pointer id %i\n", WHEREARG, speID, points);
	
			// Set current_grid
			struct CURRENT_GRID current_grid;
			current_grid.x = 0;
			current_grid.y = 0;
			
			struct CURRENT_GRID next_grid;
			next_grid.x = 0;
			next_grid.y = 0;
					
			for(i = 0; i < canonS; i++)
			{
				points[i].alive = TRUE;
				points[i].x = canonX;
				points[i].y = canonY;
			}
			
			int more_to_do = TRUE;

#ifdef DOUBLEBUFFER
			unsigned char* buffer;
			unsigned int id = (GRID00IMAGE + (current_grid.y * 100) + (current_grid.x * 10)); 
			
			
			unsigned int bufferID1 = 0;
			unsigned int bufferID2 = 0;
    		unsigned int currentID = 0;			

			//printf(WHERESTR "SPU %i: Starting acquire buffer with id %i\n", WHEREARG, speID, id);
			bufferID1 = dsmcbe_beginAcquire(id, ACQUIRE_MODE_READ);
			currentID = bufferID1;

			while(more_to_do)
			{											
				next_grid.x = current_grid.x + 1;
				if(next_grid.x == X)
				{
					next_grid.y = (current_grid.y + 1);
					next_grid.x = 0;
					i = 0;
					if(next_grid.y == Y)
					{
						next_grid.x = 0;
						next_grid.y = 0;

						id = (GRID00IMAGE + (next_grid.y * 100) + (next_grid.x * 10));
						//printf(WHERESTR "SPU %i: Starting acquire buffer with id %i\n", WHEREARG, speID, id);
						if (currentID == bufferID1)
							bufferID2 = dsmcbe_beginAcquire(id, ACQUIRE_MODE_READ);
						else
							bufferID1 = dsmcbe_beginAcquire(id, ACQUIRE_MODE_READ);

						buffer = dsmcbe_endAsync(currentID, &size);
						//printf(WHERESTR "SPU %i: Acquire for buffer returned pointer id %i\n", WHEREARG, speID, buffer);
						more_to_do = canon(points, canonAX, canonAY, canonS, buffer, current_grid);			
						dsmcbe_release(buffer);
						//printf(WHERESTR "SPU %i: Released buffer with pointer %i\n", WHEREARG, speID, buffer);

						if(!more_to_do)
						{
							buffer = dsmcbe_endAsync(currentID == bufferID1 ? bufferID2 : bufferID1, &size);
							//printf(WHERESTR "SPU %i: Acquire for buffer returned pointer id %i\n", WHEREARG, speID, buffer);
							dsmcbe_release(buffer);
							//printf(WHERESTR "SPU %i: Released buffer with pointer %i\n", WHEREARG, speID, buffer);
							//printf(WHERESTR "No more to do - Last\n", WHEREARG);
							break;
						}
						current_grid.x = next_grid.x;
						current_grid.y = next_grid.y;
						currentID = currentID == bufferID1 ? bufferID2 : bufferID1;
						//printf(WHERESTR "Starting all over!\n", WHEREARG);						
						continue;
					}				
				}
				
				id = (GRID00IMAGE + (next_grid.y * 100) + (next_grid.x * 10));
				//printf(WHERESTR "SPU %i: Starting acquire buffer with id %i\n", WHEREARG, speID, id);
				if (currentID == bufferID1)
					bufferID2 = dsmcbe_beginAcquire(id, ACQUIRE_MODE_READ);
				else
					bufferID1 = dsmcbe_beginAcquire(id, ACQUIRE_MODE_READ);
								
				buffer = dsmcbe_endAsync(currentID, &size);
				//printf(WHERESTR "SPU %i: Acquire for buffer returned pointer id %i\n", WHEREARG, speID, buffer);
				more_to_do = canon(points, canonAX, canonAY, canonS, buffer, current_grid);
				dsmcbe_release(buffer);
				//printf(WHERESTR "SPU %i: Released buffer with pointer %i\n", WHEREARG, speID, buffer);
				
				if(!more_to_do)
				{
					buffer = dsmcbe_endAsync(currentID == bufferID1 ? bufferID2 : bufferID1, &size);
					//printf(WHERESTR "SPU %i: Acquire for buffer returned pointer id %i\n", WHEREARG, speID, buffer);
					dsmcbe_release(buffer);
					//printf(WHERESTR "SPU %i: Released buffer with pointer %i\n", WHEREARG, speID, buffer);
					//printf(WHERESTR "No more to do - Middel\n", WHEREARG);
				}
				current_grid.x = next_grid.x;
				current_grid.y = next_grid.y;		
				currentID = currentID == bufferID1 ? bufferID2 : bufferID1;
			}
#else
			unsigned char* buffer;
			unsigned int id;

			while(more_to_do)
			{
				id = (GRID00IMAGE + (current_grid.y * 100) + (current_grid.x * 10));
				buffer = dsmcbe_acquire(id, &size, ACQUIRE_MODE_READ);
							
				next_grid.x = current_grid.x + 1;
				if(next_grid.x == 3)
				{
					next_grid.y = (current_grid.y + 1);
					next_grid.x = 0;
					i = 0;
					if(next_grid.y == 3)
					{
						more_to_do = canon(points, canonAX, canonAY, canonS, buffer, current_grid);			
						dsmcbe_release(buffer);
						if(!more_to_do)
						{
							break;
						}
						next_grid.x = 0;
						next_grid.y = 0;
						current_grid.x = next_grid.x;
						current_grid.y = next_grid.y;						
						continue;
					}				
				}
										
				more_to_do = canon(points, canonAX, canonAY, canonS, buffer, current_grid);
																							
				current_grid.x = next_grid.x;
				current_grid.y = next_grid.y;
		
				dsmcbe_release(buffer);
			}
#endif
			dsmcbe_release(points);
			lastPoint++;
			//printf(WHERESTR "SPU %i: Released points with pointer %i\n", WHEREARG, speID,  points);
		}

		//printf("SPU %i: Creating FINISH package with id %i\n", speID, FINISHED + (jobID * 100) + speID);
		unsigned int* ptr = dsmcbe_create(FINISHED + (jobID * 100) + speID, sizeof(unsigned int) * 2);
		ptr[0] = firstPoint;
		ptr[1] = lastPoint;
		//printf("SPU %i: First %u, last %u\n", speID, ptr[0], ptr[1]); 									 
		dsmcbe_release(ptr);
		//printf("SPU %i: Created FINISH package with id %i\n", speID, FINISHED + (jobID * 1000) + speID);
		jobID++;
	}

	//Remove compiler warning
	envp = 0;
	speid = 0;

	return 0;
}

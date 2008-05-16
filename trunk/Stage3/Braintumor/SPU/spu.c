#include <stdio.h>
#include <stdlib.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <profile.h>
#include <math.h>
#include <libmisc.h>
#include <dsmcbe_spu.h>
#include <unistd.h>
#include "../PPU/guids.h"
#include "../Common/Common.h"
#include "../DSMCBE/common/debug.h"

#define BLOCKSIZE (GRIDWIDTH * GRIDHEIGTH) // In bytes
#define NUM_OF_BUFFERS 2
#define AVALIBLE_STORAGE (BLOCKSIZE * NUM_OF_BUFFERS) // In bytes
#define MAX_BUFFER_SIZE (AVALIBLE_STORAGE / NUM_OF_BUFFERS) // In bytes
#define RANDOM(max) ((float)(((float)rand() / (float)RAND_MAX) * (float)(max)))
#define CEIL(x) (((int)((x)/GRIDWIDTH))+1)
#define FALSE 0
#define TRUE 1
#define DEAD 2
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

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
	
	int memory_width = width + ((128 - width) % 128);

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
					
	for(y = 0; y < GRIDHEIGTH; y++) {
		for(x = 0; x < GRIDWIDTH; x++) {
			sum += buffer[(y * GRIDWIDTH)+x];
		}
	}
	printf("SPU: Buffer with id: %i value is: %i\n", id, sum);
}

int main()
{
	prof_clear();
	prof_start();
	
	srand(1);
	unsigned int i;
	
	initialize();

	//printf("SPU: Ready to start\n");
	
	int jobID = 0;
		
	while(1) {
		
		// Make points buffer
		struct POINTS* points;
		struct PACKAGE* package;

		unsigned long size;
		//printf("spu.c: Trying to acquire JOB\n");
		package = acquire(JOB+jobID, &size, WRITE);
		//printf("spu.c: Finished acquiring JOB\n");
		// Get canon information
		// Position(x,y) Angel(ax,ay), Shots(S)	
		unsigned int pid = package->id;
		unsigned int maxpid = package->maxid;
		unsigned int canonS = package->shots_spu;
		unsigned int canonX = package->canonX;
		unsigned int canonY = package->canonY;
		
		float canonAX = package->canonAX;
		float canonAY = package->canonAY;
			
		CTWIDTH = package->width;
		CTHEIGTH = package->heigth;			
	
		//printf("spu.c: pid: %i, maxpid %i, canonS: %i, canonX: %i, canonY: %i, canonAX: %f, canonAY: %f, width: %i, heigth: %i\n", pid, maxpid, canonS, canonX, canonY, canonAX, canonAY, CTWIDTH, CTHEIGTH);
			
		printf(WHERESTR "Canon %i with PID: %i out of %i\n", WHEREARG, jobID + 1, pid, maxpid);
	
		if(pid >= maxpid) {
			release(package);
			unsigned long size; 
			int* count = acquire(COUNT+jobID, &size, WRITE);
			*count += 1;
			release(count);
			jobID++;
			continue;
		}
			
		package->id = pid + 1;
		release(package);
		//clean(JOB+jobID);
		
		//printf("spu.c: Trying to acquire RESULT\n");
		points = acquire(RESULT + pid, &size, WRITE);
		//printf("spu.c: Finished acquiring RESULT\n");

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
		
		while(more_to_do)
		{			
		
			unsigned int id = (GRID00IMAGE + (current_grid.y * 100) + (current_grid.x * 10));
			unsigned long size;
			
			//printf("spu.c: Trying to acquire BUFFER\n");
			unsigned char* buffer;
			buffer = acquire(id, &size, READ);			
			//printf(WHERESTR "Finished acquiring BUFFER %i\n", WHEREARG, id);
					
			next_grid.x = current_grid.x + 1;
			if(next_grid.x == 3)
			{
				next_grid.y = (current_grid.y + 1);
				next_grid.x = 0;
				i = 0;
				if(next_grid.y == 3)
				{
					more_to_do = canon(points, canonAX, canonAY, canonS, buffer, current_grid);			
					//printf("spu.c: Trying to release BUFFER\n");
					release(buffer);
					//printf("spu.c: Finished releasing BUFFER\n");
					if(!more_to_do)
					{
						//printf("All points in POINTS are dead!!\n");
						break;
					}
					next_grid.x = 0;
					next_grid.y = 0;
					current_grid.x = next_grid.x;
					current_grid.y = next_grid.y;						
					//printf("Starting all over because there is more work to do!\n");
					continue;
				}				
			}
									
			more_to_do = canon(points, canonAX, canonAY, canonS, buffer, current_grid);
																						
			current_grid.x = next_grid.x;
			current_grid.y = next_grid.y;
			/*
			if(more_to_do)	
				printf("More work to do\n");
			else
				printf("No more work to do\n");
			*/
			//printf("spu.c: Trying to release BUFFER\n");			
			release(buffer);
			//printf("spu.c: Finished releasing BUFFER\n");
		}
		release(points);
		//clean(RESULT + pid);
	}
	
	prof_stop();
 	
	return 0;
}


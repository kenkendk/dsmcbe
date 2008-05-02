#include <stdio.h>
#include <stdlib.h>
#include <malloc_align.h>
#include <free_align.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <profile.h>
#include <math.h>
#include <libmisc.h>
#include "../Common/Common.h"
#include <dsmcbe_spu.h>
#include "../PPU/guids.h"

#define GRIDWIDTH 256 // Number of pixels
#define GRIDHEIGTH 256 // Number of pixels
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

struct DMA_LIST_ELEMENT {
	union {
		unsigned int all32;
		struct {
			unsigned int stall : 1;
			unsigned int reserved : 15;
			unsigned int nbytes : 16;
		} bits;
	} size;
	unsigned int ea_low;
};

struct DMA_LIST_ELEMENT list[GRIDHEIGTH] __attribute__ ((aligned (16)));

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
							
	printf("Canon firering %i shots in grid(%i,%i)\n", pcnt, current_grid.x, current_grid.y);
		
	int width = MIN(GRIDWIDTH, CTWIDTH-(current_grid.x * GRIDWIDTH));
	int heigth = MIN(GRIDHEIGTH, CTHEIGTH-(current_grid.y * GRIDHEIGTH));

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
				if ((RANDOM(10) * (256-buffer[((yy-miny)*width)+(xx-minx)])) > (float)1700.0)
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
/*
	printf("Canon finished firering %i shots in grid(%i,%i)\n", pcnt, current_grid.x, current_grid.y);
	printf("Counter: %i\n\n", counter);
*/	
	return more;
}

int main()
{
	prof_clear();
	prof_start();
	
	srand(1);
	unsigned int i;

	initialize();
	
	struct PACKAGE* package;
	unsigned long size;
	printf("spu.c: Trying to acquire JOB\n");
	package = acquire(JOB, &size);
	printf("spu.c: Finished acquiring JOB\n");
	// Get canon information
	// Position(x,y) Angel(ax,ay), Shots(S)	
	unsigned int canonS = package->shots_spu;
	unsigned int canonX = package->canonX;
	unsigned int canonY = package->canonY;
	
	float canonAX = package->canonAX;
	float canonAY = package->canonAY;
	
	printf("spu.c: canonS: %i, canonX: %i, canonY: %i, canonAX: %f, canonAY: %f\n", canonS, canonX, canonY, canonAX, canonAY);

	// Make points buffer
	struct POINTS* points;

	printf("SPU: Ready to start\n");

	
	CTWIDTH = 576;
	CTHEIGTH = 708;
		
	unsigned char* buffer0;
	unsigned char* buffer1;

	while(1)
	{
		unsigned char* current_buffer = buffer0;
		unsigned char* next_buffer = buffer1;
	
		printf("spu.c: Trying to acquire RESULT\n");
		unsigned long size;
		points = acquire(RESULT, &size);
		printf("spu.c: Finished acquiring RESULT\n");
				
		do
		{			
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
			
			int id = (1000 + (current_grid.y * 10) + current_grid.x);
			unsigned long size;
			
			printf("spu.c: Trying to acquire %i\n", id);
			current_buffer = acquire(id, &size);
			printf("spu.c: Finished acquiring %i\n", id);
			
			while(1)
			{	
				int more_to_do = TRUE;
				
				next_grid.x = current_grid.x + 1;
				if(next_grid.x == 3)
				{
					next_grid.y = (current_grid.y + 1);
					next_grid.x = 0;
					i = 0;
					if(next_grid.y == 3)
					{
						more_to_do = canon(points, canonAX, canonAY, canonS, current_buffer, current_grid);			
						if(!more_to_do)
						{
							printf("All points in POINTS are dead!!\n");
							break;
						}
						next_grid.x = 0;
						next_grid.y = 0;
						printf("Starting all over because there is more work to do!\n");				
					}				
				}
				
				id = (1000 + (next_grid.y * 10) + next_grid.x);	

				printf("spu.c: Trying to acquire next %i\n", id);					
				next_buffer = acquire(id, &size);
				printf("spu.c: Finished acquiring next %i\n", id);
											
				more_to_do = canon(points, canonAX, canonAY, canonS, current_buffer, current_grid);
				
				release(current_buffer);
						
				if(!more_to_do)
				{
					printf("All points in POINTS are dead!!\n");
					break;
				}
				
				// Swap current_buffer and next_buffer
				current_buffer = (current_buffer == buffer0 ? buffer1 : buffer0);
				next_buffer = (next_buffer == buffer0 ? buffer1 : buffer0);
													
				current_grid.x = next_grid.x;
				current_grid.y = next_grid.y;	
			}
				
			// Send points to energyEA
			release(points);
			
			unsigned int tag_mask = (1 << 10);
			mfc_write_tag_mask(tag_mask);
			mfc_read_tag_status_any();
			
			// Signal done with assignment
			spu_write_out_mbox(1);		
		}while(1);
		
		// Continue with new canon or 
		if (spu_read_in_mbox() == 0)
		{
			//free(points);
			break;
		}
		//free(points);
 	}
	
	prof_stop();
 	
	return 0;
}


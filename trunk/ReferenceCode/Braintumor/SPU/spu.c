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


//Returns the groupid assigned to the current buffer
// b0 = buffer0
// b1 = buffer1
// current = the current buffer
int GetDMAGroupID(unsigned char* b0, unsigned char* b1, unsigned char* current)
{
	if (current == b0)
		return 1;
	else if (current == b1)
		return 0;
	else
		return -1;
}

//Waits for a DMA transfer to complete
// b0 = buffer0
// b1 = buffer1
// current = the current buffer
void WaitForDMATransfer(unsigned char* b0, unsigned char* b1, unsigned char* current)
{
	int groupid = GetDMAGroupID(b0, b1, current);
	unsigned int tag_mask = (1 << groupid);
	mfc_write_tag_mask(tag_mask);
	mfc_read_tag_status_any();
	//printf("DMA Transfer %d done\n", current == b0 ? (int)b0 : (int)b1);
}

void SendResult(struct POINTS* source, unsigned int ea_low)
{
	unsigned int i = 0;
	unsigned int tagid;
	unsigned int listsize = sizeof(struct DMA_LIST_ELEMENT);

	for(i = 0; i < 2; i++)
	{
		list[i].size.all32 = sizeof(struct POINTS) * 1024;
		list[i].ea_low = ea_low;
		ea_low += sizeof(struct POINTS) * 1024;
	}
	
	/* Specify the list size and initiate the list transfer */
	tagid = 10;
	listsize *= i;
 
	mfc_putl(source, list[0].ea_low, list, listsize, tagid, 0, 0);	
}

void StartDMAListTransferOfNext(unsigned char* b0, unsigned char* b1, unsigned char* current, unsigned int ea_low, struct CURRENT_GRID current_grid)
{
	unsigned char* target = (current == b0 ? b1 : b0);
	unsigned int i = 0;
	unsigned int tagid;
	unsigned int listsize = sizeof(struct DMA_LIST_ELEMENT);
	unsigned int width = MIN(GRIDWIDTH, CTWIDTH-(current_grid.x * GRIDWIDTH));
	unsigned int heigth = MIN(GRIDHEIGTH, CTHEIGTH-(current_grid.y * GRIDHEIGTH)); 
	ea_low += ((current_grid.y * GRIDHEIGTH) * MEMORYWIDTH()) + (current_grid.x * GRIDWIDTH);
			
	if (ea_low % 128 != 0)
		printf("Not alligned!\n");
	
	for(i = 0; i < heigth; i++)
	{
		list[i].size.all32 = width;
		list[i].ea_low = ea_low;
		
		ea_low += MEMORYWIDTH();
		if (ea_low % 128 != 0)
			printf("Not alligned!\n");
	}
	
	/* Specify the list size and initiate the list transfer */
	tagid = GetDMAGroupID(b0, b1, target);
	listsize *= i;

	//printf("Starting DMA transfer (%d,%d) with target: %d, tagid: %d, listsize: %d, size: %d, maxy: %d\n", current_grid.x, current_grid.y, (int)target, tagid, i, width, heigth); 
	mfc_getl(target, list[0].ea_low, list, listsize, tagid, 0, 0);
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
	
	// Get CT information
	// Pointer to start (ea)
	unsigned int ctEA;	

	// Get canon information
	// Position(x,y) Angel(ax,ay), Shots(S)
	unsigned int canonS;
	unsigned int canonX;
	unsigned int canonY;
	
	unsigned int canonAX_Store;
	unsigned int canonAY_Store; 
	
	float* canonAX;
	float* canonAY;
	
	// Where to put the result (ea)
	unsigned int resultEA;
	
	// Make points buffer
	struct POINTS* points;
	
	CTWIDTH = spu_read_in_mbox();
	CTHEIGTH = spu_read_in_mbox();
		
	canonAX = (float*)&canonAX_Store;
	canonAY = (float*)&canonAY_Store;

	unsigned char* buffer0 = (unsigned char*)_malloc_align(sizeof(unsigned char) * 75 * 1024, 7);
	if(buffer0 == NULL)
		printf("Error allocation memory");
	
	unsigned char* buffer1 = (unsigned char*)_malloc_align(sizeof(unsigned char) * 75 * 1024, 7);
	if(buffer1 == NULL)
		printf("Error allocation memory");
		
	while(1)
	{	
		ctEA = spu_read_in_mbox();
		resultEA = spu_read_in_mbox();
			
		// If all work is done, return
		if((canonS = spu_read_in_mbox()) == 0) 
			return 0;
				
		canonX = spu_read_in_mbox();
		canonY = spu_read_in_mbox();
		canonAX_Store = spu_read_in_mbox();
		canonAY_Store = spu_read_in_mbox();
			
		unsigned char* current_buffer = buffer1;
	
		points = (struct POINTS*)malloc(sizeof(struct POINTS) * canonS);
				
		do
		{
			// TODO: Man kunne spare en DMA transfer, hvis man mellem to job hentede den første dma transfer.
			//printf("Starting a new job\n");
			
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
	
			// Issue DMA list transfer to get first map 
			StartDMAListTransferOfNext(buffer0, buffer1, current_buffer, ctEA, current_grid);
		
			// Swap current_buffer
			current_buffer = (current_buffer == buffer0 ? buffer1 : buffer0);
			
			// Wait for DMA transfer to complete
			WaitForDMATransfer(buffer0, buffer1, current_buffer);
			
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
						more_to_do = canon(points, (*canonAX), (*canonAY), canonS, current_buffer, current_grid);			
						if(!more_to_do)
						{
							//printf("All points in POINTS are dead!!\n");
							break;
						}
						next_grid.x = 0;
						next_grid.y = 0;
						//printf("Starting all over because there is more work to do!\n");				
					}				
				}
					
				// Issue DMA list transfer to get next map
				StartDMAListTransferOfNext(buffer0, buffer1, current_buffer, ctEA, next_grid);
			
				more_to_do = canon(points, (*canonAX), (*canonAY), canonS, current_buffer, current_grid);
						
				if(!more_to_do)
				{
					//printf("All points in POINTS are dead!!\n");
					break;
				}
				
				// Swap current_buffer
				current_buffer = (current_buffer == buffer0 ? buffer1 : buffer0);
									
				// Wait for DMA transfer to complete
				WaitForDMATransfer(buffer0, buffer1, current_buffer);
				
				current_grid.x = next_grid.x;
				current_grid.y = next_grid.y;	
			}
				
			// Send points to energyEA
			SendResult(points, resultEA);
			unsigned int tag_mask = (1 << 10);
			mfc_write_tag_mask(tag_mask);
			mfc_read_tag_status_any();
			
			// Signal done with assignment
			spu_write_out_mbox(1);		
		}while((resultEA = spu_read_in_mbox()) != 0 && (canonS = spu_read_in_mbox()) != 0);
		
		// Continue with new canon or 
		if (spu_read_in_mbox() == 0)
		{
			free(points);
			break;
		}
		free(points);		
 	}
	
 	_free_align(buffer0);
 	_free_align(buffer1);

	prof_stop();
 	
	return 0;
}


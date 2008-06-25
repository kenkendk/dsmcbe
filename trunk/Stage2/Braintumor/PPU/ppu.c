#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include <malloc_align.h>
#include <free_align.h>
#include <math.h>
#include "../Common/PPMReaderWriter.h"
#include "../Common/Common.h"
#include "../Common/StopWatch.h"
#include <unistd.h>

#include "guids.h"
#include <dsmcbe_ppu.h>

extern spe_program_handle_t SPU;

#define FALSE 0
#define TRUE 1
#define RANDOM(max) ((((float)rand() / (float)RAND_MAX) * (float)(max)))
#define MIN(a,b) ((a)<(b) ? (a) : (b))

#define SHOTS_SPU 2048
#define SHOTS (SHOTS_SPU * 480)

int WIDTH;
int HEIGTH;
int SPU_THREADS;

int MAPOFFSET(int x, int y)
{
	return ((y * WIDTH) + x);
}

int fpos(unsigned char* scale, int scale_size, unsigned int x)
{
	int i;
	
	for(i = 0; i < scale_size; i++)
        if(x <= scale[i])
			return i;
    return (i - 1);
}

void* malloc_align7(unsigned int size)
{
	return _malloc_align(size, 7);
}

void canon(int id, int shots, int shots_spu, int canonX, int canonY, float canonAX, float canonAY, unsigned char* energy)
{
	int i;
	unsigned char max_integer = (unsigned char)pow(2, sizeof(unsigned char)*8);
	
	int shotsspu = SHOTS_SPU;

	struct PACKAGE* package;
	package = create(JOB+id, sizeof(struct PACKAGE));
	package->id = 0;
	package->maxid = (shots / shots_spu);
	package->heigth = HEIGTH;
	package->width = WIDTH;
	package->shots_spu = shotsspu;
	package->canonX = canonX;
	package->canonY = canonY;
	package->canonAX = canonAX;
	package->canonAY = canonAY;
	release(package);
	
	int* count = create(COUNT+id, sizeof(int));
	*count = 0;
	release(count);
	
	unsigned long size;
	do {
		sleep(1);
		count = acquire(COUNT+id, &size);
		release(count); 
	}while(*count < SPU_THREADS);
				
	printf("\n\n\n\n\nStart working on results\n\n\n\n\n");				
				
	for(i = 0; i < (shots / shots_spu); i++) {
		unsigned long size;
		struct POINTS* points = acquire(RESULT + i, &size);	
			
		// Save results to energy
		for(i = 0; i < shots_spu; i++) {
			if(points[i].alive == FALSE)
				energy[MAPOFFSET((int)(points[i].x), (int)(points[i].y))] =  MIN(energy[MAPOFFSET((int)(points[i].x),(int)(points[i].y))] + 1, max_integer);			
		}
		release(points);
	}
}

void calc(int id, struct IMAGE_FORMAT_GREY* grid) {

	int x, y;
	int sum = 0;
					
	for(y = 0; y < GRIDHEIGTH; y++) {
		for(x = 0; x < GRIDWIDTH; x++) {
			sum += grid->image[(y * GRIDWIDTH)+x];
		}
	}
	printf("PPU: Buffer with id: %i value is: %i\n", id, sum);
}

int main(int argc, char* argv[])
{
	if(argc != 4)
	{
		printf("Wrong number of arguments \"./PPU input output\"\n");
		return -1;
	}
	char* input = argv[1];
	char* output = argv[2]; 	
	SPU_THREADS = atoi(argv[3]);
	
	pthread_t* threads;
	
	struct IMAGE_FORMAT result;
	unsigned char* energy;
	unsigned char* cmap;
	unsigned char* scale;
	int scale_size = 9;
	char timer_buffer[256];
	int y, x, i;

	WIDTH = 576;
	HEIGTH = 708;

	threads = simpleInitialize(SPU_THREADS);

	printf("Starting loading images!\n");
	int memory_width = GRIDWIDTH + ((128 - GRIDHEIGTH) % 128);
	
	struct IMAGE_FORMAT_GREY* grid00 = create(GRID00, sizeof(struct IMAGE_FORMAT_GREY));
	grid00->image = create(GRID00IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT00.ppm", malloc_align7, grid00);
	release(grid00->image);		
	release(grid00);			
	struct IMAGE_FORMAT_GREY* grid01 = create(GRID01, sizeof(struct IMAGE_FORMAT_GREY));
	grid01->image = create(GRID01IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT01.ppm", malloc_align7, grid01);
	release(grid01->image);	
	release(grid01);	
	struct IMAGE_FORMAT_GREY* grid02 = create(GRID02, sizeof(struct IMAGE_FORMAT_GREY));
	grid02->image = create(GRID02IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT02.ppm", malloc_align7, grid02);
	release(grid02->image);
	release(grid02);

	struct IMAGE_FORMAT_GREY* grid10 = create(GRID10, sizeof(struct IMAGE_FORMAT_GREY));
	grid10->image = create(GRID10IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT10.ppm", malloc_align7, grid10);
	release(grid10->image);
	release(grid10);
	struct IMAGE_FORMAT_GREY* grid11 = create(GRID11, sizeof(struct IMAGE_FORMAT_GREY));
	grid11->image = create(GRID11IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT11.ppm", malloc_align7, grid11);
	release(grid11->image);
	release(grid11);
	struct IMAGE_FORMAT_GREY* grid12 = create(GRID12, sizeof(struct IMAGE_FORMAT_GREY));
	grid12->image = create(GRID12IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT12.ppm", malloc_align7, grid12);
	release(grid12->image);
	release(grid12);

	struct IMAGE_FORMAT_GREY* grid20 = create(GRID20, sizeof(struct IMAGE_FORMAT_GREY));
	grid20->image = create(GRID20IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT20.ppm", malloc_align7, grid20);
	release(grid20->image);
	release(grid20);
	struct IMAGE_FORMAT_GREY* grid21 = create(GRID21, sizeof(struct IMAGE_FORMAT_GREY));
	grid21->image = create(GRID21IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT21.ppm", malloc_align7, grid21);
	release(grid21->image);
	release(grid21);
	struct IMAGE_FORMAT_GREY* grid22 = create(GRID22, sizeof(struct IMAGE_FORMAT_GREY));
	grid22->image = create(GRID22IMAGE, memory_width * GRIDHEIGTH * sizeof(unsigned char));
	readimage_grey("CT22.ppm", malloc_align7, grid22);
	release(grid22->image);
	release(grid22);
	
	printf("Finished loading images!\n");
	
	for(i = 0; i < (SHOTS / SHOTS_SPU); i++)
	{
		struct POINTS* points = create(RESULT + i, sizeof(struct POINTS) * SHOTS_SPU);
		release(points);	
	}
	
	energy = (unsigned char*)malloc(sizeof(unsigned char) * (HEIGTH * WIDTH));
	memset(energy, 0, sizeof(unsigned char) * (HEIGTH * WIDTH));

	cmap = (unsigned char*)malloc(sizeof(unsigned char)*(9*3));
	cmap[0] = 0; cmap[1] = 0; cmap[2] = 85;
	cmap[3] = 0; cmap[4] = 0; cmap[5] = 170;
	cmap[6] = 0; cmap[7] = 0; cmap[8] = 255;
	cmap[9] = 0; cmap[10] = 85; cmap[11] = 0;
	cmap[12] = 0; cmap[13] = 170; cmap[14] = 0;
	cmap[15] = 0; cmap[16] = 255; cmap[17] = 0;
	cmap[18] = 85; cmap[19] = 0; cmap[20] = 0;
	cmap[21] = 170; cmap[22] = 0; cmap[23] = 0;
	cmap[24] = 255; cmap[25] = 0; cmap[26] = 0;

	scale = (unsigned char*)malloc(sizeof(unsigned char));
	scale[0] = 10; scale[1] = 20; scale[2] = 30;
	scale[3] = 40; scale[4] = 50; scale[5] = 60;
	scale[6] = 70; scale[7] = 80; scale[8] = 90;
	
	srand(1);

	//Start timer!
	sw_init();
	sw_start();
	printf("Timer started\n");

	printf("Start firering canon #1\n");
	canon(0, SHOTS, SHOTS_SPU, 85, 75, 1.0, 0.8, energy);
	printf("Stopped firering canon #1\n");

	printf("Start firering canon #2\n");
	canon(1, SHOTS, SHOTS_SPU, 10, 230, 1.0, 0.0, energy);
	printf("Stopped firering canon #2\n");

	printf("Start firering canon #3\n");
	canon(2, SHOTS, SHOTS_SPU, 550, 230, -1.0, 0.0, energy);
	printf("Stopped firering canon #3\n");

	printf("Start firering canon #4\n");
	canon(3, SHOTS, SHOTS_SPU, 475, 90, -1.0, 0.75, energy);
	printf("Stopped firering canon #4\n");

	printf("Start firering canon #5\n");
	canon(4, SHOTS, SHOTS_SPU, 280, 0, 0.0, 1.0, energy);
	printf("Stopped firering canon #5\n");

	// Stop timer!
	sw_stop();
	sw_timeString(timer_buffer);
	printf("Time used: %s\n", timer_buffer);

	readimage_rgb(input, malloc, &result);
		
	// Save energy map to image
	for(y=0; y<HEIGTH; y++)
		for(x=0; x<WIDTH; x++)
			if(energy[MAPOFFSET(x,y)] > 0)
			{
				int offset = 3 * fpos(scale, scale_size, energy[MAPOFFSET(x,y)]);
				result.image[MAPOFFSET(x,y)].r = cmap[offset];
				result.image[MAPOFFSET(x,y)].g = cmap[offset+1];
				result.image[MAPOFFSET(x,y)].b = cmap[offset+2];
			}

	writeimage_rgb(output, &result);

	free(energy);
	free(cmap);
	free(scale);

	return 0;
}
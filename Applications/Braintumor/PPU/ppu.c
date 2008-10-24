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
#define DEAD 2
#define RANDOM(max) ((((float)rand() / (float)RAND_MAX) * (float)(max)))
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define SHOTS_SPU 4096
#define ROUNDS 7200
#define SHOTS (SHOTS_SPU * ROUNDS)

int WIDTH;
int HEIGTH;
int SPU_THREADS;
unsigned int id;
char* file;

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
	int i, j;
	
	int shotsspu = SHOTS_SPU;

	struct PACKAGE* package;
	package = create(JOB+id, sizeof(struct PACKAGE));
	package->id = 0;
	package->maxid = (shots / shots_spu);
	package->heigth = HEIGTH;
	package->width = WIDTH;
	package->shots_spu = shotsspu;
	package->tot_shots_spu =  ceil((SHOTS / SHOTS_SPU) / (double)(SPU_THREADS * MAX(DSMCBE_MachineCount(), 1)));
	package->canonX = canonX;
	package->canonY = canonY;
	package->canonAX = canonAX;
	package->canonAY = canonAY;
	release(package);
		
	unsigned long size;
	
	//printf("SPU_THREADS is %i and COUNT is %i\n", SPU_THREADS, DSMCBE_MachineCount());
	for(i = 0; i < SPU_THREADS * MAX(DSMCBE_MachineCount(), 1); i++)
	{
		//printf("Reading FINISH package with id %i\n", FINISHED + (id * 1000) + i);
		release(acquire(FINISHED+(id*100)+i, &size, ACQUIRE_MODE_READ));
		//printf("Read FINISH package with id %i\n", FINISHED + (id * 1000) + i);		
	}
		 				
	//printf("\n\nStart working on results\n\n");
	
	for(i = 0; i < ROUNDS; i++) {
		unsigned long size;
		//printf("Acquire Result with id %i\n", RESULT+i);
		struct POINTS* points = acquire(RESULT + i, &size, ACQUIRE_MODE_READ);	
			
		// Save results to energy
		for(j = 0; j < shots_spu; j++) {
			if(points[j].alive == FALSE)
				energy[MAPOFFSET((int)(points[j].x), (int)(points[j].y))] =  MIN(energy[MAPOFFSET((int)(points[j].x),(int)(points[j].y))] + 1, 255);
		}
		release(points);
	}
	
	//printf("\n\nEnd working on results\n\n");
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

void loadImageNormal()
{
	struct IMAGE_FORMAT_GREY* grid00 = create(GRID00, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT00.ppm", grid00, GRID00IMAGE);
	release(grid00);			
	struct IMAGE_FORMAT_GREY* grid01 = create(GRID01, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT01.ppm", grid01, GRID01IMAGE);
	release(grid01);			
	struct IMAGE_FORMAT_GREY* grid02 = create(GRID02, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT02.ppm", grid02, GRID02IMAGE);
	release(grid02);			

	struct IMAGE_FORMAT_GREY* grid10 = create(GRID10, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT10.ppm", grid10, GRID10IMAGE);
	release(grid10);			
	struct IMAGE_FORMAT_GREY* grid11 = create(GRID11, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT11.ppm", grid11, GRID11IMAGE);
	release(grid11);			
	struct IMAGE_FORMAT_GREY* grid12 = create(GRID12, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT12.ppm", grid12, GRID12IMAGE);
	release(grid12);
	
	struct IMAGE_FORMAT_GREY* grid20 = create(GRID20, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT20.ppm", grid20, GRID20IMAGE);
	release(grid20);			
	struct IMAGE_FORMAT_GREY* grid21 = create(GRID21, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT21.ppm", grid21, GRID21IMAGE);
	release(grid21);			
	struct IMAGE_FORMAT_GREY* grid22 = create(GRID22, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT22.ppm", grid22, GRID22IMAGE);
	release(grid22);			
}

void loadImageSmall()
{
	struct IMAGE_FORMAT_GREY* grid00 = create(GRID00, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT00.ppm", grid00, GRID00IMAGE);
	release(grid00);			
	struct IMAGE_FORMAT_GREY* grid01 = create(GRID01, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT01.ppm", grid01, GRID01IMAGE);
	release(grid01);			
	struct IMAGE_FORMAT_GREY* grid02 = create(GRID02, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT02.ppm", grid02, GRID02IMAGE);
	release(grid02);			
	struct IMAGE_FORMAT_GREY* grid03 = create(GRID03, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT03.ppm", grid03, GRID03IMAGE);
	release(grid03);			
	struct IMAGE_FORMAT_GREY* grid04 = create(GRID04, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT04.ppm", grid04, GRID04IMAGE);
	release(grid04);			
	
	struct IMAGE_FORMAT_GREY* grid10 = create(GRID10, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT10.ppm", grid10, GRID10IMAGE);
	release(grid10);			
	struct IMAGE_FORMAT_GREY* grid11 = create(GRID11, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT11.ppm", grid11, GRID11IMAGE);
	release(grid11);			
	struct IMAGE_FORMAT_GREY* grid12 = create(GRID12, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT12.ppm", grid12, GRID12IMAGE);
	release(grid12);			
	struct IMAGE_FORMAT_GREY* grid13 = create(GRID13, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT13.ppm", grid13, GRID13IMAGE);
	release(grid13);			
	struct IMAGE_FORMAT_GREY* grid14 = create(GRID14, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT14.ppm", grid14, GRID14IMAGE);
	release(grid14);			
	
	struct IMAGE_FORMAT_GREY* grid20 = create(GRID20, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT20.ppm", grid20, GRID20IMAGE);
	release(grid20);			
	struct IMAGE_FORMAT_GREY* grid21 = create(GRID21, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT21.ppm", grid21, GRID21IMAGE);
	release(grid21);			
	struct IMAGE_FORMAT_GREY* grid22 = create(GRID22, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT22.ppm", grid22, GRID22IMAGE);
	release(grid22);			
	struct IMAGE_FORMAT_GREY* grid23 = create(GRID23, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT23.ppm", grid23, GRID23IMAGE);
	release(grid23);			
	struct IMAGE_FORMAT_GREY* grid24 = create(GRID24, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT24.ppm", grid24, GRID24IMAGE);
	release(grid24);			
	
	struct IMAGE_FORMAT_GREY* grid30 = create(GRID30, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT30.ppm", grid30, GRID30IMAGE);
	release(grid30);			
	struct IMAGE_FORMAT_GREY* grid31 = create(GRID31, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT31.ppm", grid31, GRID31IMAGE);
	release(grid31);			
	struct IMAGE_FORMAT_GREY* grid32 = create(GRID32, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT32.ppm", grid32, GRID32IMAGE);
	release(grid32);			
	struct IMAGE_FORMAT_GREY* grid33 = create(GRID33, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT33.ppm", grid33, GRID33IMAGE);
	release(grid33);			
	struct IMAGE_FORMAT_GREY* grid34 = create(GRID34, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT34.ppm", grid34, GRID34IMAGE);
	release(grid34);			
	
	struct IMAGE_FORMAT_GREY* grid40 = create(GRID40, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT40.ppm", grid40, GRID40IMAGE);
	release(grid40);			
	struct IMAGE_FORMAT_GREY* grid41 = create(GRID41, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT41.ppm", grid41, GRID41IMAGE);
	release(grid41);			
	struct IMAGE_FORMAT_GREY* grid42 = create(GRID42, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT42.ppm", grid42, GRID42IMAGE);
	release(grid42);			
	struct IMAGE_FORMAT_GREY* grid43 = create(GRID43, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT43.ppm", grid43, GRID43IMAGE);
	release(grid43);			
	struct IMAGE_FORMAT_GREY* grid44 = create(GRID44, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT44.ppm", grid44, GRID44IMAGE);
	release(grid44);			
	
	struct IMAGE_FORMAT_GREY* grid50 = create(GRID50, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT50.ppm", grid50, GRID50IMAGE);
	release(grid50);			
	struct IMAGE_FORMAT_GREY* grid51 = create(GRID51, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT51.ppm", grid51, GRID51IMAGE);
	release(grid51);			
	struct IMAGE_FORMAT_GREY* grid52 = create(GRID52, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT52.ppm", grid52, GRID52IMAGE);
	release(grid52);			
	struct IMAGE_FORMAT_GREY* grid53 = create(GRID53, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT53.ppm", grid53, GRID53IMAGE);
	release(grid53);			
	struct IMAGE_FORMAT_GREY* grid54 = create(GRID54, sizeof(struct IMAGE_FORMAT_GREY));
	readimage_grey_DSMCBE("CT54.ppm", grid54, GRID54IMAGE);
	release(grid54);			
}

int main(int argc, char* argv[])
{

	char* input = NULL;
	char* output = NULL;	

	if(argc == 6) {
		input = argv[1];
		output = argv[2];
		SPU_THREADS = atoi(argv[3]);
		id = atoi(argv[4]);
		file = argv[5]; 	
	} else if (argc == 4) {
		id = 0;
		file = NULL; 		 		
		input = argv[1];
		output = argv[2]; 	
		SPU_THREADS = atoi(argv[3]);
	} else if (argc == 5) {
		id = 0;
		file = NULL; 		 		
		input = argv[1];
		output = argv[2]; 	
		SPU_THREADS = atoi(argv[3]);
		
	} else {
		printf("Wrong number of arguments %i\n", argc);
		return -1;
	}
	
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
	
	threads = simpleInitialize(id, file, SPU_THREADS);

	if (id == 0)
	{
		//printf("Starting loading images!\n");	
		loadImageNormal();
		//loadImageSmall();
		//printf("Finished loading images!\n");
			
		for(i = 0; i < ROUNDS; i++)
		{
			struct POINTS* points = create(RESULT + i, sizeof(struct POINTS) * SHOTS_SPU);
			memset(points, 0, sizeof(struct POINTS) * SHOTS_SPU);
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
	
		scale = (unsigned char*)malloc(sizeof(unsigned char)*9);
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
		free(result.image);
	}

	if (id != 0)
	{	
		for(i = 0; i < SPU_THREADS; i++)
			pthread_join(threads[i], NULL);
	}
	printf("Going to sleep before we die\n");
	sleep(10);
	return 0;
}

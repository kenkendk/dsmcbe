#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include <malloc_align.h>
#include <free_align.h>
#include "../Common/PPMReaderWriter.h"
#include "../Common/Common.h"
#include "../Common/StopWatch.h"

extern spe_program_handle_t SPU;

#define FALSE 0
#define TRUE 1
#define RANDOM(max) ((((float)rand() / (float)RAND_MAX) * (float)(max)))
#define MIN(a,b) ((a)<(b) ? (a) : (b))

#define SHOTS_SPU 2048
#define SHOTS (SHOTS_SPU * 7200)

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

void canon(int shots, int shots_spu, int canonX, int canonY, float canonAX, float canonAY, spe_context_ptr_t* spe_ids, unsigned char* energy, struct IMAGE_FORMAT_GREY img)
{
	int i;
	unsigned char max_integer = (unsigned char)pow(2, sizeof(unsigned char)*8);
	unsigned int spu_pointers[5] = { 0x0, 0x0, 0x0, 0x0, 0x0};
	float spu_data[2] = {0x0, 0x0};
	struct POINTS* points = (struct POINTS*)_malloc_align(sizeof(struct POINTS) * shots, 7);
	
	int sendmessages = 0;
	int shotsspu = SHOTS_SPU;

	for(i = 0; i < SPU_THREADS; i++)
	{
		//printf("Sending job (%d / %d) to (0x%dx)\n", sendmessages+1, shots/shots_spu, (int)spe_ids[i]);
		spu_pointers[0] = (unsigned int)img.image;
		spu_pointers[1] = (unsigned int)&points[sendmessages * shots_spu];
		spu_pointers[2] = shotsspu;
		spu_pointers[3] = canonX;
		spu_pointers[4] = canonY;
		spu_data[0] = canonAX;
		spu_data[1] = canonAY;
 		
		spe_in_mbox_write(spe_ids[i], spu_pointers, 5, SPE_MBOX_ALL_BLOCKING);
		spe_in_mbox_write(spe_ids[i], (unsigned int*)spu_data, 2, SPE_MBOX_ALL_BLOCKING);
		sendmessages++;
	}
	
	//Waiting to recieved message from SPE
	int total_reads = shots / shots_spu;
	int change = total_reads-((total_reads / SPU_THREADS) * SPU_THREADS);
	//int last = (shots - ((total_reads - change) / SPU_THREADS) * SPU_THREADS);
	
	if (change > 0)
		total_reads += SPU_THREADS; 
	
	while (total_reads > 0)
	{
		// Vi har nået sidste runde og nu skal der kun skydes 
		// "last" antal skud / spe istedet for SHOTS_SPU!			
		//if (total_reads == change)
		//	shotsspu = last;
			
		for(i=0;i<SPU_THREADS; i++)
		{
			if (spe_out_mbox_status(spe_ids[i]) != 0)
			{
				unsigned int data;
				spe_out_mbox_read(spe_ids[i], &data, 1);
				//printf("Read value %d from (0x%dx)\n", data, (int)spe_ids[i]);
				total_reads--;
									
				if(sendmessages >= (shots / shots_spu))
					break;
				
				// TODO: When a computational result is received from a SPE issue an "lwsync"
				// to garantee that data is written to main memory.
				
				//printf("Sending job (%d / %d) to (0x%dx)\n", sendmessages+1, shots/shots_spu, (int)spe_ids[i]);
				spu_pointers[0] = (unsigned int)&points[sendmessages * shots_spu];
				spu_pointers[1] = shotsspu;
		 		//spe_in_mbox_write(spe_ids[i], spu_pointers, 1, SPE_MBOX_ALL_BLOCKING);
		 		spe_in_mbox_write(spe_ids[i], spu_pointers, 2, SPE_MBOX_ALL_BLOCKING);
				sendmessages++;
			}
		}
	}
	
	// Save results to energy
	for(i = 0; i < SHOTS; i++)
	{
		if(points[i].alive == FALSE)
			energy[MAPOFFSET((int)(points[i].x), (int)(points[i].y))] =  MIN(energy[MAPOFFSET((int)(points[i].x),(int)(points[i].y))] + 1, max_integer);
	}
	
	_free_align(points);
}

void *ppu_pthread_function(void *arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0)
	{
		perror ("Failed running context");
		exit (1);
	}
	pthread_exit(NULL);
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
	
	spe_context_ptr_t spe_ids[SPU_THREADS];
	pthread_t threads[SPU_THREADS];
	struct IMAGE_FORMAT_GREY img;
	struct IMAGE_FORMAT result;
	unsigned char* energy;
	unsigned char* cmap;
	unsigned char* scale;
	int scale_size = 9;
	char timer_buffer[256];
	int y, x;
	int i = 0;
	unsigned int stop[1] = {0x0};		
	unsigned int cont[1] = {0x1};
		
	readimage_grey(input, malloc_align7, &img);

	WIDTH = img.width;
	HEIGTH = img.height;

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
		
	/*Create several SPE-threads to execute 'SPU'.*/
	for(i=0;i<SPU_THREADS;i++)
	{
		/* Create context */
		if ((spe_ids[i] = spe_context_create (0, NULL)) == NULL) 
		{
			perror ("Failed creating context");
			exit (1);
		}
		
		/* Load program into context */
		if (spe_program_load (spe_ids[i], &SPU)) 
		{
			perror ("Failed loading program");
			exit (1);
		}

		/* Create thread for each SPE context */
		if (pthread_create (&threads[i], NULL,	&ppu_pthread_function, &spe_ids[i])) 
		{
			perror ("Failed creating thread");
			exit (1);
		}

		unsigned int data[2] = {WIDTH, HEIGTH};
		//send_mailbox_message_to_spe(spe_ids[i], 2, data);
		spe_in_mbox_write(spe_ids[i], data, 2, SPE_MBOX_ALL_BLOCKING);
	}

	//Start timer!
	sw_init();
	sw_start();
	printf("Timer started\n");

	printf("Start firering canon #1\n");
	canon(SHOTS, SHOTS_SPU, 85, 75, 1.0, 0.8, spe_ids, energy, img);
	printf("Stopped firering canon #1\n");

	for(i = 0; i < SPU_THREADS; i++)
	{
 		spe_in_mbox_write(spe_ids[i], stop, 1, SPE_MBOX_ALL_BLOCKING);
 		spe_in_mbox_write(spe_ids[i], cont, 1, SPE_MBOX_ALL_BLOCKING);
	}

	printf("Start firering canon #2\n");
	canon(SHOTS, SHOTS_SPU, 10, 230, 1.0, 0.0, spe_ids, energy, img);
	printf("Stopped firering canon #2\n");

	for(i = 0; i < SPU_THREADS; i++)
	{
 		spe_in_mbox_write(spe_ids[i], stop, 1, SPE_MBOX_ALL_BLOCKING);
 		spe_in_mbox_write(spe_ids[i], cont, 1, SPE_MBOX_ALL_BLOCKING);
	}

	printf("Start firering canon #3\n");
	canon(SHOTS, SHOTS_SPU, 550, 230, -1.0, 0.0, spe_ids, energy, img);
	printf("Stopped firering canon #3\n");

	for(i = 0; i < SPU_THREADS; i++)
	{
 		spe_in_mbox_write(spe_ids[i], stop, 1, SPE_MBOX_ALL_BLOCKING);
 		spe_in_mbox_write(spe_ids[i], cont, 1, SPE_MBOX_ALL_BLOCKING);
	}

	printf("Start firering canon #4\n");
	canon(SHOTS, SHOTS_SPU, 475, 90, -1.0, 0.75, spe_ids, energy, img);
	printf("Stopped firering canon #4\n");

	for(i = 0; i < SPU_THREADS; i++)
	{
 		spe_in_mbox_write(spe_ids[i], stop, 1, SPE_MBOX_ALL_BLOCKING);
 		spe_in_mbox_write(spe_ids[i], cont, 1, SPE_MBOX_ALL_BLOCKING);
	}

	printf("Start firering canon #5\n");
	canon(SHOTS, SHOTS_SPU, 280, 0, 0.0, 1.0, spe_ids, energy, img);
	printf("Stopped firering canon #5\n");

	// Stop timer!
	sw_stop();
	sw_timeString(timer_buffer);
	printf("Time used: %s\n", timer_buffer);

	for(i = 0; i < SPU_THREADS; i++)
	{
 		spe_in_mbox_write(spe_ids[i], stop, 1, SPE_MBOX_ALL_BLOCKING);
 		spe_in_mbox_write(spe_ids[i], stop, 1, SPE_MBOX_ALL_BLOCKING);
	}
	
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

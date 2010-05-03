#include <dsmcbe_ppu.h>
#include <stdio.h>
#include "../guids.h"
#include <debug.h>
#include <unistd.h>
#include <libspe2.h>
#include <stdlib.h>
#include "guids.h"
#include "StopWatch.h"
#include <math.h>
#include <graphicsScreen.h>

//#define GRAPHICS

#ifndef MAX
#define MAX(x,y) ((x > y) ? (x) : (y))
#endif

#ifndef RND
#define RND (rand() / (double)RAND_MAX)
#endif

#ifndef PI
#define PI 3.14157
#endif

#ifdef GRAPHICS
#define DISPLAY_WIDTH 300
#define DISPLAY_HEIGHT 300

#define MIN_COLOR_R 0
#define MIN_COLOR_G 255
#define MIN_COLOR_B 0

#define MAX_COLOR_R 255
#define MAX_COLOR_G 0
#define MAX_COLOR_B 0

#define COLOR_RATIO_R (MAX_COLOR_R - MIN_COLOR_R)
#define COLOR_RATIO_G (MAX_COLOR_G - MIN_COLOR_G)
#define COLOR_RATIO_B (MAX_COLOR_B - MIN_COLOR_B)

#endif

#define SIGN(x) (((x) >= 0) ? (1) : (-1))

void* mallocCreate(unsigned long size);
void InitializeBigBangParticles(unsigned int size, struct Particle* particles);

int main(int argc, char **argv)
{
	printf("Compile time - %s\n", __TIME__);

	srand(time(NULL));

	unsigned long size;
    unsigned int spu_threads;
    unsigned int machineid;
    char* file;
    unsigned int i;
    char buf[256];
    unsigned int hw_threads;
    unsigned int processes;
    struct SimulationArgs* args;

    machineid = 0;
    spu_threads = 6;
    file = NULL;
    hw_threads = 1;

	if(argc == 5) {
		machineid = atoi(argv[1]);
		file = argv[2];
		spu_threads = atoi(argv[3]);
		spu_threads = atoi(argv[4]);
	} else if(argc == 4) {
		machineid = atoi(argv[1]);
		file = argv[2];
		spu_threads = atoi(argv[3]);
	} else if (argc == 3) {
		machineid = 0;
		file = NULL;
		spu_threads = atoi(argv[1]);
		hw_threads = atoi(argv[2]);
	} else if (argc == 2) {
		machineid = 0;
		file = NULL;
		spu_threads = atoi(argv[1]);
	} else {
		printf("Wrong number of arguments \"./PPU spu-threads\"\n");
		printf("                       or \"./PPU spu-threads ppu-hw-threads\"\n");
		printf("                       or \"./PPU id network-file spu-threads\"\n");
		printf("                       or \"./PPU id network-file spu-threads ppu-hw-threads\"\n");
		return -1;
	}

	if (spu_threads <= 1)
	{
		perror("There must be at least two SPU process\n");
		exit(1);
	}

    pthread_t* threads = simpleInitialize(machineid, file, spu_threads);

    if (machineid == 0)
    {
    	//printf("PPU is setting up data\n");
    	processes = spu_threads * MAX(DSMCBE_MachineCount(), 1);

    	for(i = 0; i < processes; i++)
    	{
    		args = mallocCreate(sizeof(struct SimulationArgs));
    		args->ProcessId = i;
    		args->Gravity = SIMULATION_GRAVITY;
    		args->ProcessCount = processes;
    		args->MaxMass = SIMULATION_MAX_MASS;
    		args->MaxVelocity = SIMULATION_MAX_VELOCITY;
    		args->MinX = SIMULATION_MIN_X;
    		args->MaxX = SIMULATION_MAX_X;
    		args->MinY = SIMULATION_MIN_Y;
    		args->MaxY = SIMULATION_MAX_Y;
    		args->RoundCount = REPETITIONS;
    		args->UpdateFrequency = SIMULATION_UPDATE_FREQUENCY;

    		put(SIMULATION_SETUP, args);
    	}

    	//printf("PPU has sent setup packages\n");

		sw_init();
		sw_start();

		int startIndex = 0;
		int blockSize = (int)(SIMULATION_PARTICLE_COUNT / (double)processes);
		int endIndex = blockSize;

		for(i = 0; i < processes; i++)
		{
			if (i == (processes - 1))
				endIndex = SIMULATION_PARTICLE_COUNT;
			else
				endIndex = startIndex + blockSize;

			unsigned int elementCount = endIndex - startIndex;

			void* tmp = mallocCreate((sizeof(unsigned int) * 2) + (sizeof(struct Particle) * elementCount));
			((unsigned int*)tmp)[0] = elementCount;
			((unsigned int*)tmp)[1] = i;

			InitializeBigBangParticles(elementCount, tmp + (sizeof(unsigned int) * 2));

    		//printf("PPU is sending package %d with %d particles\n", i, elementCount);
			put(CHANNEL_START_GUID, tmp);

			startIndex = endIndex;
		}

		//printf("PPU has sent %d data packages\n", processes);

#ifdef GRAPHICS
		gs_init(DISPLAY_WIDTH, DISPLAY_HEIGHT);
		gs_clear(WHITE);
		gs_update();
		struct Particle* localParticleMap = malloc(sizeof(struct Particle) * SIMULATION_PARTICLE_COUNT);
#endif

		int updates = (REPETITIONS / SIMULATION_UPDATE_FREQUENCY);
		if (REPETITIONS % SIMULATION_UPDATE_FREQUENCY != 0)
			updates++;

		while(updates > 0)
		{
#ifdef GRAPHICS
			//Clear to prevent stale data showing as fresh
			memset(localParticleMap, 0, sizeof(struct Particle) * SIMULATION_PARTICLE_COUNT);
			unsigned int particleOffset = 0;
#endif

			for(i = 0; i < processes; i++)
			{
				//printf("Waiting for particle updates\n");
				void* resData = get(FEEDBACK_CHANNEL, &size);
				//printf("Got result for package %d with %d particles\n", ((unsigned int*)resData)[0], ((unsigned int*)resData)[1]);

#ifdef GRAPHICS
				memcpy(((void*)localParticleMap) + particleOffset, resData + (sizeof(unsigned int) * 2), ((unsigned int*)resData)[1] * sizeof(struct Particle));
				particleOffset += (((unsigned int*)resData)[1] * sizeof(struct Particle));
#endif

				release(resData);
			}
			printf("Updating screen, remaining count: %d\n", updates);

#ifdef GRAPHICS
			gs_clear(WHITE);
			for(i = 0; i < SIMULATION_PARTICLE_COUNT; i++)
			{
				struct Particle* p = &(localParticleMap[i]);

                if (!(p->expired))
                {
                    int x = (int)(((p->xPos - SIMULATION_MIN_X) / (SIMULATION_MAX_X - SIMULATION_MIN_X)) * DISPLAY_WIDTH);
                    int y = (int)(((p->yPos - SIMULATION_MIN_Y) / (SIMULATION_MAX_Y - SIMULATION_MIN_Y)) * DISPLAY_HEIGHT);

                    //ARGB is actually BGRA
                    unsigned char R = (unsigned char)(((double)COLOR_RATIO_R * (p->mass / SIMULATION_MAX_MASS)) + MIN_COLOR_R);
                    unsigned char G = (unsigned char)(((double)COLOR_RATIO_G * (p->mass / SIMULATION_MAX_MASS)) + MIN_COLOR_G);
                    unsigned char B = (unsigned char)(((double)COLOR_RATIO_B * (p->mass / SIMULATION_MAX_MASS)) + MIN_COLOR_B);

                    //printf("Updating pixel %d (%d, %d) with value: %d (%d,%d,%d)\n", i, x, y, (R << 16 | G << 8 | B), R, G, B);
                    //gs_plot(x, y, (R << 16 | G << 8 | B));
                    gs_plot(x, y, 0);
                }
                /*else if (i % 100 == 0)
                	printf("Found expired particle: %f,%f, mass: %f, expired: %d\n", p->xPos, p->yPos, p->mass, p->expired);*/
			}
			gs_update();
			//printf("Updated screen\n");
#endif
			updates--;
		}

#ifdef GRAPHICS
		free(localParticleMap);
		gs_exit();
#endif

		sw_stop();
		sw_timeString(buf);

		printf("Simulation parameters:\nParticles: \t%d\nRounds: \t%d\nUpdate freq.: \t%d\n\n", SIMULATION_PARTICLE_COUNT, REPETITIONS, SIMULATION_UPDATE_FREQUENCY);
		printf("Elapsed time with %d SPU's: %s\n", processes, buf);

		release(create(MASTER_COMPLETION_LOCK, 1, CREATE_MODE_NONBLOCKING));
    }
    else
    {
    	release(acquire(MASTER_COMPLETION_LOCK, &size, ACQUIRE_MODE_READ));
    }

	printf("PPU is waiting for threads to complete\n");

	for(i = 0; i < spu_threads; i++)
	{
		//printf(WHERESTR "waiting for SPU %i\n", WHEREARG, i);
		pthread_join(threads[i], NULL);
	}

	printf("PPU is terminating\n");

	terminate();
	return 0;
}

void InitializeBigBangParticles(unsigned int size, struct Particle* particles)
{
	unsigned int i;
	struct Particle* p;

	VALUE_TYPE centerX;
	VALUE_TYPE centerY;

	centerX = ((SIMULATION_MAX_X - SIMULATION_MIN_X) / 2) + SIMULATION_MIN_X;
	centerY = ((SIMULATION_MAX_Y - SIMULATION_MIN_Y) / 2) + SIMULATION_MIN_Y;

	VALUE_TYPE angle;

	for(i = 0; i < size; i++)
	{
		p = &particles[i];

		angle = RND * PI * 2;
		p->expired = FALSE;
		p->mass = SIMULATION_MAX_MASS * RND;
		p->xPos = centerX + (RND * SIGN(0.5 - RND));
		p->yPos = centerY + (RND * SIGN(0.5 - RND));
		p->xVel = sin(angle) * SIMULATION_MAX_VELOCITY;
		p->yVel = cos(angle) * SIMULATION_MAX_VELOCITY;

		/*if ((i % 100) == 0)
			printf("Generated particle (%f,%f), mass: %f, expired: %d\n", p->xPos, p->yPos, p->mass, p->expired);*/
	}
}

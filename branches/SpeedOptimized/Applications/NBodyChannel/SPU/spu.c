#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include "../PPU/guids.h"
#include <math.h>

#define SIGN(x) (((x) >= 0) ? (1) : (-1))

void ApplyForce(struct Particle* ownerList, struct Particle* passingList, size_t ownerSize, size_t passingSize, unsigned int samelist, struct SimulationArgs* args)
{
	size_t i, j;
	struct Particle* a, *b;
	VALUE_TYPE distanceX, distanceY, r;

	/*if (args->ProcessId == 0 && samelist)
		printf("Particle before ApplyForces: (%f, %f), mass: %f, expired: %d\n", ownerList->xPos, ownerList->yPos, ownerList->mass, ownerList->expired);*/

	for(i = 0; i < ownerSize; i++)
		for(j = 0; j < passingSize; j++)
			if (!(samelist || i == j))
			{
				a = &ownerList[i];
				b = &passingList[j];

				if (!(a->expired && b->expired))
				{
					distanceX = abs(a->xPos - b->xPos);
					distanceY = abs(a->yPos - b->yPos);
					r = sqrt((distanceX * distanceX) + (distanceY * distanceY));

					//They have merged into one element
					/*if (r == 0)
					{
						a->mass += b->mass;
						a->xVel += b->xVel;
						a->yVel += b->yVel;

						b->expired = TRUE;
					}
					else*/
					{
						//The forces influence each other
						a->xVel += ((args->Gravity * a->mass * b->mass) / (r * r)) * ((b->xPos - a->xPos) / r) / a->mass;
						a->yVel += ((args->Gravity * a->mass * b->mass) / (r * r)) * ((b->yPos - a->yPos) / r) / a->mass;
					}
				}
			}

	/*if (args->ProcessId == 0 && samelist)
		printf("Particle after ApplyForces: (%f, %f), mass: %f, expired: %d\n", ownerList->xPos, ownerList->yPos, ownerList->mass, ownerList->expired);*/
}

void Move(struct SimulationArgs* args, struct Particle* particles, size_t size)
{
	size_t i;
	struct Particle* p;

	for (i = 0; i < size; i++)
	{
		p = &particles[i];
		if (!p->expired)
		{

			if (abs(p->xVel) > args->MaxVelocity)
				p->xVel = args->MaxVelocity * SIGN(p->xVel);
			if (abs(p->yVel) > args->MaxVelocity)
				p->yVel = args->MaxVelocity * SIGN(p->yVel);

			p->xPos += p->xVel;
			p->yPos += p->yVel;

			//If the particle is outside the simulation area, just quit
			if (p->xPos > args->MaxX || p->xPos < args->MinX || p->yPos > args->MaxY || p->yPos < args->MinY)
				p->expired = TRUE;
		}
	}
}

//TODO: Remove this function once DSMCBE supports forwarding a message directly
void ForwardData(void* data, unsigned long size, GUID channelId)
{
	void* tmp;

	tmp = createMalloc(size);
	memcpy(tmp, data, size);
	release(data);
	put(channelId, tmp);
}

int main(int argc, char** argv) {
	
	initialize();

	unsigned long size;
	struct SimulationArgs* args;

	void* tempData;

	unsigned int initialSize;
	struct Particle* initialParticles;

	unsigned int tempSize;
	struct Particle* tempParticles;

	unsigned int isInitialDataInTemp;

	unsigned int roundCounter;
	unsigned int isInitialRound;

	unsigned int initialPackageId;
	unsigned int tempPackageId;

	unsigned int i;
	void* tmp;

	void* putBuffer = NULL;
	unsigned int putBufferSize;

	unsigned int forwardCount;

	args = get(SIMULATION_SETUP, &size);

	GUID readerChannel = CHANNEL_START_GUID + (args->ProcessId % args->ProcessCount);
	GUID writerChannel = CHANNEL_START_GUID + ((args->ProcessId + 1) % args->ProcessCount);

	roundCounter = 0;
	initialSize = 0;
	initialParticles = NULL;
	isInitialRound = TRUE;
	putBuffer = NULL;
	putBufferSize = 0;

	forwardCount = (args->ProcessCount - 1) - args->ProcessId;

	//printf("SPU %d is ready to forward, %d packages, readerChannel: %d, writerChannel: %d\n", args->ProcessId, forwardCount, readerChannel, writerChannel);

	//Processes forward the required data into the other SPU's
	for(i = 0; i < forwardCount; i++)
		ForwardData(get(readerChannel, &size), size, writerChannel);

	//printf("SPU %d has forwarded all copies\n", args->ProcessId);

	tempData = get(readerChannel, &size);
	initialSize = ((unsigned int*)tempData)[0];
	initialPackageId = ((unsigned int*)tempData)[1];

	//We must release the channel before requesting it again
	initialParticles = MALLOC(sizeof(struct Particle) * initialSize);
	memcpy(initialParticles, tempData + (sizeof(unsigned int) * 2), sizeof(struct Particle) * initialSize);
	release(tempData);

	//printf("SPU %d has received initial data, packageId: %d\n", args->ProcessId, initialPackageId);

	//printf("SPU %d is building initial response\n", args->ProcessId);
	tempData = createMalloc((sizeof(unsigned int) * 2) + (sizeof(struct Particle) * initialSize));
	((unsigned int*)tempData)[0] = initialSize;
	((unsigned int*)tempData)[1] = initialPackageId;
	tempParticles = (struct Particle*)(tempData + (sizeof(unsigned int) * 2));
	memcpy(tempParticles, initialParticles, sizeof(struct Particle) * initialSize);

	//printf("SPU %d is in ApplyForces, size: %d\n", args->ProcessId, initialSize);
	ApplyForce(initialParticles, tempParticles, initialSize, initialSize, TRUE, args);

	//printf("SPU %d is writing initial package %d to channel %d\n", args->ProcessId, initialPackageId, writerChannel);
	if (args->ProcessId == 0)
	{
		putBuffer = tempData;
		putBufferSize = size;
	}
	else
		put(writerChannel, tempData);

	isInitialDataInTemp = FALSE;

	while(1)
	{
		//printf("SPU %d is awating input from channel %d\n", args->ProcessId, readerChannel);

		tempData = get(readerChannel, &size);
		tempSize = ((unsigned int*)tempData)[0];
		tempPackageId = ((unsigned int*)tempData)[1];
		tempParticles = (struct Particle*)(tempData + (sizeof(unsigned int) * 2));

		//printf("SPU %d has recieved package %d, from channel %d\n", args->ProcessId, tempPackageId, readerChannel);

		isInitialDataInTemp = tempPackageId == initialPackageId;

		ApplyForce(initialParticles, tempParticles, initialSize, tempSize, isInitialDataInTemp, args);
		if (isInitialDataInTemp)
		{
			//printf("SPU %d has is moving data, initialSize: %d, tempSize: %d, initialId: %d, tempId: %d\n", args->ProcessId, initialSize, tempSize, initialPackageId, tempPackageId);

			Move(args, initialParticles, initialSize);
			if (initialSize != tempSize)
				REPORT_ERROR2("SPU %d has a bad set!", args->ProcessId);

			memcpy(tempParticles, initialParticles, sizeof(struct Particle) * initialSize);
		}

		//printf("SPU %d is forwarding package %d to channel %d\n", args->ProcessId, tempPackageId, writerChannel);

		if (args->ProcessId != 0)
			ForwardData(tempData, size, writerChannel);
		else
		{
			put(writerChannel, putBuffer);

			putBufferSize = size;
			putBuffer = createMalloc(size);
			memcpy(putBuffer, tempData, size);
			release(tempData);
		}

		if (isInitialDataInTemp)
		{
			roundCounter++;

			/*if (args->ProcessId == 0)
				printf("Round %d\n", roundCounter);*/

			if (roundCounter == args->RoundCount || (args->UpdateFrequency != 0 && (roundCounter % args->UpdateFrequency) == 0))
			{
				//printf("SPU %d is updating global copy, roundCounter: %d\n", args->ProcessId, roundCounter);

				/*if (args->ProcessId == 0)
					printf("Particle sent to update: (%f, %f), mass: %f, expired: %d\n", initialParticles->xPos, initialParticles->yPos, initialParticles->mass, initialParticles->expired);*/

				tmp = createMalloc((2 * sizeof(unsigned int)) + sizeof(struct Particle) * initialSize);
				((unsigned int*)tmp)[0] = initialPackageId;
				((unsigned int*)tmp)[1] = initialSize;
				memcpy(tmp + (2 * sizeof(unsigned int)), initialParticles, sizeof(struct Particle) * initialSize);
				put(FEEDBACK_CHANNEL, tmp);
			}

			if (roundCounter == args->RoundCount)
			{
				//Exit the loop
				break;
			}
		}

		isInitialRound = FALSE;
	}

	if (args->ProcessId != 1)
	{
		//printf("SPU %d is reading final package\n", args->ProcessId);
		release(get(readerChannel, &size));
		//FREE(putBuffer);
	}

	//printf("SPU %d is terminating, roundCounter: %d\n", args->ProcessId, roundCounter);
	
	FREE(initialParticles);
	release(args);
	terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


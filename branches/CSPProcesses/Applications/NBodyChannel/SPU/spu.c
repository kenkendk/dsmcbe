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

				if (!(a->expired) && !(b->expired))
				{
					distanceX = abs(a->xPos - b->xPos);
					distanceY = abs(a->yPos - b->yPos);
					r = sqrt((distanceX * distanceX) + (distanceY * distanceY));

					//They have merged into one element
					if (r == 0)
					{
						a->mass += b->mass;
						a->xVel += b->xVel;
						a->yVel += b->yVel;

						b->expired = TRUE;
					}
					else
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

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	unsigned long size;
	struct SimulationArgs* args;

	void* tempData;

	void* initialPackage;
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

	unsigned int forwardCount;

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	CSP_SAFE_CALL("read setup", dsmcbe_csp_channel_read(SIMULATION_SETUP, NULL, (void**)&args));

	GUID readerChannel = CHANNEL_START_GUID + (args->ProcessId % args->ProcessCount);
	GUID writerChannel = CHANNEL_START_GUID + ((args->ProcessId + 1) % args->ProcessCount);

	CSP_SAFE_CALL("create reader channel", dsmcbe_csp_channel_create(readerChannel, 0, CSP_CHANNEL_TYPE_ONE2ONE));

	roundCounter = 0;
	initialSize = 0;
	initialParticles = NULL;
	isInitialRound = TRUE;
	putBuffer = NULL;

	forwardCount = (args->ProcessCount - 1) - args->ProcessId;

	//printf("SPU %d is ready to forward, %d packages, readerChannel: %d, writerChannel: %d\n", args->ProcessId, forwardCount, readerChannel, writerChannel);

	//Processes forward the required data into the other SPU's
	for(i = 0; i < forwardCount; i++)
	{
		CSP_SAFE_CALL("read-forward initial data", dsmcbe_csp_channel_read(args->ProcessId == 0 ? STARTUP_CHANNEL : readerChannel, NULL, &tempData));
		CSP_SAFE_CALL("write-forward initial data", dsmcbe_csp_channel_write(writerChannel, tempData));
	}

	//printf("SPU %d has forwarded all copies\n", args->ProcessId);

	//Extract the initial package
	CSP_SAFE_CALL("read initial package", dsmcbe_csp_channel_read(args->ProcessId == 0 ? STARTUP_CHANNEL : readerChannel, &size, &initialPackage));
	initialSize = ((unsigned int*)initialPackage)[0];
	initialPackageId = ((unsigned int*)initialPackage)[1];
	initialParticles = initialPackage + (sizeof(unsigned int) * 2);

	//printf("SPU %d has received initial data, packageId: %d\n", args->ProcessId, initialPackageId);

	//printf("SPU %d is building initial response\n", args->ProcessId);

	CSP_SAFE_CALL("create working copy", dsmcbe_csp_item_create(&tempData, size));

	memcpy(tempData, initialPackage, size);
	tempParticles = (struct Particle*)(tempData + (sizeof(unsigned int) * 2));

	//printf("SPU %d is in ApplyForces, size: %d\n", args->ProcessId, initialSize);
	ApplyForce(initialParticles, tempParticles, initialSize, initialSize, TRUE, args);

	if (args->ProcessId == 0)
	{
		putBuffer = tempData;
	}
	else
	{
		//printf("SPU %d is writing initial package %d to channel %d\n", args->ProcessId, initialPackageId, writerChannel);
		CSP_SAFE_CALL("forwardning initial data", dsmcbe_csp_channel_write(writerChannel, tempData));
	}

	isInitialDataInTemp = FALSE;

	while(1)
	{
		//printf("SPU %d is awating input from channel %d\n", args->ProcessId, readerChannel);

		CSP_SAFE_CALL("read package", dsmcbe_csp_channel_read(readerChannel, &size, &tempData));
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

		//printf("SPU %d is forwarding package %d to channel %d, roundCounter %d\n", args->ProcessId, tempPackageId, writerChannel, roundCounter);

		if (args->ProcessId != 0)
		{
			CSP_SAFE_CALL("forward package", dsmcbe_csp_channel_write(writerChannel, tempData));
		}
		else
		{
			CSP_SAFE_CALL("forward package", dsmcbe_csp_channel_write(writerChannel, putBuffer));

			putBuffer = tempData;
		}

		//printf("SPU %d has forwarded package %d to channel %d, roundCounter %d\n", args->ProcessId, tempPackageId, writerChannel, roundCounter);

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

				CSP_SAFE_CALL("create feedback", dsmcbe_csp_item_create(&tmp, (2 * sizeof(unsigned int)) + sizeof(struct Particle) * initialSize));
				((unsigned int*)tmp)[0] = initialPackageId;
				((unsigned int*)tmp)[1] = initialSize;
				memcpy(tmp + (2 * sizeof(unsigned int)), initialParticles, sizeof(struct Particle) * initialSize);
				CSP_SAFE_CALL("send feedback", dsmcbe_csp_channel_write(FEEDBACK_CHANNEL, tmp));
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

		CSP_SAFE_CALL("read-discard final package", dsmcbe_csp_channel_read(readerChannel, NULL, &tempData));
		CSP_SAFE_CALL("write-discard final package", dsmcbe_csp_item_free(tempData));

		if (args->ProcessId == 0)
		{
			CSP_SAFE_CALL("free putBuffer", dsmcbe_csp_item_free(putBuffer));
		}
	}

	//printf("SPU %d is terminating, roundCounter: %d\n", args->ProcessId, roundCounter);
	
	CSP_SAFE_CALL("free initial particles", dsmcbe_csp_item_free(initialPackage));
	CSP_SAFE_CALL("free args", dsmcbe_csp_item_free(args));
	
	return 0;
}


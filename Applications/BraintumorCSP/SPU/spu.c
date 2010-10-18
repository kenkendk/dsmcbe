#include <stdlib.h>
#include <spu_mfcio.h>
#include <math.h>
#include <libmisc.h>
#include <dsmcbe_spu.h>
#include <dsmcbe_csp.h>
#include "../PPU/header_files/guids.h"
#include "../PPU/header_files/Shared.h"
#include <debug.h>
#include <unistd.h>
#include <limits.h>
#include <dsmcbe_spu_internal.h>

//Gets the index of the fragment that contains the given pixel
#define FRAGMENT_INDEX(work, x, y) (((x) / (work)->fragment_width) + (((y) / (work)->fragment_height) * (work)->fragments_x))

//Approximation of normal variant function
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

void runCanon(struct WORK_ORDER* work, struct ENERGY_POINT* workset, char* aliveMap, char* fragment, int fragmentIndex, char* neededRequests, GUID* imageMapRequest, unsigned int* channelRequests)
{
	//Extract the x and y fragment index
	int fragmentx = fragmentIndex % work->fragments_x;
	int fragmenty = fragmentIndex / work->fragments_x;

	//Calculate the fragment bounds
	int minx = fragmentx * work->fragment_width;
	int miny = fragmenty * work->fragment_height;

	int maxx;
	int maxy;
	
	if (fragmentx == (int)work->fragments_x - 1)
		maxx = work->map_width;
	else
		maxx = minx + work->fragment_width;
	
	if (fragmenty == (int)work->fragments_y - 1)
		maxy = work->map_heigth;
	else
		maxy = miny + work->fragment_height;
	
	//Cache these properties
	float ax = work->canonAX;
	float ay = work->canonAY;
	int mapwidth = work->map_width;
	int mapheight = work->map_heigth;
	int fragmentwidth = maxx - minx;
	
	size_t i;

	//Now iterate over each particle
	for(i = 0; i < work->shots; i++)
	{
		//Skip dead items
		if (aliveMap[i] == TRUE)
		{
			//We calculate the wandering with float precision
			float x = workset[i].x;
			float y = workset[i].y;
			
			//We calculate the bounds with integers
			int xx = workset[i].x;
			int yy = workset[i].y;

			//While we are inside the map bounds
			while(xx >= minx && yy >= miny && xx < maxx && yy < maxy)
			{
				//If the energy on the current pixel is larger than the threshold we deposit the energy here
				if ((RANDOM(10) * (256-fragment[((yy-miny)*fragmentwidth)+(xx-minx)])) > (float)1700.0)
				{
					//Mark as done
					aliveMap[i] = FALSE;
					workset[i].x = xx;
					workset[i].y = yy;
					break;
				}

				//Move the particle with normal variation
				x += ax + random_normal_variant(0.0, 0.15);
				y += ay + random_normal_variant(0.0, 0.15);

				//Update the integer position for bound calculation
				xx = (int)x;
				yy = (int)y;		
			}

			if (aliveMap[i] == FALSE)
				continue;

			//If the particle is outside the map, mark it as done
			if(xx < 0 || yy < 0 || xx >= (int)mapwidth || yy >= (int)mapheight)
			{
				//Mark as done
				aliveMap[i] = FALSE;
				workset[i].x = UINT_MAX;
				workset[i].y = UINT_MAX;
				continue;
			}

			//The particle is still alive, but outside this image fragment
			// so we record a request for the required fragment
			int new_fragmentIndex = FRAGMENT_INDEX(work, xx, yy);
			if (neededRequests[new_fragmentIndex] == 0)
			{
				//printf("Pixel at (%d,%d) is outside current fragment so image %d is requested\n", xx, yy, FRAGMENT_INDEX(work, xx, yy));

				neededRequests[new_fragmentIndex] = 1;
				imageMapRequest[*channelRequests] = IMAGEBUFFER + new_fragmentIndex;
				(*channelRequests)++;
			}

			//Record the particle's current position
			workset[i].x = xx;
			workset[i].y = yy;
		}
	}

}

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	srand(1);
	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	struct WORK_ORDER* work;
	GUID* imageMapRequests;
	char* neededChannels;
	size_t i;

	while(dsmcbe_csp_channel_read(WORK_CHANNEL, NULL, (void**)&work) == CSP_CALL_SUCCESS)
	{
		//printf("SPE %llu:%u got work item %d\n", speid, threadId, work->id);

		int fragments_w = work->fragments_x;
		int fragments_h = work->fragments_y;

		//We keep an array of channelIds that we need to fetch
		if (neededChannels == NULL)
		{
			neededChannels = (char*)MALLOC(fragments_w * fragments_h);
			imageMapRequests = (GUID*)MALLOC(sizeof(GUID) * fragments_w * fragments_h);
		}

		//At startup, we need no images
		memset(neededChannels, 0, fragments_w * fragments_h);

		//This counter keeps track of the the GUID requests we have so far
		unsigned int channelRequests = 0;

		//Allocate a result buffer
		struct ENERGY_POINT* workset;
		CSP_SAFE_CALL("allocate result", dsmcbe_csp_item_create((void**)&workset, sizeof(struct ENERGY_POINT) * work->shots));

		//Allocate a buffer for keeping track of dead particles
		char* aliveMap = (char*)MALLOC(work->shots);
		memset(aliveMap, TRUE, work->shots);

		//Help the compiler and cache the indirection
		int x = work->canonX;
		int y = work->canonY;

		//Initialize all particles to start at the canon
		for(i = 0; i < work->shots; i++)
		{
			workset[i].x = x;
			workset[i].y = y;
		}

		//Before we can start, we need the first image chunk
		imageMapRequests[0] = IMAGEBUFFER + FRAGMENT_INDEX(work, x, y);
		channelRequests++;

		//printf("The canon is located at (%d, %d) so image %d is requested\n", x, y, FRAGMENT_INDEX(work, x, y));

		//If more fragments are needed, some particles are still alive
		while(channelRequests > 0)
		{
			GUID selectedChannel;
			char* fragment;

			//printf("SPE %llu:%u, %d is requesting a fragment, list size is %d\n", speid, threadId, work->id, channelRequests);

			/*size_t k;
			printf("Channels in request list: ");
			for(k = 0; k < channelRequests; k++)
				printf("%d, ", imageMapRequests[k]);
			printf("\nChannels according to needed lookup: ");
			for(k = 0; k < (unsigned int)(fragments_w * fragments_h); k++)
				if (neededChannels[k] != 0)
					printf("%d, ", (int)k);
			printf("\n");*/

			size_t fragmentSize;
			CSP_SAFE_CALL("get fragment", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, imageMapRequests, channelRequests, &selectedChannel, &fragmentSize, (void**)&fragment));

			/*printf("SPE %llu:%u, %d got fragment %d, first 4 bytes: %d, %d, %d, %d, last 4 bytes: %d, %d, %d, %d\n",
					speid, threadId, work->id, selectedChannel - IMAGEBUFFER,
					(unsigned int)fragment[0], (unsigned int)fragment[1], (unsigned int)fragment[2], (unsigned int)fragment[3],
					(unsigned int)fragment[fragmentSize - 4], (unsigned int)fragment[fragmentSize - 3], (unsigned int)fragment[fragmentSize - 2], (unsigned int)fragment[fragmentSize - 1]
			);*/

			//Now remove the channel from the list of needed fragments
			for(i = 0; i < channelRequests; i++)
				if (imageMapRequests[i] == selectedChannel)
				{
					//If the channelId is not the last, move the last into this spot
					if (i != channelRequests - 1)
						imageMapRequests[i] = imageMapRequests[channelRequests - 1];

					//Remove the id from the list
					channelRequests--;
					break;
				}
			
			//Reset the lookupIndex
			neededChannels[selectedChannel - IMAGEBUFFER] = 0;

			//printf("SPE %llu:%u, %d is running canon on fragment %d, with %d particles\n", speid, threadId, work->id, selectedChannel - IMAGEBUFFER, work->shots);

			runCanon(work, workset, aliveMap, fragment, selectedChannel - IMAGEBUFFER, neededChannels, imageMapRequests, &channelRequests);

			//printf("SPE %llu:%u, %d writing back fragment %d to channel %d\n", speid, threadId, work->id, selectedChannel - IMAGEBUFFER, selectedChannel);

			CSP_SAFE_CALL("write fragment back", dsmcbe_csp_channel_write(selectedChannel, fragment));
			fragment = NULL;
		}
		
#ifdef DEBUG
		for(int i = 0; i < work->shots; i++)
			if (aliveMap[i] == TRUE)
				printf(WHERESTR "Found live particle %d\n", WHEREARG, i);
#endif
		//printf("SPE %llu:%u, %d is writing results for %d particles\n", speid, threadId, work->id, work->shots);

		//Clean up
		FREE(aliveMap);
		CSP_SAFE_CALL("free work", dsmcbe_csp_item_free(work));
		aliveMap = NULL;
		work = NULL;

		//We are now done with this work item, so send the results to the result channel
		CSP_SAFE_CALL("write results back", dsmcbe_csp_channel_write(ENERGY_RESULT_CHANNEL, workset));
		workset = NULL;
	}

	//printf("SPE %llu:%u is terminating\n", speid, threadId);

	//These are allocated once for the process, so we free them on process exit
	if (neededChannels != NULL)
	{
		FREE(neededChannels);
		FREE(imageMapRequests);
	}

	return 0;
}

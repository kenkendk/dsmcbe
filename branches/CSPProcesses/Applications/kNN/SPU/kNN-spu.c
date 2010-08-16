//#define DEBUG
#include "../PPU/kNN-Shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <debug.h>
#include <dsmcbe_spu.h>
#include <dsmcbe_csp.h>

inline PROBLEM_TYPE calc_distance(size_t dimensions, PROBLEM_TYPE* a, PROBLEM_TYPE* b)
{
	size_t i;
	PROBLEM_TYPE dist = 0;
	for(i = 0; i < dimensions; i++)
		dist += (a[i] - b[i]) * (a[i] - b[i]);

	return sqrt(dist);
}

void print_results(size_t count, struct result_entry* list)
{
	size_t i;
	printf("\nPrinting %u results\n", (unsigned int)count);
	for(i = 0; i < count; i++)
		printf("Element %u has index %u and distance %f\n", (unsigned int)i, list[i].elementId, list[i].distance);

	printf("\n\n");
}

void insert_element(size_t dimensions, size_t count, struct result_entry* list, unsigned int newId, PROBLEM_TYPE newDistance)
{
	int i, j;

	//print_results(dimensions, count, list);

	//Find out where to put the element
	if (count == 0)
	{
		i = 0;
	}
	else
	{
		for(i = 0; i < (int)count; i++)
			if (newDistance < list[i * (dimensions + 1)].distance)
				break;

		//The new element is closer than element i
		//Move elements one down the list
		for(j = count - 1; j >= i; j--)
			memcpy(&list[(j + 1)], &list[j], sizeof(struct result_entry));
	}

	//Insert the item at the newly free'd spot
	list[i].elementId = newId;
	list[i].distance = newDistance;

	//printf("Inserted new element at pos %u\n", (unsigned int)i);
	//print_results(dimensions, count + 1, list);

}

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	size_t blobsize, work_index, local_index, dimensions, k, next_counter, i;
	unsigned int isFirstInner, terminate;
	struct setup_item* setup;

	GUID readerChannel;
	GUID writerChannel;

	void* work_items_base;
	void* local_items_base;

	unsigned int work_item_id;
	unsigned int local_item_id;

	unsigned int work_item_count;
	unsigned int local_item_count;

	PROBLEM_TYPE* work_items;
	PROBLEM_TYPE* local_items;
	struct result_entry* result_items;

	void* results;
	unsigned int* result_counters;
	unsigned int result_block_size;

	PROBLEM_TYPE distance;

	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	terminate = FALSE;

	CSP_SAFE_CALL("read setup", dsmcbe_csp_channel_read(SETUP_CHANNEL, NULL, (void**)&setup));

	readerChannel = RING_CHANNEL_BASE + (setup->processid % setup->processes);
	writerChannel = RING_CHANNEL_BASE + ((setup->processid + 1) % setup->processes);

	if (setup->processid != setup->processes - 1)
		CSP_SAFE_CALL("create ring channel", dsmcbe_csp_channel_create(writerChannel, 0, CSP_CHANNEL_TYPE_ONE2ONE));

	MALLOC(setup->processid * 16); //Scew the offsets

	dimensions = setup->dimensions;
	k = setup->k;

	for(i = 0; i < setup->processid; i++)
	{
		CSP_SAFE_CALL("forward, read", dsmcbe_csp_channel_read(readerChannel, NULL, (void**)&work_items_base));
		CSP_SAFE_CALL("forward, write", dsmcbe_csp_channel_write(writerChannel, work_items_base));
	}

	while(!terminate)
	{
		//printf("In main loop, first: %s\n", isFirstOuter ? "true" : "false");

		CSP_SAFE_CALL("read work", dsmcbe_csp_channel_read(readerChannel, &blobsize, (void**)&work_items_base));

		//This is the local copy that we work on
		local_items_base = MALLOC(blobsize);
		if (local_items_base == NULL)
		{
			REPORT_ERROR("Out of memory on SPU");
			exit(-1);
		}
		memcpy(local_items_base, work_items_base, blobsize);

		local_item_id = ((unsigned int*)local_items_base)[0];
		local_item_count = ((unsigned int*)local_items_base)[1];
		local_items = (PROBLEM_TYPE*)(local_items_base + (sizeof(unsigned int) * 2));
		isFirstInner = TRUE;

		//printf("SPE %u:%u has read block %d from channel: %d\n", setup->processid, local_item_id, ((unsigned int*)work_items_base)[0], readerChannel);

		result_block_size = sizeof(PROBLEM_TYPE) + sizeof(unsigned int);

		CSP_SAFE_CALL("allocate result list", dsmcbe_csp_item_create(&results, (sizeof(unsigned int) * 2) + (result_block_size * local_item_count * k)));

		result_counters = (unsigned int*)MALLOC(sizeof(unsigned int) * local_item_count);
		memset(result_counters, 0, sizeof(unsigned int) * local_item_count);

		((unsigned int*)results)[0] = local_item_id;
		((unsigned int*)results)[1] = local_item_count;
		result_items = (struct result_entry*)(results + (sizeof(unsigned int) * 2));

		while(TRUE)
		{
			//printf("Running inner loop, isFirstInner: %s\n", isFirstInner ? "true" : "false");

			//We need to run with the current block first
			if (!isFirstInner)
			{
				CSP_SAFE_CALL("read work", dsmcbe_csp_channel_read(readerChannel, &blobsize, (void**)&work_items_base));
				//printf("SPE %u:%u has read block %d from channel: %d\n", setup->processid, local_item_id, ((unsigned int*)work_items_base)[0], readerChannel);
			}

			work_item_id = ((unsigned int*)work_items_base)[0];
			work_item_count = ((unsigned int*)work_items_base)[1];
			work_items = (PROBLEM_TYPE*)(work_items_base + (sizeof(unsigned int) * 2));

			if (!isFirstInner && work_item_id == local_item_id)
			{
				//We've made a full round
				//printf("SPE %d is transmitting %d results for block %d\n", setup->blockid, ((unsigned int*)results)[1], ((unsigned int*)results)[0]);
				CSP_SAFE_CALL("writing results", dsmcbe_csp_channel_write(RESULT_CHANNEL, results));
				//printf(WHERESTR "Released pointer %u\n", WHEREARG, (unsigned int)results);

				next_counter = work_item_id + 1;

				//Move on with this one
				CSP_SAFE_CALL("forward, write", dsmcbe_csp_channel_write(writerChannel, work_items_base));
				//printf(WHERESTR "Released pointer %u\n", WHEREARG, (unsigned int)current);

				//printf("SPE %d is forwarding until %d\n", setup->blockid, local_item_id + setup->processes);

				if (local_item_id + setup->processes < setup->blockcount)
				{
					//Now move until we are the next process
					while(next_counter != local_item_id + setup->processes)
					{
						CSP_SAFE_CALL("forward, read", dsmcbe_csp_channel_read(readerChannel, NULL, (void**)&work_items_base));
						next_counter = (*((unsigned int*)work_items_base) + 1) % setup->blockcount;
						CSP_SAFE_CALL("forward, write", dsmcbe_csp_channel_write(writerChannel, work_items_base));
					}
				}
				else
				{

					if ((setup->processid + 1) % setup->processes == 0) //We are the last entry
					{
						if (setup->blockcount % setup->processes != 0) //If we are not the first, make sure we circulate all packages
						{
							//printf("SPE %d is forwarding %u blocks\n", setup->processid, (unsigned int)(setup->blockcount));
							for(i = 0; i < (setup->blockcount); i++)
							{
								CSP_SAFE_CALL("read in loop", dsmcbe_csp_channel_read(readerChannel, NULL, (void**)&work_items_base));
								//printf("SPE %d is forwarding %u\n", setup->processid, ((unsigned int*)work_items_base)[0]);
								CSP_SAFE_CALL("write in loop", dsmcbe_csp_channel_write(writerChannel, work_items_base));
							}
						}

						//printf("SPE %d is disposing %u blocks\n", setup->processid, (unsigned int)(setup->blockcount));
						for(i = 0; i < (setup->blockcount); i++)
						{
							CSP_SAFE_CALL("dispose-read in loop", dsmcbe_csp_channel_read(readerChannel, NULL, (void**)&work_items_base));
							//printf("SPE %d is disposing %u\n", setup->processid, ((unsigned int*)work_items_base)[0]);
							CSP_SAFE_CALL("free in loop", dsmcbe_csp_item_free(work_items_base));
						}

						//Now that the buffer is clear, we can poison the ring
						//printf("SPE %d is sending poison\n", setup->processid);
						CSP_SAFE_CALL("poison", dsmcbe_csp_channel_poison(writerChannel));
						//printf("SPE %d is quitting\n", setup->processid);
					}
					else
					{
						//printf("SPE %d is entering forward-only loop\n", setup->processid);

						//We are out out work, just forward packages until we are poisoned
						while(dsmcbe_csp_channel_read(readerChannel, NULL, (void**)&work_items_base) == CSP_CALL_SUCCESS)
						{
							//printf("SPE %d is forwarding %u\n", setup->processid, ((unsigned int*)work_items_base)[0]);
							CSP_SAFE_CALL("forward in loop", dsmcbe_csp_channel_write(writerChannel, work_items_base));
						}

						//printf("SPE %d is poisoned and forwarding it\n", setup->processid);
						CSP_SAFE_CALL("forward poison", dsmcbe_csp_channel_poison(writerChannel));
						//printf("SPE %d is quitting\n", setup->processid);
					}

					terminate = TRUE;
				}
				break;
			}
			else
			{
				isFirstInner = FALSE;

				//printf("SPE %d is working on %d, local elements: %d, current %d with elements: %d....\n", setup->processid, local_item_id, local_item_count, work_item_id, work_item_count);

				for(local_index = 0; local_index < local_item_count; local_index++)
				{
					for(work_index = 0; work_index < work_item_count; work_index++)
					{
						if (local_index != work_index || local_item_id != work_item_id)
						{
							distance = calc_distance(dimensions, &local_items[local_index * dimensions], &work_items[work_index * dimensions]);

							if (result_counters[local_index] == k && result_items[((k + 1) * local_index) - 1].distance > distance)
								result_counters[local_index]--; //Make room for the new element

							if (result_counters[local_index] < k)
							{
								insert_element(dimensions, result_counters[local_index], &result_items[k * local_index], (work_item_id * setup->items_pr_block) + work_index, distance);
								result_counters[local_index]++;
							}
						}
					}
				}

				//print_results(result_counters[0], &result_items[0]);

				//printf("SPE %d is forwarding block %d\n", setup->processid, work_item_id);
				CSP_SAFE_CALL("forward work block", dsmcbe_csp_channel_write(writerChannel, work_items_base));
				work_items_base = NULL;
			}
		}

		//Clean up
		FREE(local_items_base);
		FREE(result_counters);
	}

	return CSP_CALL_SUCCESS;
}

#include "kNN-Shared.h"
#include "ppu.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <malloc_align.h>
#include <free_align.h>
#include <memory.h>
#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <pthread.h>
#include <debug.h>

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

//Generates a block of d*n elements + the setup structure, all initialized with random data
int generate_random_block(void** data, unsigned int d, unsigned int n)
{
	void* block;
	PROBLEM_TYPE* items;
	size_t blocksize;
	size_t i;
	
	blocksize = (sizeof(unsigned int) * 2) + (sizeof(PROBLEM_TYPE) * d * n);

	CSP_SAFE_CALL("generate block", dsmcbe_csp_item_create(&block, blocksize));

	items = (PROBLEM_TYPE*)(block + (sizeof(unsigned int) * 2));

	for(i = 0; i < (d * n); i++)
		items[i] = ((double)rand() / (double)RAND_MAX) * 100000;

	*data = block;

	return CSP_CALL_SUCCESS;
}

int Find_kNN(unsigned int spu_count, unsigned int spu_fibers, unsigned int k, unsigned int d, unsigned int n)
{
	void* result;
	size_t total_size, element_size, elements_pr_block, blocks, buffer_required, i;
	struct setup_item* setup;
	void* data;
	pthread_t* spu_threads;
	unsigned int execution_units;
	unsigned int problem_size;

	//Initialize random seed
	srand(time(NULL));

	problem_size = DESIRED_PROBLEM_SIZE / spu_fibers;
	element_size = sizeof(PROBLEM_TYPE) * d;
	total_size = element_size * n;
	elements_pr_block = problem_size / element_size;
	blocks = (total_size + (problem_size - 1)) / problem_size;

	execution_units = spu_count * spu_fibers;

	if (execution_units > blocks) {
		fprintf(stderr, "There were %u SPUs with %u fibers, giving %u execution units, but only %u blocks\n", spu_count, spu_fibers, blocks);
		exit(-1);
	}

	printf("Finding %d nearest neighbors in %d elements with %d dimensions, running %d blocks on %d:%d SPUs\n", k, n, d, blocks, spu_count, spu_fibers);
	printf("Element size %d, total_size %d, elements_pr_block %d, blocks %d\n", element_size, total_size, elements_pr_block, blocks);
	spu_threads = dsmcbe_simpleInitialize(0, NULL, spu_count, spu_fibers);

	buffer_required = MAX((blocks - spu_count) + 1, 1);
	CSP_SAFE_CALL("create setup channel", dsmcbe_csp_channel_create(SETUP_CHANNEL, buffer_required, CSP_CHANNEL_TYPE_ONE2ANY));
	CSP_SAFE_CALL("create setup channel", dsmcbe_csp_channel_create(RING_CHANNEL_BASE, blocks, CSP_CHANNEL_TYPE_ONE2ANY));


	for(i = 0; i < blocks; i++)
	{
		CSP_SAFE_CALL("generate block", generate_random_block((void**)&data, d, elements_pr_block));

		((unsigned int*)data)[0] = i;

		if (i == blocks - 1)
			((unsigned int*)data)[1] = (n) - ((blocks - 1) * elements_pr_block);
		else
			((unsigned int*)data)[1] = elements_pr_block;

		CSP_SAFE_CALL("write setup", dsmcbe_csp_channel_write(RING_CHANNEL_BASE, data));
	}

	for(i = 0; i < blocks; i++)
	{
		CSP_SAFE_CALL("create setup", dsmcbe_csp_item_create((void**)&setup, sizeof(struct setup_item)));

		setup->processid = i;
		setup->blockcount = blocks;
		setup->items_pr_block = elements_pr_block;
		setup->processes = execution_units;
		setup->dimensions = d;
		setup->k = k;

		CSP_SAFE_CALL("write setup", dsmcbe_csp_channel_write(SETUP_CHANNEL, setup));
	}


	CSP_SAFE_CALL("create result channel", dsmcbe_csp_channel_create(RESULT_CHANNEL, 10, CSP_CHANNEL_TYPE_ANY2ONE));

	for(i = 0; i < blocks; i++)
	{
		CSP_SAFE_CALL("harvest result", dsmcbe_csp_channel_read(RESULT_CHANNEL, NULL, &result));
#ifdef DEBUG
		printf("PPC has received results for block %u with %u elements\n", ((unsigned int*)result)[0], ((unsigned int*)result)[1]);
#endif
		CSP_SAFE_CALL("free result", dsmcbe_csp_item_free(result));
	}

	for(i = 0; i < spu_count; i++)
		pthread_join(spu_threads[i], NULL);

	dsmcbe_terminate();

	return CSP_CALL_SUCCESS;
}

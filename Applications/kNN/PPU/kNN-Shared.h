#include <stdlib.h>
#include <stddef.h>

#ifndef KNN_SHARED_H_
#define KNN_SHARED_H_

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(!FALSE)
#endif

#define SETUP_CHANNEL (100)
#define RESULT_CHANNEL (101)

#define RING_CHANNEL_BASE (1000)

#define PROBLEM_TYPE float

#define DESIRED_PROBLEM_SIZE (64 * 1024)

#define DATA_BLOCK_SIZE(dimensions, elements) (sizeof(PROBLEM_TYPE) * dimensions * elements)

struct setup_item
{
	//The id of the block
	unsigned int processid;
	//The number of blocks
	size_t blockcount;
	//The number of elements pr. block
	size_t items_pr_block;
	//The number of working spu's
	size_t processes;
	//The k in kNN
	size_t k;
	//The number of dimensions for a single point
	size_t dimensions;
};

struct result_entry
{
	PROBLEM_TYPE distance;
	unsigned int elementId;
};


#endif /*KNN_SHARED_H_*/

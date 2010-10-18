#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include <malloc_align.h>
#include <free_align.h>
#include <math.h>
#include <unistd.h>
#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <debug.h>

#include "../header_files/StopWatch.h"
#include "../header_files/guids.h"
#include "../header_files/PPMReaderWriter.h"
#include "../header_files/Shared.h"
#include "../header_files/CSPProcesses.h"


int main(int argc, char* argv[])
{
	int spu_count;
	int spu_fibers;
	unsigned int fragment_size;
	unsigned int shots_pr_canon;
	unsigned int result_set_size;
	unsigned int fragment_replicas;

	char* input = NULL;
	char* output = NULL;	

	if(argc == 5) {
		spu_count = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
		input = argv[3];
		output = argv[4];
		fragment_size = 200;
		shots_pr_canon = 10000;
		fragment_replicas = 2;
		result_set_size = 16 * 1024;
	} else if(argc == 9) {
		spu_count = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
		input = argv[3];
		output = argv[4];
		fragment_size = (unsigned int)atoi(argv[5]);

		shots_pr_canon = (unsigned int)atoi(argv[6]);
		fragment_replicas = (unsigned int)atoi(argv[7]);
		result_set_size = (unsigned int)atoi(argv[8]);

	} else {
		printf("Wrong number of arguments %i\n", argc);
		printf("Usage:\n");
		printf(" ./BraintumorCSP <spu-count> <spu-fibers> <input-image> <output-image>\n");
		printf(" ./BraintumorCSP <spu-count> <spu-fibers> <input-image> <output-image> <fragment-size> <shots-pr-canon> <fragment-replicas> <result-set-size>\n");
		return -1;
	}
	
	//Start timer!
	sw_init();
	sw_start();

	pthread_t* threads;	
	char timer_buffer[256];
	size_t i;
	size_t j;
	size_t k;

	if (spu_count * spu_fibers <= 0)
	{
		REPORT_ERROR("Must specify at least one SPE for execution");
		exit(-1);
	}

	threads = dsmcbe_simpleInitialize(0, NULL, spu_count, spu_fibers);

	struct IMAGE_FORMAT_GREY inputImage;
	readimage_grey(input, &malloc, &inputImage);

	unsigned int fragments_w = (inputImage.width + (fragment_size - 1)) / fragment_size;
	unsigned int fragments_h = (inputImage.height + (fragment_size - 1)) / fragment_size;

	printf("Read image %s, and got an image size (%d x %d), which is split into (%d x %d) fragments of size (%d x %d)\n", input, inputImage.height, inputImage.width, fragments_h, fragments_w, fragment_size, fragment_size);
	printf("Using %u shots pr canon with %u fragment replicas and a result set size of %u\n", shots_pr_canon, fragment_replicas, result_set_size);

	struct MACHINE_SETUP machineSetup;
	machineSetup.fragment_width = fragment_size;
	machineSetup.fragment_heigth = fragment_size;
	machineSetup.map_width = inputImage.width;
	machineSetup.map_heigth = inputImage.height;
	machineSetup.workers = spu_count * spu_fibers;
	machineSetup.shotsPrCanon = shots_pr_canon;
	machineSetup.result_set_size = result_set_size;

	//Create the image fragments
	for(i = 0; i < fragments_h; i++)
		for(j = 0; j < fragments_w; j++)
		{
			char* data;

			unsigned int w = j == fragments_w - 1 ? (inputImage.width - (fragment_size * (fragments_w - 1))) : fragment_size;
			unsigned int h = i == fragments_h - 1 ? (inputImage.height - (fragment_size * (fragments_h - 1))) : fragment_size;

			CSP_SAFE_CALL("create fragment", dsmcbe_csp_item_create((void**)&data, w * h));

			int offsetx = fragment_size * j;
			int offsety = fragment_size * i;

			unsigned int imageIndex = (offsety * inputImage.width) + offsetx;

			//printf("pointer is %u, offsety is %u, offsetx is %u, imageIndex %u\n", (unsigned int)data, offsety, offsetx, imageIndex);
			for(k = 0; k < (unsigned int)h; k++)
			{
				//printf("pointer is %u, imageIndex is %u\n", (unsigned int)data, offsety, offsetx);

				memcpy(data + (k * w), inputImage.image + imageIndex, w);
				imageIndex += inputImage.width;
			}
			
			GUID channelId = IMAGEBUFFER + (i * fragments_w) + j;

			CSP_SAFE_CALL("create fragment channel", dsmcbe_csp_channel_create(channelId, fragment_replicas, CSP_CHANNEL_TYPE_ANY2ANY));

			for(k = 1; k < fragment_replicas; k++)
			{
				void* tmpCpy;
				CSP_SAFE_CALL("create replica", dsmcbe_csp_item_create(&tmpCpy, fragment_size * fragment_size));
				memcpy(tmpCpy, data, fragment_size * fragment_size);
				CSP_SAFE_CALL("write replica", dsmcbe_csp_channel_write(channelId, tmpCpy));
			}

			/*printf("Created image fragment (%d, %d) with dimensions (%d x %d) and wrote it into channel %d\n", i, j, h, w, channelId);
			printf("Fragment index %d, (%d,%d)-(%d,%d) first 4 bytes: %d, %d, %d, %d, last 4 bytes: %d, %d, %d, %d\n",
					(i * fragments_w) + j,
					j * fragment_size, i * fragment_size,
					(j * fragment_size) + w, (i * fragment_size) + h,
					(unsigned int)data[0], (unsigned int)data[1], (unsigned int)data[2], (unsigned int)data[3],
					(unsigned int)data[(w * h) - 4], (unsigned int)data[(w * h) - 3], (unsigned int)data[(w * h) - 2], (unsigned int)data[(w * h) - 1]
					);
			*/
			CSP_SAFE_CALL("write fragment", dsmcbe_csp_channel_write(channelId, data));

		}


	//Fire up the other CSP processes
	pthread_t collectorThread;
	pthread_t starterThread;
	pthread_create(&collectorThread, NULL, &collectResultsProccess, &inputImage);
	pthread_create(&starterThread, NULL, &fireCanonProccess, &machineSetup);
	
	//printf("Main thread is waiting for result\n");

	//Now we just wait for the result to arrive
	struct IMAGE_FORMAT* resultmap;
	CSP_SAFE_CALL("read result", dsmcbe_csp_channel_read(FINAL_RESULT_CHANNEL, NULL, (void**)&resultmap));
	
	//printf("Main thread is writing result to disk\n");

	//Save the image to disk so we can verify it
	writeimage_rgb(output, resultmap);

	CSP_SAFE_CALL("free result map", dsmcbe_csp_item_free(resultmap));
			
	// Stop timer
	sw_stop();
	sw_timeString(timer_buffer);
	printf("Time used: %s\n", timer_buffer);
	
	return 0;
}

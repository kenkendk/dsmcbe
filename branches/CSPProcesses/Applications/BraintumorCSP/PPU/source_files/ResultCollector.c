#include <stdlib.h>
#include <unistd.h>

#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <debug.h>

#include "../header_files/StopWatch.h"
#include "../header_files/guids.h"
#include "../header_files/PPMReaderWriter.h"
#include "../header_files/Shared.h"

//The CSP process that collects results and constructs a final output image
int collectResults(struct IMAGE_FORMAT_GREY* originalMap)
{
	size_t i;
	size_t j;

	//printf("Result collector is reading result count\n");

	//Create the incoming result channel
	CSP_SAFE_CALL("create result channel", dsmcbe_csp_channel_create(ENERGY_RESULT_CHANNEL, 4, CSP_CHANNEL_TYPE_ANY2ONE));

	unsigned int* dummy;
	CSP_SAFE_CALL("read result count", dsmcbe_csp_channel_read(WORK_COUNT_CHANNEL, NULL, (void**)&dummy));
	unsigned int result_count = *dummy;
	CSP_SAFE_CALL("free result count", dsmcbe_csp_item_free(dummy));

	//printf("Result collector got result count %d\n", result_count);

	//We need this number multiple times
	unsigned int imageSize = originalMap->width * originalMap->height;

	//Allocate room to keep the results in memory
	char* energyMap = (char*)malloc(imageSize);
	memset(energyMap, 0, imageSize);

	//Now read the energy results
	for(i = 0; i < result_count; i++)
	{
		struct ENERGY_POINT* answer;
		size_t answerSize;

		printf("Result collector is reading result %d of %d\n", i, result_count);
		CSP_SAFE_CALL("read result", dsmcbe_csp_channel_read(ENERGY_RESULT_CHANNEL, &answerSize, (void**)&answer));

		size_t loopCount = answerSize / sizeof(struct ENERGY_POINT);
		for(j = 0; j < loopCount; j++)
		{
			if (answer[j].x != UINT_MAX)
			{
				unsigned int index = answer[j].y * originalMap->width + answer[j].x;
				energyMap[index] =	MIN(255, energyMap[index] + 1);
			}
		}

		CSP_SAFE_CALL("free result", dsmcbe_csp_item_free((void*)answer));
	}

	printf("Result collector is constructing result image\n");

	//Allocate space for the final result image
	struct IMAGE_FORMAT* result;
	dsmcbe_csp_item_create((void**)&result, sizeof(struct IMAGE_FORMAT) + (imageSize * sizeof(struct RGB)));
	result->width = originalMap->width;
	result->height = originalMap->height;
	//The content part is allocated together with the control structure to make it fit into a single CSP package
	result->image = (struct RGB*)(((void*)result) + sizeof(struct IMAGE_FORMAT));

	//Create the result image, by first copying the gray scale image
	for(i = 0; i < imageSize; i++)
	{
		result->image[i].r =
		result->image[i].g =
		result->image[i].b =
			originalMap->image[i];
	}

	free(originalMap->image);
	originalMap->image = NULL;

	//The number of entries in the color map
	#define COLOR_MAP_SIZE (9)

	//This maps assigns the hit count to a specific color
	char colorMap[] = {
     //hits,   R,   G,   B
		 10,   0,   0,  85,
		 20,   0,   0, 170,
		 30,   0,   0, 255,
		 40,   0,  85,   0,
		 50,   0, 170,   0,
		 60,   0, 255,   0,
		 70,  85,   0,   0,
		 80, 170,   0,   0,
		 90, 255,   0,   0
	};


	//Apply the energy map results
	for(i = 0; i < imageSize; i++)
	{
		//If any particles hit this pixel, color it according to hit count
		if (energyMap[i] != 0)
		{
			//Figure out which interval to use
			int j;
			for(j = 0; j < COLOR_MAP_SIZE - 1; j++)
				if (energyMap[i] < colorMap[j * 4])
					break;

			//Update the pixel with the corresponding hit color
			result->image[i].r = colorMap[j + 1];
			result->image[i].g = colorMap[j + 2];
			result->image[i].b = colorMap[j + 3];
		}
	}

	//printf("Result collector is writing final result\n");

	//And we are done, so write the result
	CSP_SAFE_CALL("create final", dsmcbe_csp_channel_create(FINAL_RESULT_CHANNEL, 0, CSP_CHANNEL_TYPE_ONE2ONE));
	CSP_SAFE_CALL("write final", dsmcbe_csp_channel_write(FINAL_RESULT_CHANNEL, result));

	return 0;
}



//A wrapper function with the pthreads signature
void* collectResultsProccess(void* map)
{
	return (void*)collectResults((struct IMAGE_FORMAT_GREY*)map);

}

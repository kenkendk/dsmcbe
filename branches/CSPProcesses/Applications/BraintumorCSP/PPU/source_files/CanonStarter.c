#include <stdlib.h>
#include <unistd.h>

#include <dsmcbe_ppu.h>
#include <dsmcbe_csp.h>
#include <debug.h>

#include "../header_files/StopWatch.h"
#include "../header_files/guids.h"
#include "../header_files/PPMReaderWriter.h"
#include "../header_files/Shared.h"

//Internal representation of a canon
struct canonConfig
{
	//The canon X position
	int x;
	//The canon y position
	int y;
	//The particle x acceleration
	float ax;
	//The particle y acceleration
	float ay;
};

//Helper method to get a canon setup
int getConfig(struct canonConfig** cfg);

//The CSP process that starts all canons
int fireCanon(struct MACHINE_SETUP* setup)
{
	//printf("Canon starter is reading config\n");

	size_t i;
	size_t j;

	struct canonConfig* cfg;
	unsigned int canons = getConfig(&cfg);

	//To obey the memory constraints, we must ensure that each thread can hold the results
	unsigned int max_shots = setup->result_set_size / (sizeof(struct ENERGY_POINT) + 1);

	unsigned int workCount = setup->shotsPrCanon / max_shots;
	unsigned int slackShots = setup->shotsPrCanon - (workCount * max_shots);
	if (slackShots == 0)
		slackShots = max_shots;
	else
		workCount++;

	CSP_SAFE_CALL("create work channel", dsmcbe_csp_channel_create(WORK_CHANNEL, setup->workers, CSP_CHANNEL_TYPE_ONE2ANY));

	//printf("Canon starter is writing count %d * %d = %d to result collector\n", canons, workCount, canons * workCount);
	//We need to tell the result collector how many results to read, as we are the only process that knows this
	unsigned int* dummy;
	CSP_SAFE_CALL("create work count channel", dsmcbe_csp_channel_create(WORK_COUNT_CHANNEL, 1, CSP_CHANNEL_TYPE_ONE2ONE));
	CSP_SAFE_CALL("create work count object", dsmcbe_csp_item_create((void**)&dummy, sizeof(unsigned int)));
	*dummy = canons * workCount;
	CSP_SAFE_CALL("send work count", dsmcbe_csp_channel_write(WORK_COUNT_CHANNEL, dummy));
	CSP_SAFE_CALL("posion work count channel", dsmcbe_csp_channel_poison(WORK_COUNT_CHANNEL));

	//Figure out how many fragments there are, so the SPEs don't have to
	int fragments_w = (setup->map_width + (setup->fragment_width - 1)) / setup->fragment_width;
	int fragments_h = (setup->map_heigth + (setup->fragment_heigth - 1)) / setup->fragment_heigth;


	//Now send all the jobs
	for(i = 0; i < canons; i++)
	{
		for(j = 0; j < workCount; j++)
		{
			struct WORK_ORDER* work;
			CSP_SAFE_CALL("create work", dsmcbe_csp_item_create((void**)&work, sizeof(struct WORK_ORDER)));

			work->id = (i * workCount) + j;
			work->map_heigth = setup->map_heigth;
			work->map_width = setup->map_width;
			work->fragment_height = setup->fragment_heigth;
			work->fragment_width = setup->fragment_width;
			work->fragments_x = fragments_w;
			work->fragments_y = fragments_h;
			//Last work block picks up the slack
			work->shots = j == workCount - 1 ? slackShots : max_shots;
			work->canonX = cfg[i].x;
			work->canonY = cfg[i].y;
			work->canonAX = cfg[i].ax;
			work->canonAY = cfg[i].ay;

			//printf("Canon starter is writing job %d:%d = %d with %d shots\n", i, j, work->id, work->shots);
			CSP_SAFE_CALL("write work", dsmcbe_csp_channel_write(WORK_CHANNEL, work));
		}
	}

	//printf("Canon starter is done and poisoning work channel\n");

	//We are done, poison the channel to terminate the workers
	CSP_SAFE_CALL("poison work", dsmcbe_csp_channel_poison(WORK_CHANNEL));

	free(cfg);

	return 0;
}

//Constructs a demo canon setup
int getConfig(struct canonConfig** cfg)
{
	//TODO: This could be read from a config file
	*cfg = (struct canonConfig*)malloc(sizeof(struct canonConfig) * 5);
	(*cfg)[0].x = 85;
	(*cfg)[0].y = 75;
	(*cfg)[0].ax = 1.0;
	(*cfg)[0].ay = 0.8;

	(*cfg)[1].x = 10;
	(*cfg)[1].y = 230;
	(*cfg)[1].ax = 1.0;
	(*cfg)[1].ay = 0.0;

	(*cfg)[2].x = 550;
	(*cfg)[2].y = 230;
	(*cfg)[2].ax = -1.0;
	(*cfg)[2].ay = 0.0;

	(*cfg)[3].x = 475;
	(*cfg)[3].y = 90;
	(*cfg)[3].ax = -1.0;
	(*cfg)[3].ay = 0.75;

	(*cfg)[4].x = 280;
	(*cfg)[4].y = 0;
	(*cfg)[4].ax = 0.0;
	(*cfg)[4].ay = 1.0;

	return 5;
}


//A wrapper function with the pthreads signature
void* fireCanonProccess(void* setup)
{
	return (void*)fireCanon((struct MACHINE_SETUP*)setup);
}

#include "ppu.h"
#include <pthread.h>
#include <libspe2.h>
#include "StopWatch.h"
#include <unistd.h>

void FoldPrototein(char* proto, int machineid, char* networkfile, int spu_count, int spu_fibers);

int main(int argc,char** argv) {
	char buf[256];
	int spu_threads;
	int spu_fibers;
	int machineid;
	char* file;
	char* prototein;
	
	if(argc == 6) {
		machineid = atoi(argv[1]);
		file = argv[2]; 	
		spu_threads = atoi(argv[3]);
		spu_fibers = atoi(argv[4]);
		prototein = argv[5];
	} else if(argc == 5) {
		machineid = atoi(argv[1]);
		file = argv[2]; 	
		spu_threads = atoi(argv[3]);
		spu_fibers = atoi(argv[4]);
		prototein = "PPPHHHPPP";		 
	} else if (argc == 4) {
		machineid = 0;
		file = NULL;
		spu_threads = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
		prototein = argv[3];
	} else if (argc == 3) {
		machineid = 0;
		file = NULL; 	
		spu_threads = atoi(argv[1]);		 		
		spu_fibers = atoi(argv[2]);
		prototein ="PPPHHHPPP";
	} else if (argc == 2) {
		machineid = 0;
		file = NULL; 	
		spu_threads = atoi(argv[1]);		 		
		spu_fibers = 1;
		prototein ="PPPHHHPPP";		 
	} else {
		printf("Wrong number of arguments \"./PPU id network-file spu-threads spu-fibers prototein\"\n");
		printf("						  \"./PPU id network-file spu-threads spu-fibers\"\n");
		printf("						  \"./PPU spu-threads spu-fibers prototein\"\n");
		printf("						  \"./PPU spu-threads spu-fibers\"\n");
		printf("						  \"./PPU spu-threads\"\n");
		return -1;
	}
	
	if (spu_threads <= 0)
	{
		perror("There must be at least one SPU process\n");
		exit(1);
	}

	if (spu_fibers <= 0)
	{
		perror("There must be at least one SPU fiber\n");
		exit(1);
	}

	sw_init();
	sw_start();

	FoldPrototein(prototein, machineid, file, spu_threads, spu_fibers);
	
	sw_stop();
	sw_timeString(buf);
	
	printf("\nThe program executed successfully in %s.\n", buf);
	
	if (machineid == 0 && file != NULL)
		sleep(2);
	
	return (0);
}

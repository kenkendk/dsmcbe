#include "ppu.h"
#include <pthread.h>
#include <libspe2.h>
#include "StopWatch.h"
#include <unistd.h>

void FoldPrototein(char* proto, int machineid, char* networkfile, int spu_count);

int main(int argc,char** argv) {
	char buf[256];
	int spu_threads;
	int machineid;
	char* file;
	char* prototein;
	
	if(argc == 5) {
		machineid = atoi(argv[1]);
		file = argv[2]; 	
		spu_threads = atoi(argv[3]);
		prototein = argv[4];		 
	} else if(argc == 4) {
		machineid = atoi(argv[1]);
		file = argv[2]; 	
		spu_threads = atoi(argv[3]);
		prototein = "PPPHHHPPP";		 
	} else if (argc == 3) {
		machineid = 0;
		file = NULL; 	
		spu_threads = atoi(argv[1]);		 		
		prototein = argv[2];		 
	} else if (argc == 2) {
		machineid = 0;
		file = NULL; 	
		spu_threads = atoi(argv[1]);		 		
		prototein ="PPPHHHPPP";		 
	} else {
		printf("Wrong number of arguments \"./PPU id network-file spu-threads prototein\"\n");
		return -1;
	}
	
	/*if (spu_threads <= 0)
	{
		perror("There must be at least one SPU process\n");
		exit(1);
	}*/
	
	sw_init();
	sw_start();

	FoldPrototein(prototein, machineid, file, spu_threads);
	
	sw_stop();
	sw_timeString(buf);
	
	printf("\nThe program executed successfully in %s.\n", buf);
	
	if (machineid == 0 && file != NULL)
		sleep(2);
	
	return (0);
}

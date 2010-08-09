#include "ppu.h"
#include <pthread.h>
#include <libspe2.h>
#include "StopWatch.h"
#include <unistd.h>

int Find_kNN(int spu_count, unsigned int k, unsigned int d, unsigned int n);

int main(int argc,char** argv) {
	char buf[256];
	int spu_threads;
	
	int k;
	int d;
	int n;

	if(argc == 5) {
		spu_threads = atoi(argv[1]);		 		
		k = atoi(argv[2]);
		d = atoi(argv[3]);
		n = atoi(argv[4]);
	} else if (argc == 2) {
		spu_threads = atoi(argv[1]);
		k = 10;
		d = 72;
		n = 1000;
	} else {
		printf("Wrong number of arguments \"./PPU spu-threads\"\n");
		printf("                          \"./PPU spu-threads k dimensions elements\"\n");
		return -1;
	}
	
	if (spu_threads <= 0)
	{
		perror("There must be at least one SPU process\n");
		exit(-1);
	}

	if (k <= 0 || d <= 0 || n <= 0)
	{
		perror("k, d and n must be positive and larger than 1");
		exit(-1);
	}
	
	sw_init();
	sw_start();

	Find_kNN(spu_threads, k, d, n);
	
	sw_stop();
	sw_timeString(buf);
	
	printf("\nThe program executed successfully in %s.\n", buf);
	
	return (0);
}

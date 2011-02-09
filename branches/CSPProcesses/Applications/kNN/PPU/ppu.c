#include "ppu.h"
#include <pthread.h>
#include <libspe2.h>
#include "StopWatch.h"
#include <unistd.h>

int Find_kNN(int spu_count, int spu_fibers, unsigned int k, unsigned int d, unsigned int n);

int main(int argc,char** argv) {
	char buf[256];
	int spu_threads;
	int spu_fibers;
	
	int k;
	int d;
	int n;

	if(argc == 6) {
		spu_threads = atoi(argv[1]);		 		
		spu_fibers = atoi(argv[2]);
		k = atoi(argv[3]);
		d = atoi(argv[4]);
		n = atoi(argv[5]);
	} else if (argc == 3) {
		spu_threads = atoi(argv[1]);
		spu_fibers = atoi(argv[2]);
		k = 10;
		d = 72;
		n = 1000;
	} else {
		printf("Wrong number of arguments \"./PPU spu-threads spu-fibers\"\n");
		printf("                          \"./PPU spu-threads spu-fibers k dimensions elements\"\n");
		return -1;
	}
	
	if (spu_threads <= 0 || spu_fibers <= 0)
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

	Find_kNN(spu_threads, spu_fibers, k, d, n);
	
	sw_stop();
	sw_timeString(buf);
	
	printf("\nThe program executed successfully in %s.\n", buf);
	
	return (0);
}

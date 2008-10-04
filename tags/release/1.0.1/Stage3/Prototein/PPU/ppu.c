#include "ppu.h"
#include <pthread.h>
#include <libspe2.h>
#include "StopWatch.h"

void FoldPrototein(char* proto, int spu_count);

int main(int argc,char** argv) {
	char buf[256];
	int spu_threads;
	
	if (argc <= 1)
	{
		perror("Supply the number of SPU processes as the first argument\n");
		exit(1);
	}
	
	spu_threads = atoi(argv[1]);
	printf("SPU's: %d\n", spu_threads);
	
	if (spu_threads <= 0)
	{
		perror("There must be at least one SPU process\n");
		exit(1);
	}
	
	sw_init();
	sw_start();
	

	if (argc == 3)
		FoldPrototein(argv[2], spu_threads);
	else
		FoldPrototein("PPPHHHPPP", spu_threads);
	
	sw_stop();
	sw_timeString(buf);
	
	printf("\nThe program executed successfully in %s.\n", buf);
	
	return (0);
}

#include "ppu.h"
#include <pthread.h>
#include <libspe2.h>

void FoldPrototein(char* proto, int spu_count);

int main(int argc,char** argv) {
	int spu_threads;
	
	if (argc <= 1)
	{
		perror("Supply the number of SPU processes as the first argument\n");
		exit(1);
	}
	
	spu_threads = atoi(argv[1]);
	printf("Threads: %d, %d, %s, %s\n", spu_threads, argc, argv[1], argv[2]);
	
	if (spu_threads <= 0)
	{
		perror("There must be at least one SPU process\n");
		exit(1);
	}

	if (argc == 3)
		FoldPrototein(argv[2], spu_threads);
	else
		FoldPrototein("PPPHHHPPP", spu_threads);
	
	printf("\nThe program has successfully executed.\n");
	
	return (0);
}

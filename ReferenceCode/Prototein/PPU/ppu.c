#include "ppu.h"
#include "StopWatch.h"

DMATest(speid_t*, unsigned int);
void FoldPrototein(char* proto, speid_t* ids, pthread_t* threads, int spu_count);

void *ppu_pthread_function(void *arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0) 
	{
		perror ("Failed running context");
		exit (1);
	}
	pthread_exit(NULL);
}


int main(int argc,char** argv) {
	char buf[256];
	speid_t* spe_ids;
	pthread_t* threads;
	int i, j, status = 0, spu_threads;
	
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
	
	sw_init();
	sw_start();
	
	spe_ids = malloc(sizeof(speid_t) * spu_threads);
	threads = malloc(sizeof(pthread_t) * spu_threads);

	/*Create several SPE-threads to execute 'SPU'.*/
	for(i=0;i<spu_threads;i++)
	{
		/* Create context */
		if ((spe_ids[i] = spe_context_create (0, NULL)) == NULL) 
		{
			perror ("Failed creating context");
			exit (1);
		}
		
		/* Load program into context */
		if (spe_program_load (spe_ids[i], &SPU)) 
		{
			perror ("Failed loading program");
			exit (1);
		}

		/* Create thread for each SPE context */
		if (pthread_create (&threads[i], NULL,	&ppu_pthread_function, &spe_ids[i])) 
		{
			perror ("Failed creating thread");
			exit (1);
		}
	}	
	
	//DMATest(spe_ids, SPU_THREADS);
	if (argc == 3)
		FoldPrototein(argv[2], spe_ids, threads, spu_threads);
	else
		FoldPrototein("PPPHHHPPP", spe_ids, threads, spu_threads);
	
	free(spe_ids);
	free(threads);
	

	sw_stop();
	sw_timeString(buf);
	
	printf("\nThe program executed successfully in %s.\n", buf);
	
	return (0);
}


void send_mailbox_message_to_spe(speid_t target, unsigned int data_size, unsigned int* data)
{
	spe_in_mbox_write(target, data, data_size, SPE_MBOX_ALL_BLOCKING);
}

void WaitForSPUCompletion(pthread_t* threads, unsigned int spu_count)
{
	int i, status;
	/*Wait for SPU-thread to complete execution.*/
	for(i=0;i<spu_count;i++) {
		//pthread_join(threads[i], &status);
		//(void)spe_wait(spe_ids[i],&status,0);
	}
	
}
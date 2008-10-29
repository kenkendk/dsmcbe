#include "dsmcbe_ppu.h"
#include "common/debug.h"

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>

#include <memory.h>
#include <libspe2.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <malloc_align.h>
#include "StopWatch.h"
#include "Shared.h"

extern spe_program_handle_t SPU;

#ifdef NET_MODE
	int* sockfd;
	void* net_listen(void*);
	void* net_connect(void*);
#endif

void* ppu_pf(void* arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	
	//printf(WHERESTR "Starting SPU\n", WHEREARG);
	if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0)
	{
		REPORT_ERROR("Failed running context");
		return NULL;
	}
	//printf(WHERESTR "Terminated SPU\n", WHEREARG);
	pthread_exit(NULL);
}


int main(int argc, char* argv[])
{
	size_t i, thread_count;
	thread_count = 2;

	spe_context_ptr_t* spe_ids;
	pthread_t* spu_threads;

	if ((spe_ids = (spe_context_ptr_t*)malloc(thread_count * sizeof(spe_context_ptr_t))) == NULL)
		REPORT_ERROR("malloc error");
	
	if ((spu_threads = (pthread_t*)malloc(thread_count * sizeof(pthread_t))) == NULL)
		REPORT_ERROR("malloc error");

	
	for(i = 0; i < thread_count; i++)
	{
		if ((spe_ids[i] = spe_context_create (SPE_EVENTS_ENABLE, NULL)) == NULL) 
		{
			perror ("Failed creating context");
			return 0;
		}

		if (spe_program_load (spe_ids[i], &SPU))
		{
			perror ("Failed loading program");
			return 0;
		}

		if (pthread_create (&spu_threads[i], NULL,	&ppu_pf, &spe_ids[i])) 
		{
			perror ("Failed creating thread");
			return 0;
		}
	}


	char buf[256];
	unsigned int data_size = DATA_SIZE;
	#define SWAP_SPE (spe_no = (spe_no == 0 ? 1 : 0));
	volatile unsigned int* f = _malloc_align(data_size, 7);
	size_t spe_no = 0;

	sw_init();
	sw_start();

#ifdef MBOX_MODE
	char* mode = "MBOX";

	spe_in_mbox_write(spe_ids[spe_no], f, 1, 0);
	spe_out_mbox_read(spe_ids[spe_no], f, 1);
	while(*f != UINT_MAX)
	{
		printf("%i\n", *f);
		SWAP_SPE;
		spe_in_mbox_write(spe_ids[spe_no], f, 1, 0);
		spe_out_mbox_read(spe_ids[spe_no], f, 1);
	}	

#endif


#ifdef DMA_MODE	
	char* mode = "DMA";
	
	unsigned int x;
	spe_in_mbox_write(spe_ids[spe_no], &data_size, 1, 0);
	spe_in_mbox_write(spe_ids[spe_no], &f, 1, 0);
	spe_out_mbox_read(spe_ids[spe_no], &x, 1);
	while(*f != UINT_MAX)
	{
		printf("%i\n", *f);
		SWAP_SPE;
		spe_in_mbox_write(spe_ids[spe_no], &data_size, 1, 0);
		spe_in_mbox_write(spe_ids[spe_no], &f, 1, 0);
		spe_out_mbox_read(spe_ids[spe_no], &x, 1);
	}	
#endif

#ifdef NET_MODE

	char* mode = "NET";

	if ((sockfd = (int*)malloc(sizeof(int) * 2)) == NULL)
		REPORT_ERROR("malloc error");
	
	pthread_t p1, p2;
	pthread_create(&p1, NULL, net_listen, NULL);
	pthread_create(&p1, NULL, net_connect, NULL);
	
	pthread_join(p1, NULL);
	pthread_join(p2, NULL);
	
	unsigned int x;
	spe_in_mbox_write(spe_ids[spe_no], &data_size, 1, 0);
	spe_in_mbox_write(spe_ids[spe_no], &f, 1, 0);
	spe_out_mbox_read(spe_ids[spe_no], &x, 1);
	while(*f != UINT_MAX)
	{
		send(sockfd[spe_no], f, data_size, 0);
		printf("%i\n", *f);
		SWAP_SPE;
		recv(sockfd[spe_no], f, data_size, 0);
		spe_in_mbox_write(spe_ids[spe_no], &data_size, 1, 0);
		spe_in_mbox_write(spe_ids[spe_no], &f, 1, 0);
		spe_out_mbox_read(spe_ids[spe_no], &x, 1);
	}	
	
#endif

	sw_stop();
	sw_timeString(buf);
	printf("Time taken for %s with size %i: %s\n", mode, data_size, buf);
	
	return 0;
}

#ifdef NET_MODE
void* net_listen(void* dummy)
{
	unsigned int socketfd;
	struct sockaddr_in unused;
	
	struct sockaddr_in addr;
	memset(&(addr.sin_zero),'\0',8);
	inet_aton("127.0.0.1",&(addr.sin_addr));
	addr.sin_port = 6008;

	socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if(bind(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1)
	{
		REPORT_ERROR("bind()");
		exit(1);
	}
	
	if(listen(socketfd, 1) == -1)
	{
	  	REPORT_ERROR("listen()");
	  	exit(1);
	}
		  	
	unsigned int sin_size = sizeof(struct sockaddr_in);
	if ((sockfd[0] = accept(socketfd, (struct sockaddr *)&unused, &sin_size)) == -1)
	{
	  	REPORT_ERROR("accept()");
	  	exit(1);
	}

	return dummy;
}

void* net_connect(void* dummy)
{
	struct sockaddr_in addr;
	memset(&(addr.sin_zero),'\0',8);
	inet_aton("127.0.0.1",&(addr.sin_addr));
	addr.sin_port = 6008;
	
	REPORT_ERROR("Sleeping 5 secs to settle network");
	sleep(5);
	
	sockfd[1] = socket(AF_INET, SOCK_STREAM, 0);

	if(connect(sockfd[1], (struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1) {
		REPORT_ERROR("connect()");
		exit(1);
	}
	return dummy;
}
#endif

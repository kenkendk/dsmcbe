#include "../../dsmcbe.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../header files/PPUEventHandler.h"
#include "../header files/SPUEventHandler.h"
#include "../header files/RequestCoordinator.h"
#include "../header files/NetworkHandler.h"
#include "../../dsmcbe_ppu.h"

#include "../../common/debug.h"

static int mustrelease_spe_id = 0;
extern spe_program_handle_t SPU;
unsigned int dsmcbe_host_number = INT_MAX;

/* how many pending connections queue will hold */
#define BACKLOG 10

void* ppu_pthread_function(void* arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	//printf(WHERESTR "Starting SPU\n", WHEREARG);
	if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0)
	{
		perror ("Failed running context");
		return NULL;
	}
	//printf(WHERESTR "Terminated SPU\n", WHEREARG);
	pthread_exit(NULL);
}

void reverse(char s[])
{
    int c, i, j;

    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void itoa(int n, char s[])
{
    int i, sign;

    if ((sign = n) < 0)  /* record sign */
        n = -n;          /* make n positive */
    i = 0;
    do {       /* generate digits in reverse order */
        s[i++] = n % 10 + '0';   /* get next digit */
    } while ((n /= 10) > 0);     /* delete it */
    if (sign < 0)
        s[i++] = '-';
    s[i] = '\0';
    reverse(s);
} 

void updateNetworkFile(char* path, unsigned int networkcount)
{
	FILE* filesource; 
	FILE* filetarget;
	unsigned int port;
	char str[5];
	char ip[15];
	
	printf(WHERESTR "Starting network setup\n", WHEREARG);
	
	filesource = fopen(path , "r");
	filetarget = fopen("networkbackup.txt", "w");
	
	if (filesource == NULL || filetarget == NULL) 
       REPORT_ERROR("Error reading file");
			
	while(!feof(filesource)) { 
		if (fscanf(filesource, "%s %u", ip, &port) != 2 && feof(filesource))					
			break;
			
		port = port + networkcount;
		
		itoa(port, str);
			
		fputs(ip, filetarget);
		fputs(" ", filetarget); 
		fputs(str, filetarget);
		fputs("\n", filetarget); 
						
	}	
	
	fclose(filetarget);
	fclose(filesource);
	
	filesource = fopen("networkbackup.txt" , "r");
	filetarget = fopen(path, "w");
	
	if (filesource == NULL || filetarget == NULL) 
       REPORT_ERROR("Error reading file");
			
	while(!feof(filesource)) { 
		if (fscanf(filesource, "%s %s", ip, str) != 2 && feof(filesource))					
			break;
			
		fputs(ip, filetarget);
		fputs(" ", filetarget); 
		fputs(str, filetarget);
		fputs("\n", filetarget); 
						
	}	
	
	fclose(filetarget);
	fclose(filesource);
	
}

int* initializeNetwork(unsigned int id, char* path, unsigned int* count)
{
	FILE* filesource;
	struct sockaddr_in* network;
	if ((network = MALLOC(sizeof(struct sockaddr_in) * 10)) == NULL)
		REPORT_ERROR("malloc error");
	
	struct sockaddr_in addr;
	unsigned int port;
	char ip[15];
	unsigned int networkcount, j, k;
		
	printf(WHERESTR "Starting network setup\n", WHEREARG);
	
	filesource = fopen (path , "r");
	
	if (filesource == NULL) 
       REPORT_ERROR("Error reading file");
			
	networkcount = 0;	
	while(!feof(filesource)) { 
		if (fscanf(filesource, "%s %u", ip, &port) != 2 && feof(filesource))			
			break;
				
		addr.sin_family = AF_INET;
		inet_aton(ip,&(addr.sin_addr));		
		addr.sin_port = port;
		memset(&(addr.sin_zero),'\0',8);
		network[networkcount] = addr;
		networkcount++;
	}
		
	//for(j = 0; j < networkcount; j++)
		//printf("%s:%u\n", inet_ntoa(network[j].sin_addr), network[j].sin_port);

	fclose(filesource);
		
	int* sockfd;
	if ((sockfd = (int*)MALLOC(sizeof(int) * networkcount)) == NULL)
		REPORT_ERROR("malloc error");
	
	if (id == 0 && id < networkcount) {
		printf(WHERESTR "This machine is coordinator\n", WHEREARG);

		for(j = networkcount - 1; j > id; j--) {		
			printf(WHERESTR "This machine needs to connect to machine with id:  %i\n", WHEREARG, j);
			if((sockfd[j] = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			{
				REPORT_ERROR("socket()");
				exit(1);
			}
			
			int conres = -1;
			
			for(k = 0; k < 5; k++)
			{
				conres = connect(sockfd[j], (struct sockaddr *)&network[j], sizeof(struct sockaddr));
				if (conres == -1)
				{
					printf(WHERESTR "Host %d did not respond, retry in 5 sec, attempt %d of 5\n", WHEREARG, j, k + 1);
					sleep(5);
				}
				else
					break;
			}
			
			if (conres == -1)
			{
				REPORT_ERROR("connect()");
				exit(1);
			}
		}
		
		updateNetworkFile(path, networkcount);

	} else if (id < networkcount){
		printf(WHERESTR "This machine is not coordinator\n", WHEREARG);

		printf(WHERESTR "This machine needs to wait for connection from id: %i\n", WHEREARG, 0);
		
		sockfd[id] = socket(AF_INET, SOCK_STREAM, 0);
		if(sockfd[id] == -1) {
			REPORT_ERROR("socket()");
			exit(1);
		}
		  
		if(bind(sockfd[id], (struct sockaddr *)&network[id], sizeof(struct sockaddr)) == -1)
		{
			REPORT_ERROR("bind()");
			exit(1);
		}
		
		if(listen(sockfd[id], BACKLOG) == -1)
		{
		  	REPORT_ERROR("listen()");
		  	exit(1);
		}
		  	
		unsigned int sin_size = sizeof(struct sockaddr_in);
		if ((sockfd[0] = accept(sockfd[id], (struct sockaddr *)&network[id], &sin_size)) == -1)
		{
		  	REPORT_ERROR("accept()");
		  	exit(1);
		}
		
		for(j = networkcount - 1; j > id; j--) {		
			printf(WHERESTR "This machine needs to connect to machine with id: %i\n", WHEREARG, j);

			if((sockfd[j] = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			{
				REPORT_ERROR("socket()");
				exit(1);
			}
						
			if(connect(sockfd[j], (struct sockaddr *)&network[j], sizeof(struct sockaddr)) == -1) {
				REPORT_ERROR("connect()");
				exit(1);
			}
		}
			  	
		for(j = id - 1; j > 0; j--) {
			printf(WHERESTR "This machine needs to wait for connection from id: %i\n", WHEREARG, j);			  	
			if ((sockfd[j] = accept(sockfd[id], (struct sockaddr *)&network[id], &sin_size)) == -1)
			{
			  	REPORT_ERROR("accept()");
			  	exit(1);
			}
		}
	} else
		REPORT_ERROR("Cannot parse ID with IP/PORT from network file");
			
	printf(WHERESTR "Network setup completed\n", WHEREARG);
	FREE(network);
	network = NULL;
	*count = networkcount;
	return sockfd;
}

pthread_t* simpleInitialize(unsigned int id, char* path, unsigned int thread_count)
{
	size_t i;
	spe_context_ptr_t* spe_ids;
	pthread_t* spu_threads;
	int* sockets = NULL;
	unsigned int socketsCount = 0;		

	if (path != NULL) {
		dsmcbe_host_number = id;
		sockets = initializeNetwork(id, path, &socketsCount);
	} else
		dsmcbe_host_number = 0;
		
	if (thread_count > 0)
	{
		if ((spe_ids = (spe_context_ptr_t*)MALLOC(thread_count * sizeof(spe_context_ptr_t))) == NULL)
			REPORT_ERROR("dsmcbe.c: malloc error");
		
		if ((spu_threads = (pthread_t*)MALLOC(thread_count * sizeof(pthread_t))) == NULL)
			REPORT_ERROR("dsmcbe.c: malloc error");
	
		mustrelease_spe_id = 1;
		
		// Create several SPE-threads to execute 'SPU'.
		for(i = 0; i < thread_count; i++){
			// Create context
			if ((spe_ids[i] = spe_context_create (0, NULL)) == NULL) 
			{
				perror ("Failed creating context");
				return NULL;
			}
	
			// Load program into context
			if (spe_program_load (spe_ids[i], &SPU)) 
			{
				perror ("Failed loading program");
				return NULL;
			}
	
			//printf(WHERESTR "Starting SPU thread\n", WHEREARG);
			// Create thread for each SPE context
			if (pthread_create (&spu_threads[i], NULL,	&ppu_pthread_function, &spe_ids[i])) 
			{
				perror ("Failed creating thread");
				return NULL;
			}
		}
	}
	else
	{
		mustrelease_spe_id = 0;
		spe_ids = NULL;
		spu_threads = NULL;
	}	
	initialize(spe_ids, thread_count, sockets, socketsCount);
	
	return spu_threads;
}

void initialize(spe_context_ptr_t* threads, unsigned int thread_count, int* sockets, unsigned int socketsCount)
{
	InitializeCoordinator();
	InitializePPUHandler();
	InitializeSPUHandler(threads, thread_count);
	InitializeNetworkHandler(sockets, socketsCount);	
}

void* create(GUID id, unsigned long size){
	return threadCreate(id, size);
}

void* acquire(GUID id, unsigned long* size, int type){
	return threadAcquire(id, size, type);	
}

void release(void* data){
	threadRelease(data);
}

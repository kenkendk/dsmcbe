//#define NO_EMBEDDED_SPU

#include <dsmcbe.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <PPUEventHandler.h>
#include <SPUEventHandler.h>
#include <RequestCoordinator.h>
#include <NetworkHandler.h>
#include <dsmcbe_ppu.h>
#include <debug.h>
#include <dsmcbe_initializers.h>
#include <stdlib.h>

extern unsigned int dsmcbe_net_remote_hosts;
static int mustrelease_spe_id = 0;
extern spe_program_handle_t SPU;
OBJECT_TABLE_ENTRY_TYPE dsmcbe_host_number = OBJECT_TABLE_RESERVED;
static int dsmcbe_display_network_startup_value = 0;

void dsmcbe_display_network_startup(int value) { dsmcbe_display_network_startup_value = value; }

void* dsmcbe_ppu_pthread_function(void* arg) {
	spe_context_ptr_t ctx;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	ctx = *((spe_context_ptr_t *)arg);
	
	//printf(WHERESTR "Starting SPU\n", WHEREARG);
	if (spe_context_run(ctx, &entry, 0, (void*)((int)dsmcbe_host_number), NULL, NULL) < 0)
	{
		REPORT_ERROR("Failed running context");
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

void dsmcbe_ppu_updateNetworkFile(char* path, unsigned int networkcount)
{
	return;
	
	FILE* filesource; 
	FILE* filetarget;
	unsigned int port;
	char str[5];
	char ip[15];
	
	if (dsmcbe_display_network_startup_value)
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

int* dsmcbe_ppu_initializeNetwork(unsigned int id, char* path, unsigned int* count)
{
	FILE* filesource;
	struct sockaddr_in* network;
	//TODO: Do not hardcode a limit of 100 machines :)
	network = MALLOC(sizeof(struct sockaddr_in) * 100);
	
	struct sockaddr_in addr;
	unsigned int port;
	char ip[256];
	unsigned int networkcount, j, k;
		
	if (dsmcbe_display_network_startup_value)
		printf(WHERESTR "Starting network setup\n", WHEREARG);
	
	filesource = fopen (path , "r");
	
	if (filesource == NULL) 
       REPORT_ERROR("Error reading file");
			
	networkcount = 0;	
	while(!feof(filesource)) { 
		//What happens if hostname > 256 chars?
		if (fscanf(filesource, "%s %u", ip, &port) != 2 && feof(filesource))			
			break;
		
		struct hostent *he;
		he = gethostbyname(ip);
		addr.sin_family = AF_INET;

		if (he != NULL)
		{
			if (*((unsigned long*)(he->h_addr_list[0])) == 0)
				he = NULL;
			else
				addr.sin_addr.s_addr = *((unsigned long*)(he->h_addr_list[0]));
		}

		if (he == NULL)
		{
			if (inet_aton(ip,&(addr.sin_addr)) == 0)
			{		
				fprintf(stderr, WHERESTR "Failed to find/parse host: %s\n", WHEREARG, ip);
				exit(-1);
			} 
		}
		
		//printf("IP parsed gave: %d, name gave: %d\n", addr.sin_addr.s_addr, he->h_addr);
		
		addr.sin_port = port;
		memset(&(addr.sin_zero),'\0',8);
		network[networkcount] = addr;
		networkcount++;
	}
		
	//for(j = 0; j < networkcount; j++)
		//printf("%s:%u\n", inet_ntoa(network[j].sin_addr), network[j].sin_port);

	fclose(filesource);
		
	int* sockfd = MALLOC(sizeof(int) * networkcount);
	
	if (id == 0 && id < networkcount) {
		if (dsmcbe_display_network_startup_value)
			printf(WHERESTR "This machine is coordinator\n", WHEREARG);

		for(j = networkcount - 1; j > id; j--) {		
			if (dsmcbe_display_network_startup_value)
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
					printf(WHERESTR "Host %d did not respond, retry in 5 sec, attempt %d of 5 (port: %d)\n", WHEREARG, j, k + 1, network[j].sin_port);
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
		
		dsmcbe_ppu_updateNetworkFile(path, networkcount);

	} else if (id < networkcount){
		if (dsmcbe_display_network_startup_value)
			printf(WHERESTR "This machine is not coordinator\n", WHEREARG);

		if (dsmcbe_display_network_startup_value)
			printf(WHERESTR "This machine needs to wait for connection from id: %i\n", WHEREARG, 0);
		
		sockfd[id] = socket(AF_INET, SOCK_STREAM, 0);
		if(sockfd[id] == -1) {
			REPORT_ERROR("socket()");
			exit(1);
		}

		if (dsmcbe_display_network_startup_value)
			printf(WHERESTR "This machine starts to listen on port: %i\n", WHEREARG, network[id].sin_port);
		  
		if(bind(sockfd[id], (struct sockaddr *)&network[id], sizeof(struct sockaddr)) == -1)
		{
			REPORT_ERROR("bind()");
			exit(1);
		}

		if (dsmcbe_display_network_startup_value)
			printf(WHERESTR "This machine listens on port: %i\n", WHEREARG, network[id].sin_port);

		//We set the backlog to the number of machines
		if(listen(sockfd[id], networkcount) == -1)
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
			if (dsmcbe_display_network_startup_value)
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
			if (dsmcbe_display_network_startup_value)
				printf(WHERESTR "This machine needs to wait for connection from id: %i\n", WHEREARG, j);			  	
			
			if ((sockfd[j] = accept(sockfd[id], (struct sockaddr *)&network[id], &sin_size)) == -1)
			{
			  	REPORT_ERROR("accept()");
			  	exit(1);
			}
		}
	} else
		REPORT_ERROR("Cannot parse ID with IP/PORT from network file");
			
	if (dsmcbe_display_network_startup_value)
		printf(WHERESTR "Network setup completed\n", WHEREARG);
		
	FREE(network);
	network = NULL;
	*count = networkcount;
	return sockfd;
}

pthread_t* dsmcbe_simpleInitialize(unsigned int id, char* path, unsigned int thread_count)
{
	size_t i;
	spe_context_ptr_t* spe_ids;
	pthread_t* spu_threads;
	int* sockets = NULL;
	unsigned int socketsCount = 0;
#ifdef NO_EMBEDDED_SPU
	spe_program_handle_t* program;
#endif		

	// Make GLIB thread safe - this is not default behavior
	if (!g_thread_supported ())
		g_thread_init (NULL);

	if (!g_thread_supported ())
		printf(WHERESTR "Warning: GLIB is not THREAD SAFE!!\n", WHEREARG);

	const gchar* version = NULL;

	if ((version = glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION)) != NULL)
	{
		printf(WHERESTR "Bad version of GLIB (%d.%d.%d). Version should be (%d.%d.%d) or newer!!\n", WHEREARG, glib_major_version, glib_minor_version, glib_micro_version, GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
		REPORT_ERROR2("%s", version);
		exit(1);
		return NULL;
	}

 	if (path != NULL) {
		dsmcbe_host_number = id;
		sockets = dsmcbe_ppu_initializeNetwork(id, path, &socketsCount);
	} else
		dsmcbe_host_number = 0;
		
	if (thread_count > 0)
	{
		spe_ids = MALLOC(thread_count * sizeof(spe_context_ptr_t));
		spu_threads = MALLOC(thread_count * sizeof(pthread_t));
	
		mustrelease_spe_id = 1;

#ifdef NO_EMBEDDED_SPU		
		program = spe_image_open("spu.elf");
#endif
		
		// Create several SPE-threads to execute 'SPU'.
		for(i = 0; i < thread_count; i++){
			// Create context
			
			//If events are in use, SPE_EVENTS_ENABLE must be on. Apparently there is no cost in just leaving it on
			//SPE_MAP_PS enables libspe to use memory mapped IO, rather than file handle based
			//SPE_MAP_PS cannot be used if we want to use the SPE_EVENT_TAG_GROUP event
			if ((spe_ids[i] = spe_context_create (SPE_EVENTS_ENABLE /*| SPE_MAP_PS*/, NULL)) == NULL) 
			{
				perror ("Failed creating context");
				return NULL;
			}
	
			//printf(WHERESTR "Loading SPU image\n", WHEREARG);
			// Load program into context
#ifdef NO_EMBEDDED_SPU			
			if (spe_program_load (spe_ids[i], program))
#else
			if (spe_program_load (spe_ids[i], &SPU))
#endif 
			{
				perror ("Failed loading program");
				return NULL;
			}
	
			//printf(WHERESTR "Starting SPU thread\n", WHEREARG);
			// Create thread for each SPE context
			if (pthread_create (&spu_threads[i], NULL,	&dsmcbe_ppu_pthread_function, &spe_ids[i]))
			{
				perror ("Failed creating thread");
				return NULL;
			}
			//printf(WHERESTR "Started SPU thread\n", WHEREARG);
		}

#ifdef NO_EMBEDDED_SPU
		spe_image_close(program);
#endif
	}
	else
	{
		mustrelease_spe_id = 0;
		spe_ids = NULL;
		spu_threads = NULL;
	}	
	
	//printf(WHERESTR "Calling initialize\n", WHEREARG);

	dsmcbe_initialize(spe_ids, thread_count, sockets, socketsCount);
	
	return spu_threads;
}

void dsmcbe_initialize(spe_context_ptr_t* threads, unsigned int thread_count, int* sockets, unsigned int socketsCount)
{
	//The coordinator needs this
	dsmcbe_net_remote_hosts = socketsCount;
	
	//printf(WHERESTR "Calling Initialize Coordinator\n", WHEREARG);
	dsmcbe_rc_initialize();
	//printf(WHERESTR "Calling Initialize Network\n", WHEREARG);
	dsmcbe_net_initialize(sockets, socketsCount);
	//printf(WHERESTR "Calling Initialize PPU\n", WHEREARG);
	dsmcbe_ppu_initialize();
	//printf(WHERESTR "Calling Initialize SPU\n", WHEREARG);
	dsmcbe_spu_initialize(threads, thread_count);
	//printf(WHERESTR "Done Initialize\n", WHEREARG);
}

void* dsmcbe_create(GUID id, unsigned long size){
	return dsmcbe_ppu_create(id, size);
}

void* dsmcbe_acquire(GUID id, unsigned long* size, int type){
	return dsmcbe_ppu_acquire(id, size, type);
}

void dsmcbe_release(void* data){
	dsmcbe_ppu_release(data);
}

void dsmcbe_acquireBarrier(GUID id)
{
	dsmcbe_ppu_acquireBarrier(id);
}

void dsmcbe_createBarrier(GUID id, unsigned int count)
{
	unsigned int* tmp = dsmcbe_create(id, sizeof(unsigned int) * 2);
	if (tmp == NULL)
	{
		REPORT_ERROR("Failed to create barrier");
		return;
	}
	
	tmp[0] = count;
	tmp[1] = 0;
	dsmcbe_release(tmp);
}


void dsmcbe_terminate()
{
	//TODO: Implement this!
}

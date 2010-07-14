#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <poll.h>

//#define TRACE_NETWORK_PACKAGES

#include <RequestCoordinator.h>
#include <debug.h>
#include <NetworkHandler.h>
#include <dsmcbe_initializers.h>
#include <stdlib.h>

#include <glib.h>
#include <limits.h>

struct dsmcbe_net_QueueableItemWrapper
{
	QueueableItem ui;
	unsigned int origId;
};

//There can be no more than this many pending requests
#define NET_MAX_SEQUENCE 1000000

//This is used to terminate the thread
volatile int dsmcbe_net_do_terminate;

//This is a handle to the thread
pthread_t dsmcbe_net_read_thread;
pthread_t dsmcbe_net_write_thread;

//This is the mutex and condition that protects the work queues
pthread_mutex_t dsmcbe_net_work_mutex;
pthread_cond_t dsmcbe_net_work_ready;

//Each remote host has its own requestQueue
GQueue** dsmcbe_net_GrequestQueues;

//Keep track of which objects are in queue for the network
GHashTable* dsmcbe_net_GactiveTransfers;

//Keep a list of pending invalidates for each object
GHashTable* dsmcbe_net_GpendingInvalidates;

//The number of hosts
unsigned int dsmcbe_net_remote_hosts;
//An array of file descriptors for the remote hosts
int* dsmcbe_net_remote_handles;

void* dsmcbe_net_Reader(void* data);
void* dsmcbe_net_Writer(void* data);
void dsmcbe_net_sendPackage(void* package, unsigned int machineId);
void* dsmcbe_net_readPackage(int fd);
void dsmcbe_net_processPackage(void* data, unsigned int machineId);
OBJECT_TABLE_ENTRY_TYPE dsmcbe_rc_GetMachineID(GUID id);

#define CAST_TO_PACKAGE(x) ((struct dsmcbe_createRequest*)((x)->dataRequest))
#define CAST_AS_PACKAGE(x) ((struct dsmcbe_createRequest*)x)

//These tables are used to translate requestIDs from internal seqences to network
// sequence and back again
GHashTable** dsmcbe_net_Gidlookups;

//These are the sequence numbers assigned to the packages, a unique sequence for each host
unsigned int* dsmcbe_net_sequenceNumbers;

inline unsigned int dsmcbe_MachineCount() { return dsmcbe_net_remote_hosts; }

void dsmcbe_net_Unsubscribe(GUID dataitem, unsigned int machineId)
{
	struct dsmcbe_releaseRequest* req;

	if (dsmcbe_net_remote_hosts == 0)
	{
		//printf(WHERESTR "Returning\n", WHEREARG);
		return;
	}
	
	if (dataitem == 0)
	{
		REPORT_ERROR("bad request");
		return;
	}

	if (machineId > dsmcbe_net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}

	req = dsmcbe_new_releaseRequest(dataitem, 0, ACQUIRE_MODE_READ, 0, 0, NULL);
		
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&dsmcbe_net_work_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	g_queue_push_tail(dsmcbe_net_GrequestQueues[machineId], req);
	pthread_cond_signal(&dsmcbe_net_work_ready);
	pthread_mutex_unlock(&dsmcbe_net_work_mutex);
	
}

void dsmcbe_net_RespondToInvalidate(QueueableItem ui)
{
	dsmcbe_rc_EnqueInvalidateResponse(CAST_TO_PACKAGE(ui)->dataItem, CAST_TO_PACKAGE(ui)->requestID);
	free(ui->dataRequest);
	ui->dataRequest = NULL;
	free(ui);
	ui = NULL;	
}

void dsmcbe_net_EnqueueCallback(QueueableItem item, void* data)
{
	if (dsmcbe_net_remote_hosts == 0)
		return;

	//Remove unused parameter warning
	item = item;

	struct dsmcbe_createRequest* req = (struct dsmcbe_createRequest*)data;

	//Mark the object as active
	if (
		req->packageCode == PACKAGE_ACQUIRE_RESPONSE ||
		req->packageCode == PACKAGE_MIGRATION_RESPONSE ||
		req->packageCode == PACKAGE_RELEASE_REQUEST
	)
	{
		unsigned int* v = g_hash_table_lookup(dsmcbe_net_GactiveTransfers, (void*)(req->dataItem));
		
		if (v == NULL)
		{
			v = MALLOC(sizeof(unsigned int));
			*v = 0;
			g_hash_table_insert(dsmcbe_net_GactiveTransfers, (void*)(req->dataItem), v);
		}

		(*v)++;
	}
}

void dsmcbe_net_Request(QueueableItem item, unsigned int machineId)
{
	unsigned int nextId;
	struct dsmcbe_net_QueueableItemWrapper* w;
	int packagesize;
	
	void* datacopy;
		
	if (dsmcbe_net_remote_hosts == 0)
	{
		printf(WHERESTR "Returning\n", WHEREARG);
		return;
	}
	
	if (item == NULL || item->dataRequest == NULL)
	{
		REPORT_ERROR("bad request");
		return;
	}

	if (CAST_TO_PACKAGE(item)->packageCode == PACKAGE_INVALIDATE_REQUEST)
	{
		if (dsmcbe_net_remote_hosts == 0)
		{
			dsmcbe_net_RespondToInvalidate(item);
			return;
		}
		

		pthread_mutex_lock(&dsmcbe_net_work_mutex);
		
		unsigned int* v = g_hash_table_lookup(dsmcbe_net_GactiveTransfers, (void*)CAST_TO_PACKAGE(item)->dataItem);
		if (v == NULL || *v == 0)
		{
			pthread_mutex_unlock(&dsmcbe_net_work_mutex);
			dsmcbe_net_RespondToInvalidate(item);
			return;
		}
		else
		{
			//REPORT_ERROR("Delaying invalidate!");
			if (g_hash_table_lookup(dsmcbe_net_GpendingInvalidates, (void*)CAST_TO_PACKAGE(item)->dataItem) != NULL)
			{
				REPORT_ERROR("Found pending invalidates in table already!");
			}
			else 
			{
				g_hash_table_insert(dsmcbe_net_GpendingInvalidates, (void*)CAST_TO_PACKAGE(item)->dataItem, item);
			}
				
			pthread_mutex_unlock(&dsmcbe_net_work_mutex);
			return;
		}
	}

	if (machineId > dsmcbe_net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
			
	w = MALLOC(sizeof(struct dsmcbe_net_QueueableItemWrapper));
	if (w == NULL)
		return;

	packagesize = PACKAGE_SIZE(CAST_TO_PACKAGE(item)->packageCode);
	
	datacopy = MALLOC(packagesize);
	if (datacopy == NULL)
		return;
		
	memcpy(datacopy, item->dataRequest, packagesize);
	
	w->ui = item;
	w->origId = CAST_TO_PACKAGE(item)->requestID;
	
	pthread_mutex_lock(&dsmcbe_net_work_mutex);
	
	nextId = NEXT_SEQ_NO(dsmcbe_net_sequenceNumbers[machineId], NET_MAX_SEQUENCE);
	CAST_AS_PACKAGE(datacopy)->requestID = nextId;
	
	dsmcbe_net_EnqueueCallback(NULL, datacopy);
	
	if (g_hash_table_lookup(dsmcbe_net_Gidlookups[machineId], (void*)nextId) == NULL)
		g_hash_table_insert(dsmcbe_net_Gidlookups[machineId], (void*)nextId, w);
	else
		REPORT_ERROR("Could not insert into net_idlookups")
	
	g_queue_push_tail(dsmcbe_net_GrequestQueues[machineId], datacopy);
	
	pthread_cond_signal(&dsmcbe_net_work_ready);
	pthread_mutex_unlock(&dsmcbe_net_work_mutex);
}

void dsmcbe_net_initialize(int* remote_handles, unsigned int remote_hosts)
{
	
	size_t i;
	pthread_attr_t attr;
	dsmcbe_net_do_terminate = 0;

	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, remote_hosts);
	dsmcbe_net_remote_hosts = remote_hosts;
	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, dsmcbe_net_remote_hosts);
	dsmcbe_net_remote_handles = remote_handles;
	
	if (dsmcbe_net_remote_hosts == 0)
	{
		//printf(WHERESTR "Cannot initialize network, no remote hosts found!\n", WHEREARG);
		return;
	}
	pthread_mutex_init(&dsmcbe_net_work_mutex, NULL);
	pthread_cond_init (&dsmcbe_net_work_ready, NULL);
		
	dsmcbe_net_GrequestQueues = MALLOC(sizeof(GQueue*) * dsmcbe_net_remote_hosts);
	dsmcbe_net_Gidlookups = MALLOC(sizeof(GHashTable*) * dsmcbe_net_remote_hosts);
	dsmcbe_net_sequenceNumbers = MALLOC(sizeof(unsigned int) * dsmcbe_net_remote_hosts);

	for(i = 0; i < dsmcbe_net_remote_hosts; i++)
	{
		dsmcbe_net_GrequestQueues[i] = g_queue_new();
		dsmcbe_net_Gidlookups[i] = g_hash_table_new(NULL, NULL);
		dsmcbe_net_sequenceNumbers[i] = 0;
	}

	dsmcbe_net_GactiveTransfers = g_hash_table_new(NULL, NULL);
	dsmcbe_net_GpendingInvalidates = g_hash_table_new(NULL, NULL);
	
	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	pthread_create(&dsmcbe_net_read_thread, &attr, dsmcbe_net_Reader, NULL);
	pthread_create(&dsmcbe_net_write_thread, &attr, dsmcbe_net_Writer, NULL);

	pthread_attr_destroy(&attr);
	
	for(i = 0; i < dsmcbe_net_remote_hosts; i++)
		dsmcbe_rc_RegisterInvalidateSubscriber(&dsmcbe_net_work_mutex, &dsmcbe_net_work_ready, &dsmcbe_net_GrequestQueues[i], i);
}

void dsmcbe_net_Update(GUID id, unsigned int offset, unsigned int dataSize, void* data)
{
	struct dsmcbe_updateRequest* req;
	size_t i;
	
	if (dsmcbe_net_remote_hosts == 0)
		return;
	
	pthread_mutex_lock(&dsmcbe_net_work_mutex);
	for(i = 0; i < dsmcbe_net_remote_hosts; i++)
	{
		if (i != dsmcbe_host_number)
		{
			req = dsmcbe_new_updateRequest(id, 0, dataSize, offset, data);
			g_queue_push_tail(dsmcbe_net_GrequestQueues[i], req);
		}
	}
	
	pthread_cond_signal(&dsmcbe_net_work_ready);

	pthread_mutex_unlock(&dsmcbe_net_work_mutex);
}

void dsmcbe_net_terminate(int force)
{
	
	size_t i;

	dsmcbe_net_do_terminate = force ? 1 : 1;

	if (dsmcbe_net_remote_hosts == 0)
		return;
	
	pthread_mutex_lock(&dsmcbe_net_work_mutex);
	pthread_cond_signal(&dsmcbe_net_work_ready);
	pthread_mutex_unlock(&dsmcbe_net_work_mutex);
	
	pthread_join(dsmcbe_net_read_thread, NULL);
	pthread_join(dsmcbe_net_write_thread, NULL);

	for(i = 0; i < dsmcbe_net_remote_hosts; i++)
		dsmcbe_rc_UnregisterInvalidateSubscriber(&dsmcbe_net_GrequestQueues[i]);

	for(i = 0; i < dsmcbe_net_remote_hosts; i++)
	{
		g_queue_free(dsmcbe_net_GrequestQueues[i]);
		dsmcbe_net_GrequestQueues[i] = NULL;
		g_hash_table_destroy(dsmcbe_net_Gidlookups[i]);
		dsmcbe_net_Gidlookups[i] = NULL;
	}
	
	FREE(dsmcbe_net_GrequestQueues);
	dsmcbe_net_GrequestQueues = NULL;
	FREE(dsmcbe_net_Gidlookups);
	dsmcbe_net_Gidlookups = NULL;
	FREE(dsmcbe_net_sequenceNumbers);
	dsmcbe_net_sequenceNumbers = NULL;
	
	pthread_mutex_destroy(&dsmcbe_net_work_mutex);
	pthread_cond_destroy(&dsmcbe_net_work_ready);
}

void* dsmcbe_net_Reader(void* data)
{
	
	struct pollfd* sockets;
	size_t i;
	int res;
	
	sockets = MALLOC(sizeof(struct pollfd) * dsmcbe_net_remote_hosts);
		
	//Valgrind reports uninitialized data here, but apparently it is a problem in poll()
	memset(sockets, 0, sizeof(struct pollfd) * dsmcbe_net_remote_hosts);

	for(i = 0; i < dsmcbe_net_remote_hosts; i++)
	{
		sockets[i].fd = i == dsmcbe_host_number ? -1 : dsmcbe_net_remote_handles[i];
		sockets[i].events = i == dsmcbe_host_number ? 0 : POLLIN | POLLHUP | POLLERR;
		sockets[i].revents = 0;
	}
		
	while(!dsmcbe_net_do_terminate)
	{
		//We check each second for the termination event
		res = poll(sockets, dsmcbe_net_remote_hosts, 1000);
		if (res < 0) {
			REPORT_ERROR("Poll reported error");
		} else if (res == 0)
			continue;		
		
		for(i = 0; i < dsmcbe_net_remote_hosts; i++)
		{
			if (sockets[i].revents & POLLIN)
			{
				//For some reason this keeps happening when the socket is forced closed
				if (recv(sockets[i].fd, &res, 1, MSG_PEEK) == 0)
					sockets[i].revents = POLLHUP;
				
				dsmcbe_net_processPackage(dsmcbe_net_readPackage(sockets[i].fd), i);
			}

			if ((sockets[i].revents & POLLHUP) || (sockets[i].revents & POLLERR))
			{
				REPORT_ERROR("Socked closed unexpectedly");
				sockets[i].fd = -1;
				exit(0);
			}
		}
	}
		
	FREE(sockets);
	sockets = NULL;
	
	//Returning the unused argument removes a warning
	return data;
}

void* dsmcbe_net_Writer(void* data)
{
	size_t i;
	unsigned int hostno;
	void* package;
	
	while(!dsmcbe_net_do_terminate)
	{
		hostno = dsmcbe_net_remote_hosts + 1;
		package = NULL;
		pthread_mutex_lock(&dsmcbe_net_work_mutex);
		while(package == NULL && !dsmcbe_net_do_terminate)
		{
			for(i = 0; i < dsmcbe_net_remote_hosts; i++)
				if (!g_queue_is_empty(dsmcbe_net_GrequestQueues[i]))
				{
					package = g_queue_pop_head(dsmcbe_net_GrequestQueues[i]);
					hostno = i;
					break;
				}

			if (package == NULL && !dsmcbe_net_do_terminate)
			{
				pthread_cond_wait(&dsmcbe_net_work_ready, &dsmcbe_net_work_mutex);
			}
		}
		
		pthread_mutex_unlock(&dsmcbe_net_work_mutex);
		
		if (dsmcbe_net_do_terminate || package == NULL)
			continue;

		if (package != NULL)
		{
			dsmcbe_net_sendPackage(package, hostno);

			if (
				CAST_AS_PACKAGE(package)->packageCode == PACKAGE_ACQUIRE_RESPONSE ||
				CAST_AS_PACKAGE(package)->packageCode == PACKAGE_MIGRATION_RESPONSE ||
				CAST_AS_PACKAGE(package)->packageCode == PACKAGE_RELEASE_REQUEST
			)
			{
				unsigned int locked = TRUE;
				pthread_mutex_lock(&dsmcbe_net_work_mutex);

				unsigned int* v = g_hash_table_lookup(dsmcbe_net_GactiveTransfers, (void*)CAST_AS_PACKAGE(package)->dataItem);
				if (v == NULL) { REPORT_ERROR("response was not registered!"); }
				if (*v == 0) { REPORT_ERROR("response was not registered!"); }
				(*v)--;
				
				if (*v == 0)
				{  
					QueueableItem ui = g_hash_table_lookup(dsmcbe_net_GpendingInvalidates, (void*)CAST_AS_PACKAGE(package)->dataItem);
					if (ui != NULL)
					{
						g_hash_table_remove(dsmcbe_net_GpendingInvalidates, (void*)CAST_AS_PACKAGE(package)->dataItem);
						pthread_mutex_unlock(&dsmcbe_net_work_mutex);
						locked = FALSE;
						dsmcbe_net_RespondToInvalidate(ui);
					}
				}
				
				if (locked)
					pthread_mutex_unlock(&dsmcbe_net_work_mutex);
			}
			else if (CAST_AS_PACKAGE(package)->packageCode == PACKAGE_INVALIDATE_REQUEST)
			{
				pthread_mutex_lock(&dsmcbe_net_work_mutex);
				
				unsigned int* v = g_hash_table_lookup(dsmcbe_net_GactiveTransfers, (void*)CAST_AS_PACKAGE(package)->dataItem);
				if (v == NULL || *v == 0)
				{
					QueueableItem ui = g_hash_table_lookup(dsmcbe_net_GpendingInvalidates, (void*)CAST_AS_PACKAGE(package)->dataItem);
					if (ui != NULL)
					{
						g_hash_table_remove(dsmcbe_net_GpendingInvalidates, (void*)CAST_AS_PACKAGE(package)->dataItem);
						pthread_mutex_unlock(&dsmcbe_net_work_mutex);
						dsmcbe_net_RespondToInvalidate(ui);
					}
					else
					{
						pthread_mutex_unlock(&dsmcbe_net_work_mutex);
						dsmcbe_rc_EnqueInvalidateResponse(CAST_AS_PACKAGE(package)->dataItem, CAST_AS_PACKAGE(package)->requestID);
					}
						
				}
				else
					pthread_mutex_unlock(&dsmcbe_net_work_mutex);
			}

			FREE(package);
			package = NULL;
		}
	}
	
	//Returning the unused argument removes a warning
	return data;
}

void dsmcbe_net_processPackage(void* data, unsigned int machineId)
{
	
	QueueableItem ui;
	struct dsmcbe_net_QueueableItemWrapper* w;
	
	if (data == NULL)
		return;
	
	if (machineId > dsmcbe_net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
	
#ifdef TRACE_NETWORK_PACKAGES
		printf(WHERESTR "Received a package from machine: %d, type: %s (%d), reqId: %d, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)data)->packageCode), ((struct createRequest*)data)->packageCode, ((struct createRequest*)data)->requestID, ((struct createRequest*)data)->dataItem);
#endif

	//Here we re-map the request to make it look like it came from the original recipient
	if (CAST_AS_PACKAGE(data)->packageCode != PACKAGE_UPDATE)
	{
		machineId = CAST_AS_PACKAGE(data)->originalRecipient;
		CAST_AS_PACKAGE(data)->requestID = CAST_AS_PACKAGE(data)->originalRequestID;
	}
	
	switch(CAST_AS_PACKAGE(data)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_RELEASE_REQUEST:
		case PACKAGE_INVALIDATE_REQUEST:
		case PACKAGE_UPDATE:		
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:

			ui = dsmcbe_rc_new_QueueableItem(&dsmcbe_net_work_mutex, &dsmcbe_net_work_ready, &dsmcbe_net_GrequestQueues[machineId], data,  &dsmcbe_net_EnqueueCallback);
			dsmcbe_rc_EnqueItem(ui);
			break;
		
		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_INVALIDATE_RESPONSE:
		case PACKAGE_MIGRATION_RESPONSE:
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
		case PACKAGE_NACK:
		
			pthread_mutex_lock(&dsmcbe_net_work_mutex);
			
			if ((w = g_hash_table_lookup(dsmcbe_net_Gidlookups[machineId], (void*)CAST_AS_PACKAGE(data)->requestID)) == NULL)
			{
				REPORT_ERROR("Incoming response did not match an outgoing request");
				pthread_mutex_unlock(&dsmcbe_net_work_mutex);
				return;
			}

			g_hash_table_remove(dsmcbe_net_Gidlookups[machineId], (void*)CAST_AS_PACKAGE(data)->requestID);

			pthread_mutex_unlock(&dsmcbe_net_work_mutex);

			CAST_AS_PACKAGE(data)->requestID = w->origId;
			ui = w->ui;
			FREE(w);
			w = NULL;

			if (CAST_AS_PACKAGE(data)->packageCode == PACKAGE_ACQUIRE_RESPONSE || CAST_AS_PACKAGE(data)->packageCode == PACKAGE_MIGRATION_RESPONSE || CAST_AS_PACKAGE(data)->packageCode == PACKAGE_UPDATE)
			{
				ui = dsmcbe_rc_new_QueueableItem(NULL, NULL, NULL, data, NULL);

				//Forward this to the request coordinator, so it may record the data and propagate it
				dsmcbe_rc_EnqueItem(ui);
			}
			else if (CAST_AS_PACKAGE(data)->packageCode == PACKAGE_ACQUIRE_BARRIER_RESPONSE)
			{
				//We send the barrier response over the RequestCoordinator to avoid races
				if (ui->dataRequest != NULL)
					FREE(ui->dataRequest);
				ui->dataRequest = data;
				dsmcbe_rc_EnqueItem(ui);
			}
			else
			{
				//Other responses are sent directly to the receiver
				if (ui->mutex != NULL)
					pthread_mutex_lock(ui->mutex);
				if (ui->Gqueue != NULL)
					g_queue_push_tail(*(ui->Gqueue), data);
				if (ui->event != NULL)
					pthread_cond_signal(ui->event);
				if (ui->mutex != NULL)
					pthread_mutex_unlock(ui->mutex);
	
				FREE(ui);
				ui = NULL;
			}
			
			break;
		
		default:
			REPORT_ERROR("Invalid package type detected");
		 
	}
	
}

#define DATA_POINTER(type, p) \
		((void**) \
		(type == PACKAGE_ACQUIRE_RESPONSE ? &((struct dsmcbe_acquireResponse*)p)->data : \
		(type == PACKAGE_RELEASE_REQUEST ? &((struct dsmcbe_releaseRequest*)p)->data : \
		(type == PACKAGE_UPDATE ? &((struct dsmcbe_updateRequest*)p)->data : \
		(type == PACKAGE_MIGRATION_RESPONSE ? &((struct dsmcbe_migrationResponse*)p)->data : \
				NULL)))))

#define DATA_SIZE(type, p) \
		((unsigned int) \
		(type == PACKAGE_ACQUIRE_RESPONSE ? ((struct dsmcbe_acquireResponse*)p)->dataSize : \
		(type == PACKAGE_RELEASE_REQUEST ? ((struct dsmcbe_releaseRequest*)p)->dataSize : \
		(type == PACKAGE_UPDATE ? ((struct dsmcbe_updateRequest*)p)->dataSize : \
		(type == PACKAGE_MIGRATION_RESPONSE ? ((struct dsmcbe_migrationResponse*)p)->dataSize : \
				0)))))

void* dsmcbe_net_readPackage(int fd)
{
	unsigned int type;
	void* data;
	int blocksize;
	int transfersize;
	
	data = NULL;

	if (recv(fd, &type, sizeof(unsigned int), MSG_PEEK) != 0)
	{
		blocksize = PACKAGE_SIZE(type);
		data = MALLOC(blocksize);

		switch(type)
		{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_INVALIDATE_REQUEST:
		case PACKAGE_INVALIDATE_RESPONSE:
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
		case PACKAGE_NACK:
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire create package");
			break;

		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_RELEASE_REQUEST:
		case PACKAGE_UPDATE:
		case PACKAGE_MIGRATION_RESPONSE:
			blocksize -= sizeof(void*);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire response package"); 

			blocksize = DATA_SIZE(type, data);
			if (blocksize != 0)
			{
				transfersize = ALIGNED_SIZE(blocksize);
				
				*DATA_POINTER(type, data) = MALLOC_ALIGN(transfersize, 7);
				if (recv(fd, *DATA_POINTER(type, data), blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from acquire response");
			}
			else 
				*DATA_POINTER(type, data) = NULL;
			break;

		default:
			REPORT_ERROR("Invalid package code detected");
			break;
		
		}
	}

	return data;
}

void dsmcbe_net_sendPackage(void* package, unsigned int machineId)
{
	
	int fd;
	
	if (machineId > dsmcbe_net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
	
	if (package == NULL) 
	{ 
		REPORT_ERROR("NULL pointer");
		return; 
	}

#ifdef TRACE_NETWORK_PACKAGES
		printf(WHERESTR "Sending a package to machine: %d, type: %s (%d), reqId: %d, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)package)->packageCode), ((struct createRequest*)package)->packageCode, ((struct createRequest*)package)->requestID, ((struct createRequest*)package)->dataItem);
#endif

	//Preprocessing: re-route any migrated packages
	if (CAST_AS_PACKAGE(package)->packageCode == PACKAGE_ACQUIRE_RESPONSE || CAST_AS_PACKAGE(package)->packageCode == PACKAGE_NACK || CAST_AS_PACKAGE(package)->packageCode == PACKAGE_ACQUIRE_BARRIER_RESPONSE)
	{
		if (CAST_AS_PACKAGE(package)->originalRecipient == UINT_MAX)
			REPORT_ERROR("Originator is invalid!");

		machineId = CAST_AS_PACKAGE(package)->originator;
	}
	else if(CAST_AS_PACKAGE(package)->packageCode != PACKAGE_UPDATE && CAST_AS_PACKAGE(package)->originalRecipient == UINT_MAX)
	{
		CAST_AS_PACKAGE(package)->originalRecipient = machineId;
		CAST_AS_PACKAGE(package)->originalRequestID = CAST_AS_PACKAGE(package)->requestID;
	}	

	//We have mapped it back to ourselves :)
	if (machineId == dsmcbe_host_number)
	{
		printf(WHERESTR "Internal re-map to self\n", WHEREARG);
		dsmcbe_net_processPackage(package, dsmcbe_host_number);
		return;
	}
	
	fd = dsmcbe_net_remote_handles[machineId];
	int blocksize = PACKAGE_SIZE(CAST_AS_PACKAGE(package)->packageCode);
	unsigned int type = CAST_AS_PACKAGE(package)->packageCode;

	switch(type)
	{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_NACK:
		case PACKAGE_INVALIDATE_REQUEST:
		case PACKAGE_INVALIDATE_RESPONSE:
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			if (send(fd, package, blocksize, 0) != blocksize)
				REPORT_ERROR("Failed to send entire package");
			break;

		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_RELEASE_REQUEST:
		case PACKAGE_UPDATE:
		case PACKAGE_MIGRATION_RESPONSE:
			blocksize -= sizeof(void*);
			if (send(fd, package, blocksize, MSG_MORE) != blocksize)
				REPORT_ERROR("Failed to send entire package");

			if (*DATA_POINTER(type, package) == NULL) { REPORT_ERROR("NULL pointer"); }
			if ((unsigned int)send(fd, *DATA_POINTER(type, package), DATA_SIZE(type, package), 0) != DATA_SIZE(type, package))
				REPORT_ERROR("Failed to send entire data package");
			break;

		case PACKAGE_WRITEBUFFER_READY:
			//Never forward this one
			break;
		default:
			fprintf(stderr, WHERESTR "Invalid package type (%u) detected\n", WHEREARG, CAST_AS_PACKAGE(package)->packageCode);
	}
}

#undef DATA_POINTER
#undef DATA_SIZE

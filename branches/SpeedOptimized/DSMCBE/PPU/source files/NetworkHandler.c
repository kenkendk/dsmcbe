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

struct QueueableItemWrapper
{
	QueueableItem ui;
	unsigned int origId;
};

//There can be no more than this many pending requests
#define NET_MAX_SEQUENCE 1000000

//This is used to terminate the thread
volatile int net_terminate;

//This is a handle to the thread
pthread_t net_read_thread;
pthread_t net_write_thread;

//This is the mutex and condition that protects the work queues
pthread_mutex_t net_work_mutex;
pthread_cond_t net_work_ready;

//Each remote host has its own requestQueue
GQueue** Gnet_requestQueues;

//Keep track of which objects are in queue for the network
GHashTable* Gnet_activeTransfers;

//Keep a list of pending invalidates for each object
GHashTable* Gnet_pendingInvalidates;

//The number of hosts
unsigned int net_remote_hosts;
//An array of file descriptors for the remote hosts
int* net_remote_handles;

void* net_Reader(void* data);
void* net_Writer(void* data);
void net_sendPackage(void* package, unsigned int machineId);
void* net_readPackage(int fd);
void net_processPackage(void* data, unsigned int machineId);
OBJECT_TABLE_ENTRY_TYPE GetMachineID(GUID id);

//These tables are used to translate requestIDs from internal seqences to network
// sequence and back again
GHashTable** Gnet_idlookups;

//These are the sequence numbers assigned to the packages, a unique sequence for each host
unsigned int* net_sequenceNumbers;

inline unsigned int DSMCBE_MachineCount() { return net_remote_hosts; }

void NetUnsubscribe(GUID dataitem, unsigned int machineId)
{
	struct releaseRequest* req;

	if (net_remote_hosts == 0)
	{
		//printf(WHERESTR "Returning\n", WHEREARG);
		return;
	}
	
	if (dataitem == 0)
	{
		REPORT_ERROR("bad request");
		return;
	}

	if (machineId > net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}


	req = MALLOC(sizeof(struct releaseRequest));
		
	req->packageCode = PACKAGE_RELEASE_REQUEST;
	req->requestID = 0; 
	req->mode = ACQUIRE_MODE_READ;
	req->data = NULL;
	req->dataSize = 0;
	req->offset = 0;
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&net_work_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	g_queue_push_tail(Gnet_requestQueues[machineId], req);
	pthread_cond_signal(&net_work_ready);
	pthread_mutex_unlock(&net_work_mutex);
	
}

void NetRespondToInvalidate(struct QueueableItemStruct* ui)
{
	EnqueInvalidateResponse(((struct invalidateRequest*)ui->dataRequest)->requestID);
	free(ui->dataRequest);
	ui->dataRequest = NULL;
	free(ui);
	ui = NULL;	
}

void NetEnqueueCallback(void* data)
{
	if (net_remote_hosts == 0)
		return;
		
	//Mark the object as active
	if (
		((struct createRequest*)(data))->packageCode == PACKAGE_ACQUIRE_RESPONSE || 
		((struct createRequest*)(data))->packageCode == PACKAGE_MIGRATION_RESPONSE ||
		((struct createRequest*)(data))->packageCode == PACKAGE_RELEASE_REQUEST
	)
	{
		//printf(WHERESTR "Sending package with type: %d, for id: %d.\n", WHEREARG, ((struct createRequest*)data)->packageCode, ((struct invalidateRequest*)data)->dataItem);
		unsigned int* v = g_hash_table_lookup(Gnet_activeTransfers, (void*)((struct createRequest*)(data))->dataItem);
		
		if (v == NULL)
		{
			v = MALLOC(sizeof(unsigned int));
			*v = 0;
			g_hash_table_insert(Gnet_activeTransfers, (void*)((struct createRequest*)(data))->dataItem, v);
		}

		(*v)++;
	}
}

void NetRequest(QueueableItem item, unsigned int machineId)
{
		
	unsigned int nextId;
	struct QueueableItemWrapper* w;
	int packagesize;
	
	void* datacopy;
		
	//printf(WHERESTR "Received a netrequest, target machine: %d\n", WHEREARG, machineId);
	
	if (net_remote_hosts == 0)
	{
		printf(WHERESTR "Returning\n", WHEREARG);
		return;
	}
	
	if (item == NULL || item->dataRequest == NULL)
	{
		REPORT_ERROR("bad request");
		return;
	}

	if (((struct createRequest*)(item->dataRequest))->packageCode == PACKAGE_INVALIDATE_REQUEST)
	{
		if (net_remote_hosts == 0)
		{
			NetRespondToInvalidate(item);
			return;
		}
		

		pthread_mutex_lock(&net_work_mutex);
		
		unsigned int* v = g_hash_table_lookup(Gnet_activeTransfers, (void*)((struct invalidateRequest*)(item->dataRequest))->dataItem);
		if (v == NULL || *v == 0)
		{
			pthread_mutex_unlock(&net_work_mutex);
			NetRespondToInvalidate(item);
			return;
		}
		else
		{
			//REPORT_ERROR("Delaying invalidate!");
			if (g_hash_table_lookup(Gnet_pendingInvalidates, (void*)((struct invalidateRequest*)(item->dataRequest))->dataItem) != NULL)
			{
				REPORT_ERROR("Found pending invalidates in table already!");
			}
			else 
			{
				g_hash_table_insert(Gnet_pendingInvalidates, (void*)((struct invalidateRequest*)(item->dataRequest))->dataItem, item);
			}
				
			pthread_mutex_unlock(&net_work_mutex);
			return;
		}
	}

	if (machineId > net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
			
	w = MALLOC(sizeof(struct QueueableItemWrapper));
	if (w == NULL)
		return;

	packagesize = PACKAGE_SIZE(((struct createRequest*)(item->dataRequest))->packageCode);
	
	/*switch (((struct createRequest*)(item->dataRequest))->packageCode) {
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			packagesize = sizeof(struct acquireBarrierRequest); 	
			break;
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			packagesize = sizeof(struct acquireBarrierResponse); 	
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			packagesize = sizeof(struct acquireRequest); 	
			break;			
		case PACKAGE_ACQUIRE_RESPONSE:
			packagesize = sizeof(struct acquireResponse); 	
			break;			
		case PACKAGE_CREATE_REQUEST:
			packagesize = sizeof(struct createRequest); 	
			break;			
		case PACKAGE_INVALIDATE_REQUEST:
			packagesize = sizeof(struct invalidateRequest); 	
			break;			
		case PACKAGE_INVALIDATE_RESPONSE:
			packagesize = sizeof(struct invalidateResponse); 	
			break;			
		case PACKAGE_MIGRATION_RESPONSE:
			packagesize = sizeof(struct migrationResponse); 	
			break;			
		case PACKAGE_NACK:
			packagesize = sizeof(struct NACK); 	
			break;			
		case PACKAGE_RELEASE_REQUEST:
			packagesize = sizeof(struct releaseRequest); 	
			break;			
		case PACKAGE_RELEASE_RESPONSE:
			packagesize = sizeof(struct releaseResponse); 	
			break;			
		case PACKAGE_WRITEBUFFER_READY:
			packagesize = sizeof(struct writebufferReady); 	
			break;			
		default:
			packagesize = 0;
			break;
	}*/
	
	//if (temp != packagesize)
	//{
		//fprintf(stderr, "Packagecode %u temp %i vs. packagesize %i", ((struct createRequest*)(item->dataRequest))->packageCode, temp, packagesize);
	//}

	datacopy = MALLOC(packagesize);
	if (datacopy == NULL)
		return;
		
	void* ix = item;
	ix = item->dataRequest;
	ix = datacopy;
	ix = (void*)((struct createRequest*)(item->dataRequest))->requestID;
	
	memcpy(datacopy, item->dataRequest, packagesize);
	
	w->ui = item;
	w->origId = ((struct createRequest*)(item->dataRequest))->requestID;
	
	//printf(WHERESTR "Locking\n", WHEREARG);
	pthread_mutex_lock(&net_work_mutex);
	//printf(WHERESTR "Locked\n", WHEREARG);
	
	nextId = NEXT_SEQ_NO(net_sequenceNumbers[machineId], NET_MAX_SEQUENCE);
	((struct createRequest*)(datacopy))->requestID = nextId;
	
	//printf(WHERESTR "Received a netrequest, target machine: %d, %d, %d, %d, %d, %d, %d, MALLOC: %d\n", WHEREARG, machineId, net_idlookups[machineId], net_idlookups, w, nextId, net_idlookups[machineId]->fill, net_idlookups[machineId]->count, (int)malloc);

	NetEnqueueCallback(datacopy);
	
	//printf(WHERESTR "Mapping request id %d to %d\n", WHEREARG, w->origId, nextId);
	if (g_hash_table_lookup(Gnet_idlookups[machineId], (void*)nextId) == NULL)
		g_hash_table_insert(Gnet_idlookups[machineId], (void*)nextId, w);
	else
		REPORT_ERROR("Could not insert into net_idlookups")
	
	//printf(WHERESTR "Received a netrequest, target machine: %d\n", WHEREARG, machineId);
	g_queue_push_tail(Gnet_requestQueues[machineId], datacopy);
	
	//printf(WHERESTR "Signaling\n", WHEREARG);
	pthread_cond_signal(&net_work_ready);
	//printf(WHERESTR "Unlocking\n", WHEREARG);
	pthread_mutex_unlock(&net_work_mutex);
	//printf(WHERESTR "Unlocked\n", WHEREARG);

	//printf(WHERESTR "Netrequest inserted for target machine: %d\n", WHEREARG, machineId);
}

void InitializeNetworkHandler(int* remote_handles, unsigned int remote_hosts)
{
	
	size_t i;
	pthread_attr_t attr;
	net_terminate = 0;

	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, remote_hosts);
	net_remote_hosts = remote_hosts;
	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, net_remote_hosts);
	net_remote_handles = remote_handles;
	
	if (net_remote_hosts == 0)
	{
		//printf(WHERESTR "Cannot initialize network, no remote hosts found!\n", WHEREARG);
		return;
	}
	pthread_mutex_init(&net_work_mutex, NULL);
	pthread_cond_init (&net_work_ready, NULL);
		
	Gnet_requestQueues = MALLOC(sizeof(GQueue*) * net_remote_hosts);
	Gnet_idlookups = MALLOC(sizeof(GHashTable*) * net_remote_hosts);
	net_sequenceNumbers = MALLOC(sizeof(unsigned int) * net_remote_hosts);

	for(i = 0; i < net_remote_hosts; i++)
	{
		Gnet_requestQueues[i] = g_queue_new();
		Gnet_idlookups[i] = g_hash_table_new(NULL, NULL);
		net_sequenceNumbers[i] = 0;
	}

	Gnet_activeTransfers = g_hash_table_new(NULL, NULL);
	Gnet_pendingInvalidates = g_hash_table_new(NULL, NULL);
	
	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	pthread_create(&net_read_thread, &attr, net_Reader, NULL);
	pthread_create(&net_write_thread, &attr, net_Writer, NULL);

	pthread_attr_destroy(&attr);
	
	for(i = 0; i < net_remote_hosts; i++)
		RegisterInvalidateSubscriber(&net_work_mutex, &net_work_ready, &Gnet_requestQueues[i], i);
}

void NetUpdate(GUID id, unsigned int offset, unsigned int dataSize, void* data)
{
	//printf(WHERESTR "NetUpdating GUID %i\n", WHEREARG, id);
	
	struct updateRequest* req;
	size_t i;
	
	if (net_remote_hosts == 0)
		return;
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&net_work_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	//printf(WHERESTR "Taking lock: %d\n", WHEREARG, (int)&net_work_mutex);
	for(i = 0; i < net_remote_hosts; i++)
	{
		if (i != dsmcbe_host_number)
		{
			//printf(WHERESTR "Processing update, target machine: %d, GUID: %d\n", WHEREARG, i, id);
			req = MALLOC(sizeof(struct updateRequest));

			req->dataItem = id;
			req->packageCode = PACKAGE_UPDATE;
			req->requestID = 0;
			req->offset = offset;
			req->dataSize = dataSize;
			req->data = data;
			g_queue_push_tail(Gnet_requestQueues[i], req);
		}
	}
	
	pthread_cond_signal(&net_work_ready);

	//printf(WHERESTR "Releasing lock: %d\n", WHEREARG, (int)&net_work_mutex);
	pthread_mutex_unlock(&net_work_mutex);
	//printf(WHERESTR "Processed updates\n", WHEREARG);
}

void TerminateNetworkHandler(int force)
{
	
	size_t i;

	net_terminate = force ? 1 : 1;

	if (net_remote_hosts == 0)
		return;
	
	//printf(WHERESTR "locking mutex\n", WHEREARG);
	pthread_mutex_lock(&net_work_mutex);
	//printf(WHERESTR "locked mutex\n", WHEREARG);
	pthread_cond_signal(&net_work_ready);
	pthread_mutex_unlock(&net_work_mutex);
	
	pthread_join(net_read_thread, NULL);
	pthread_join(net_write_thread, NULL);

	for(i = 0; i < net_remote_hosts; i++)
		UnregisterInvalidateSubscriber(&Gnet_requestQueues[i]);

	for(i = 0; i < net_remote_hosts; i++)
	{
		g_queue_free(Gnet_requestQueues[i]);
		Gnet_requestQueues[i] = NULL;
		g_hash_table_destroy(Gnet_idlookups[i]);
		Gnet_idlookups[i] = NULL;
	}
	
	FREE(Gnet_requestQueues);
	Gnet_requestQueues = NULL;
	FREE(Gnet_idlookups);
	Gnet_idlookups = NULL;
	FREE(net_sequenceNumbers);
	net_sequenceNumbers = NULL;
	
	pthread_mutex_destroy(&net_work_mutex);
	pthread_cond_destroy(&net_work_ready);
}

void* net_Reader(void* data)
{
	
	struct pollfd* sockets;
	size_t i;
	int res;
	
	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, net_remote_hosts);
	
	sockets = MALLOC(sizeof(struct pollfd) * net_remote_hosts);
		
	//Valgrind reports uninitialized data here, but apparently it is a problem in poll()
	memset(sockets, 0, sizeof(struct pollfd) * net_remote_hosts);

	for(i = 0; i < net_remote_hosts; i++)
	{
		sockets[i].fd = i == dsmcbe_host_number ? -1 : net_remote_handles[i];
		sockets[i].events = i == dsmcbe_host_number ? 0 : POLLIN | POLLHUP | POLLERR;
		sockets[i].revents = 0;
	}
		
	while(!net_terminate)
	{
		//We check each second for the termination event
		res = poll(sockets, net_remote_hosts, 1000);
		if (res < 0) {
			REPORT_ERROR("Poll reported error");
		} else if (res == 0)
			continue;		
		
		//printf(WHERESTR "Network packaged received\n", WHEREARG);
		
		for(i = 0; i < net_remote_hosts; i++)
		{
			if (sockets[i].revents & POLLIN)
			{
				//For some reason this keeps happening when the socket is forced closed
				if (recv(sockets[i].fd, &res, 1, MSG_PEEK) == 0)
					sockets[i].revents = POLLHUP;
				
				//printf(WHERESTR "Processing network package from %d, revents: %d\n", WHEREARG, i, sockets[i].revents);
				net_processPackage(net_readPackage(sockets[i].fd), i);
				//printf(WHERESTR "Processed network package from: %d\n", WHEREARG, i);
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

void* net_Writer(void* data)
{
	
	size_t i;
	unsigned int hostno;
	void* package;
	
	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, net_remote_hosts);
	
	while(!net_terminate)
	{
		//printf(WHERESTR "Network packaged ready to send\n", WHEREARG);
		hostno = net_remote_hosts + 1;
		package = NULL;
		//printf(WHERESTR "Locking\n", WHEREARG);
		pthread_mutex_lock(&net_work_mutex);
		//printf(WHERESTR "Locked\n", WHEREARG);
		while(package == NULL && !net_terminate)
		{
			for(i = 0; i < net_remote_hosts; i++)
				if (!g_queue_is_empty(Gnet_requestQueues[i]))
				{
					//printf(WHERESTR "Queue is NOT empty\n", WHEREARG);
					package = g_queue_pop_head(Gnet_requestQueues[i]);
					hostno = i;
					//printf(WHERESTR "Hostno is %i\n", WHEREARG, hostno);
					break;
				}
				else
				{
					//printf(WHERESTR "Queue is empty\n", WHEREARG);
				}
			if (package == NULL && !net_terminate) 
			{
				//printf(WHERESTR "Waiting\n", WHEREARG);
				pthread_cond_wait(&net_work_ready, &net_work_mutex);
				//printf(WHERESTR "Received signal\n", WHEREARG);
			}
		}
		
		//printf(WHERESTR "Unlocking\n", WHEREARG);
		pthread_mutex_unlock(&net_work_mutex);
		//printf(WHERESTR "Unlocked\n", WHEREARG);
		
		if (net_terminate || package == NULL)
			continue;

		if (package != NULL)
		{
			net_sendPackage(package, hostno);

			if (
				((struct createRequest*)package)->packageCode == PACKAGE_ACQUIRE_RESPONSE || 
				((struct createRequest*)package)->packageCode == PACKAGE_MIGRATION_RESPONSE ||
				((struct createRequest*)package)->packageCode == PACKAGE_RELEASE_REQUEST
			)
			{
				//printf(WHERESTR "Sending package with type: %d, to %d for id: %d.\n", WHEREARG, ((struct createRequest*)package)->packageCode, 	hostno, ((struct invalidateRequest*)package)->dataItem);
				
				unsigned int locked = TRUE;
				pthread_mutex_lock(&net_work_mutex);

				unsigned int* v = g_hash_table_lookup(Gnet_activeTransfers, (void*)((struct acquireResponse*)(package))->dataItem);
				if (v == NULL) { REPORT_ERROR("response was not registered!"); }
				if (*v == 0) { REPORT_ERROR("response was not registered!"); }
				(*v)--;
				
				if (*v == 0)
				{  
					struct QueueableItemStruct* ui = g_hash_table_lookup(Gnet_pendingInvalidates, (void*)((struct acquireResponse*)(package))->dataItem);
					if (ui != NULL)
					{
						g_hash_table_remove(Gnet_pendingInvalidates, (void*)((struct acquireResponse*)(package))->dataItem);
						pthread_mutex_unlock(&net_work_mutex);
						locked = FALSE;
						NetRespondToInvalidate(ui);
					}
				}
				
				if (locked)
					pthread_mutex_unlock(&net_work_mutex);
			}
			else if (((struct invalidateRequest*)package)->packageCode == PACKAGE_INVALIDATE_REQUEST)
			{
				pthread_mutex_lock(&net_work_mutex);
				
				unsigned int* v = g_hash_table_lookup(Gnet_activeTransfers, (void*)((struct invalidateRequest*)package)->dataItem);
				if (v == NULL || *v == 0)
				{
					struct QueueableItemStruct* ui = g_hash_table_lookup(Gnet_pendingInvalidates, (void*)((struct invalidateRequest*)(package))->dataItem);
					if (ui != NULL)
					{
						g_hash_table_remove(Gnet_pendingInvalidates, (void*)((struct invalidateRequest*)(package))->dataItem);
						pthread_mutex_unlock(&net_work_mutex);
						NetRespondToInvalidate(ui);
					}
					else
					{
						pthread_mutex_unlock(&net_work_mutex);
						EnqueInvalidateResponse(((struct invalidateRequest*)package)->requestID);
					}
						
				}
				else
					pthread_mutex_unlock(&net_work_mutex);
			}

			FREE(package);
			package = NULL;
		}
	}
	
	//Returning the unused argument removes a warning
	return data;
}

void net_processPackage(void* data, unsigned int machineId)
{
	
	QueueableItem ui;
	struct QueueableItemWrapper* w;
	
	if (data == NULL)
		return;
	
	if (machineId > net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
	
#ifdef TRACE_NETWORK_PACKAGES
		printf(WHERESTR "Received a package from machine: %d, type: %s (%d), reqId: %d, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)data)->packageCode), ((struct createRequest*)data)->packageCode, ((struct createRequest*)data)->requestID, ((struct createRequest*)data)->dataItem);
#endif

	//Here we re-map the request to make it look like it came from the original recipient
	switch(((struct createRequest*)data)->packageCode)
	{
		case PACKAGE_ACQUIRE_RESPONSE:
			machineId = ((struct acquireResponse*)data)->originalRecipient;
			((struct acquireResponse*)data)->requestID = ((struct acquireResponse*)data)->originalRequestID;
			break;
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			machineId = ((struct acquireBarrierResponse*)data)->originalRecipient;
			((struct acquireBarrierResponse*)data)->requestID = ((struct acquireBarrierResponse*)data)->originalRequestID;
			break;
		case PACKAGE_NACK:
			machineId = ((struct NACK*)data)->originalRecipient;
			((struct NACK*)data)->requestID = ((struct NACK*)data)->originalRequestID;
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			machineId = ((struct migrationResponse*)data)->originalRecipient;
			((struct migrationResponse*)data)->requestID = ((struct migrationResponse*)data)->originalRequestID;
			break;
	}
	
	switch(((struct createRequest*)data)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_RELEASE_REQUEST:
		case PACKAGE_INVALIDATE_REQUEST:
		case PACKAGE_UPDATE:		
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:

			//printf(WHERESTR "Processing network package from %d, with type: %s, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)data)->packageCode), ((struct acquireRequest*)data)->dataItem);
		
			ui = MALLOC(sizeof(struct QueueableItemStruct));

			ui->dataRequest = data;
			ui->Gqueue = &Gnet_requestQueues[machineId];
			ui->mutex = &net_work_mutex;
			ui->event = &net_work_ready;
			ui->callback = &NetEnqueueCallback;
			
			//printf(WHERESTR "Enqueued %d with callback %d\n", WHEREARG, (unsigned int)ui ,(int)ui->callback);
			EnqueItem(ui);
			break;
		
		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_INVALIDATE_RESPONSE:
		case PACKAGE_MIGRATION_RESPONSE:
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
		//case PACKAGE_NACK:
		
			//printf(WHERESTR "locking mutex\n", WHEREARG);
			pthread_mutex_lock(&net_work_mutex);
			//printf(WHERESTR "locked mutex\n", WHEREARG);
			
			if ((w = g_hash_table_lookup(Gnet_idlookups[machineId], (void*)((struct createRequest*)data)->requestID)) == NULL)
			{
				REPORT_ERROR("Incoming response did not match an outgoing request");
				pthread_mutex_unlock(&net_work_mutex);
				return;
			}

			g_hash_table_remove(Gnet_idlookups[machineId], (void*)((struct createRequest*)data)->requestID);

			pthread_mutex_unlock(&net_work_mutex);

			//printf(WHERESTR "Altering request id from %d to %d\n", WHEREARG, ((struct createRequest*)data)->requestID, w->origId);
			((struct createRequest*)data)->requestID = w->origId;
			ui = w->ui;
			FREE(w);
			w = NULL;

			if (((struct createRequest*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE || ((struct createRequest*)data)->packageCode == PACKAGE_MIGRATION_RESPONSE || ((struct createRequest*)data)->packageCode == PACKAGE_UPDATE)
			{
				//printf(WHERESTR "Acquire response package from %d, for guid: %d, reqId: %d\n", WHEREARG, machineId, ((struct acquireResponse*)data)->dataItem, ((struct acquireResponse*)data)->requestID);
				ui = MALLOC(sizeof(struct QueueableItemStruct));

				ui->event = NULL;
				ui->mutex = NULL;
				ui->Gqueue = NULL;
				ui->callback = NULL;
				ui->dataRequest = data;

				//Forward this to the request coordinator, so it may record the data and propagate it
				//printf(WHERESTR "Enqueue item :%d\n", WHEREARG, (unsigned int)ui);
				EnqueItem(ui);
			}
			else if (((struct createRequest*)data)->packageCode == PACKAGE_ACQUIRE_BARRIER_RESPONSE)
			{
				//printf(WHERESTR "Inserting barrier response\n", WHEREARG);
				//We send the barrier response over the RequestCoordinator to avoid races
				if (ui->dataRequest != NULL)
					FREE(ui->dataRequest);
				ui->dataRequest = data;
				EnqueItem(ui);
			}
			else
			{
				//printf(WHERESTR "Forwarding response directly\n", WHEREARG);
				
				//Other responses are sent directly to the receiver
				if (ui->mutex != NULL)
				{
					//printf(WHERESTR "locking mutex\n", WHEREARG);
					pthread_mutex_lock(ui->mutex);
					//printf(WHERESTR "locked mutex\n", WHEREARG);
				}
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
			//fprintf(stderr, WHERESTR "Package code: %d, reqId: %d\n", WHEREARG, ((struct createRequest*)data)->packageCode, ((struct createRequest*)data)->requestID);
			REPORT_ERROR("Invalid package type detected");
		 
	}
	
}

void* net_readPackage(int fd)
{
	unsigned int type;
	void* data;
	int blocksize;
	int transfersize;
	
	data = NULL;

	if (recv(fd, &type, sizeof(unsigned int), MSG_PEEK) != 0)
	{
		//printf(WHERESTR "Reading network package, type: %d\n", WHEREARG, type);
		switch(type)
		{
		case PACKAGE_CREATE_REQUEST:
			blocksize = sizeof(struct createRequest);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire create package"); 
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			blocksize = sizeof(struct acquireRequest);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire read package"); 
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			blocksize = sizeof(struct acquireRequest);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire write package"); 
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			blocksize = sizeof(struct acquireResponse);
			data = MALLOC(blocksize);
			blocksize -= sizeof(void*);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire response package"); 

			blocksize = ((struct acquireResponse*)data)->dataSize;
			if (blocksize != 0)
			{
				transfersize = ALIGNED_SIZE(blocksize);
				
				//printf(WHERESTR "Blocksize: %d, Transfersize: %d\n", WHEREARG, blocksize, transfersize);
				
				((struct acquireResponse*)data)->data = MALLOC_ALIGN(transfersize, 7);
				if (recv(fd, ((struct acquireResponse*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from acquire response");
			}
			else 
				((struct acquireResponse*)data)->data = NULL;			
			break;
		case PACKAGE_RELEASE_REQUEST:
			blocksize = sizeof(struct releaseRequest);
			data = MALLOC(blocksize);
			blocksize -= sizeof(unsigned long);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire release request package"); 

			blocksize = ((struct releaseRequest*)data)->dataSize;
			if (blocksize != 0)
			{
				transfersize = ALIGNED_SIZE(blocksize);
				((struct releaseRequest*)data)->data = MALLOC_ALIGN(transfersize, 7);
				if (recv(fd, ((struct releaseRequest*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from release request");
			} else {
				((struct releaseRequest*)data)->data = NULL;
			}
			break;
		case PACKAGE_INVALIDATE_REQUEST:
			blocksize = sizeof(struct invalidateRequest);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire invalidate request package"); 
			//printf(WHERESTR "Read network package, type: %d\n", WHEREARG, type);
			break;
		case PACKAGE_INVALIDATE_RESPONSE:
			blocksize = sizeof(struct invalidateResponse);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire invalidate response package"); 
			break;
		case PACKAGE_UPDATE:
			blocksize = sizeof(struct updateRequest);
			data = MALLOC(blocksize);
			blocksize -= sizeof(unsigned long);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire release request package"); 

			blocksize = ((struct updateRequest*)data)->dataSize;
			if (blocksize != 0)
			{
				transfersize = ALIGNED_SIZE(blocksize);
				((struct updateRequest*)data)->data = MALLOC_ALIGN(transfersize, 7);
				if (recv(fd, ((struct updateRequest*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from release request");
			} else {
				((struct updateRequest*)data)->data = NULL;
			}
			break;
		case PACKAGE_NACK:
			blocksize = sizeof(struct NACK);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire nack package"); 
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			blocksize = sizeof(struct migrationResponse);
			data = MALLOC(blocksize);
			
			blocksize -= (sizeof(unsigned long));
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire migration package"); 

			blocksize = ((struct migrationResponse*)data)->dataSize;
			if (blocksize != 0)
			{
				transfersize = ALIGNED_SIZE(blocksize);
				((struct migrationResponse*)data)->data = MALLOC_ALIGN(transfersize, 7);
				if (recv(fd, ((struct migrationResponse*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from migration response");
			}
			else
				((struct migrationResponse*)data)->data = NULL;
				
		
			break;
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			blocksize = sizeof(struct acquireBarrierRequest);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire barrier package"); 
			//printf(WHERESTR "Read barrier request GUID %i\n", WHEREARG, ((struct acquireBarrierRequest*)data)->dataItem);
			break;

		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			blocksize = sizeof(struct acquireBarrierResponse);
			data = MALLOC(blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire barrier package"); 
			//printf(WHERESTR "Read barrier response req %i\n", WHEREARG, ((struct acquireBarrierResponse*)data)->requestID);
			break;
			
		default:
			REPORT_ERROR("Invalid package code detected");
			break;
		
		}
	}

	return data;
}

void net_sendPackage(void* package, unsigned int machineId)
{
	
	int fd;
	
	if (machineId > net_remote_hosts)
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
	switch(((struct createRequest*)package)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			if (((struct createRequest*)package)->originalRecipient == UINT_MAX)
			{
				((struct createRequest*)package)->originalRecipient = machineId;
				((struct createRequest*)package)->originalRequestID = ((struct createRequest*)package)->requestID;
			}
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			if (((struct acquireRequest*)package)->originalRecipient == UINT_MAX)
			{
				((struct acquireRequest*)package)->originalRecipient = machineId;
				((struct acquireRequest*)package)->originalRequestID = ((struct acquireRequest*)package)->requestID;
			}
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			if (((struct acquireRequest*)package)->originalRecipient == UINT_MAX)
			{
				((struct acquireRequest*)package)->originalRecipient = machineId;
				((struct acquireRequest*)package)->originalRequestID = ((struct acquireRequest*)package)->requestID;
			}
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			if (((struct acquireResponse*)package)->originator == UINT_MAX)
				REPORT_ERROR("Originator is invalid!");

			machineId = ((struct acquireResponse*)package)->originator;
			break;
		case PACKAGE_RELEASE_REQUEST:
			break;
		case PACKAGE_INVALIDATE_REQUEST:			
			break;
		case PACKAGE_INVALIDATE_RESPONSE:		
			break;
		case PACKAGE_UPDATE:
			break;		
		case PACKAGE_NACK:
			if (((struct NACK*)package)->originator == UINT_MAX)
				REPORT_ERROR("Originator is invalid!");
			
			machineId = ((struct acquireBarrierResponse*)package)->originator;
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			break;
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			if (((struct acquireBarrierRequest*)package)->originalRecipient == UINT_MAX)
			{
				((struct acquireBarrierRequest*)package)->originalRecipient = machineId;
				((struct acquireBarrierRequest*)package)->originalRequestID = ((struct acquireBarrierRequest*)package)->requestID;
			}
			break;
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			if (((struct acquireBarrierResponse*)package)->originator == UINT_MAX)
				REPORT_ERROR("Originator is invalid!");

			machineId =((struct acquireBarrierResponse*)package)->originator;
			break;
		case PACKAGE_WRITEBUFFER_READY:
			break;
		default:
			fprintf(stderr, WHERESTR "Invalid package type (%u) detected\n", WHEREARG, ((struct createRequest*)package)->packageCode);
	}	

	//We have mapped it back to ourselves :)
	if (machineId == dsmcbe_host_number)
	{
		printf(WHERESTR "Internal re-map to self\n", WHEREARG);
		net_processPackage(package, dsmcbe_host_number);
		return;
	}
	
	fd = net_remote_handles[machineId];

	switch(((struct createRequest*)package)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			if (send(fd, package, sizeof(struct createRequest), 0) != sizeof(struct createRequest))
				REPORT_ERROR("Failed to send entire create request package");			 
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			if (send(fd, package, sizeof(struct acquireRequest), 0) != sizeof(struct acquireRequest))
				REPORT_ERROR("Failed to send entire acquire request package");
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			if (send(fd, package, sizeof(struct acquireRequest), 0) != sizeof(struct acquireRequest))
				REPORT_ERROR("Failed to send entire acquire request write package");			 
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			if (send(fd, package, sizeof(struct acquireResponse) - sizeof(void*), MSG_MORE) != sizeof(struct acquireResponse) - sizeof(void*))
				REPORT_ERROR("Failed to send entire acquire response package");
			if (((struct acquireResponse*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			if ((unsigned int)send(fd, ((struct acquireResponse*)package)->data, ((struct acquireResponse*)package)->dataSize, 0) != ((struct acquireResponse*)package)->dataSize)
				REPORT_ERROR("Failed to send entire acquire response data package");
			break;
		case PACKAGE_RELEASE_REQUEST:
			if (send(fd, package, sizeof(struct releaseRequest) - sizeof(void*), MSG_MORE) != sizeof(struct releaseRequest) - sizeof(void*))
				REPORT_ERROR("Failed to send entire release request package");
			if (((struct releaseRequest*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			if ((unsigned int)send(fd, ((struct releaseRequest*)package)->data, ((struct releaseRequest*)package)->dataSize, 0) != ((struct releaseRequest*)package)->dataSize)
				REPORT_ERROR("Failed to send entire release request data package");				  
			break;
		case PACKAGE_INVALIDATE_REQUEST:			
			//printf(WHERESTR "NetInvalidating GUID %i\n", WHEREARG, ((struct invalidateRequest*)package)->dataItem);			
			if (send(fd, package, sizeof(struct invalidateRequest), 0) != sizeof(struct invalidateRequest))
				REPORT_ERROR("Failed to send entire invalidate request package");
			break;
		case PACKAGE_INVALIDATE_RESPONSE:		
			if (send(fd, package, sizeof(struct invalidateResponse), 0) != sizeof(struct invalidateResponse))
				REPORT_ERROR("Failed to send entire invalidate response package");
			break;
		case PACKAGE_UPDATE:
			//printf(WHERESTR "NetUpdating GUID %i\n", WHEREARG, ((struct updateRequest*)package)->dataItem);
			if (send(fd, package, sizeof(struct updateRequest) - sizeof(void*), MSG_MORE) != sizeof(struct updateRequest) - sizeof(void*))
				REPORT_ERROR("Failed to send entire update request package");
			if (((struct updateRequest*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			if ((unsigned int)send(fd, ((struct updateRequest*)package)->data, ((struct updateRequest*)package)->dataSize, 0) != ((struct updateRequest*)package)->dataSize)
				REPORT_ERROR("Failed to send entire update request data package");
			break;		
		case PACKAGE_NACK:
			if (send(fd, package, sizeof(struct NACK), 0) != sizeof(struct NACK))
				REPORT_ERROR("Failed to send entire nack package");				
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			send(fd, package, sizeof(struct migrationResponse) - (sizeof(unsigned long)), MSG_MORE);
			if (((struct migrationResponse*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			send(fd, ((struct migrationResponse*)package)->data, ((struct migrationResponse*)package)->dataSize, 0); 
			break;
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			//printf(WHERESTR "Sending Barrier Request GUID %i\n", WHEREARG, ((struct acquireBarrierRequest*)package)->dataItem);
			if (send(fd, package, sizeof(struct acquireBarrierRequest), 0) != sizeof(struct acquireBarrierRequest))
				REPORT_ERROR("Failed to send entire acquire barrier request package");				
			break;
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			//printf(WHERESTR "Sending Barrier response req: %i\n", WHEREARG, ((struct acquireBarrierResponse*)package)->requestID);
			if (send(fd, package, sizeof(struct acquireBarrierResponse), 0) != sizeof(struct acquireBarrierResponse))
				REPORT_ERROR("Failed to send entire acquire barrier response package");				
			break;
		case PACKAGE_WRITEBUFFER_READY:
			break;
		default:
			fprintf(stderr, WHERESTR "Invalid package type (%u) detected\n", WHEREARG, ((struct createRequest*)package)->packageCode);
	}
}

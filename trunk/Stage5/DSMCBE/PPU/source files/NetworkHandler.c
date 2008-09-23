#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <poll.h>
#include <stropts.h>

//#define TRACE_NETWORK_PACKAGES

#include "../header files/RequestCoordinator.h"
#include "../../dsmcbe.h"
#include "../../common/debug.h"
#include "../header files/NetworkHandler.h"

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

//This mutex protects the lease table and write initiator
pthread_mutex_t net_leaseLock;

//Each remote host has its own requestQueue
GQueue** Gnet_requestQueues;

//Keep track of which hosts have what data, this minimizes the amount of invalidate messages
GHashTable* Gnet_leaseTable;
GHashTable* Gnet_writeInitiator;

//The number of hosts
static unsigned int net_remote_hosts;
//An array of file descriptors for the remote hosts
int* net_remote_handles;

void* net_Reader(void* data);
void* net_Writer(void* data);
void net_sendPackage(void* package, unsigned int machineId);
void* net_readPackage(int fd);
void net_processPackage(void* data, unsigned int machineId);
unsigned int GetMachineID(GUID id);

//These tables are used to translate requestIDs from internal seqences to network
// sequence and back again
GHashTable** Gnet_idlookups;

//These are the sequence numbers assigned to the packages, a unique sequence for each host
unsigned int* net_sequenceNumbers;

void NetUnsubscribe(GUID dataitem, unsigned int machineId)
{
	struct releaseRequest* req;

	if (net_remote_hosts == 0)
	{
		printf(WHERESTR "Returning\n", WHEREARG);
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


	if ((req = MALLOC(sizeof(struct releaseRequest))) == NULL)
		REPORT_ERROR("malloc error");
		
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

void NetRequest(QueueableItem item, unsigned int machineId)
{
		
	unsigned int nextId;
	struct QueueableItemWrapper* w;
	int packagesize;
	
	void* datacopy;
		
	//printf(WHERESTR "Recieved a netrequest, target machine: %d\n", WHEREARG, machineId);
	
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

	if (machineId > net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
			
	if ((w = MALLOC(sizeof(struct QueueableItemWrapper))) == NULL)
	{
		REPORT_ERROR("MALLOC error");
		return;
	}

	packagesize = PACKAGE_SIZE(((struct createRequest*)(item->dataRequest))->packageCode);

	if ((datacopy = MALLOC(packagesize)) == NULL)
	{
		REPORT_ERROR("MALLOC error");
		return;
	}
		
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
	
	//printf(WHERESTR "Recieved a netrequest, target machine: %d, %d, %d, %d, %d, %d, %d, MALLOC: %d\n", WHEREARG, machineId, net_idlookups[machineId], net_idlookups, w, nextId, net_idlookups[machineId]->fill, net_idlookups[machineId]->count, (int)malloc);

	
	//printf(WHERESTR "Mapping request id %d to %d\n", WHEREARG, w->origId, nextId);
	if (g_hash_table_lookup(Gnet_idlookups[machineId], (void*)nextId) == NULL)
		g_hash_table_insert(Gnet_idlookups[machineId], (void*)nextId, w);
	else
		REPORT_ERROR("Could not insert into net_idlookups")
	
	//printf(WHERESTR "Recieved a netrequest, target machine: %d\n", WHEREARG, machineId);
	g_queue_push_tail(Gnet_requestQueues[machineId], datacopy);
	
	//printf(WHERESTR "Signaling\n", WHEREARG);
	pthread_cond_signal(&net_work_ready);
	//printf(WHERESTR "Unlokcing\n", WHEREARG);
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
		
	if((Gnet_requestQueues = (GQueue**)MALLOC(sizeof(GQueue*) * net_remote_hosts)) == NULL)
		REPORT_ERROR("MALLOC error");

	if((Gnet_idlookups = (GHashTable**)MALLOC(sizeof(GHashTable*) * net_remote_hosts)) == NULL)
		REPORT_ERROR("MALLOC error");
		
	if((net_sequenceNumbers = (unsigned int*)MALLOC(sizeof(unsigned int) * net_remote_hosts)) == NULL)
		REPORT_ERROR("MALLOC error");

	for(i = 0; i < net_remote_hosts; i++)
	{
		Gnet_requestQueues[i] = g_queue_new();
		Gnet_idlookups[i] = g_hash_table_new(NULL, NULL);
		net_sequenceNumbers[i] = 0;
	}
	
	pthread_mutex_init(&net_leaseLock, NULL); 
	Gnet_leaseTable = g_hash_table_new(NULL, NULL);
	Gnet_writeInitiator = g_hash_table_new(NULL, NULL);

	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	pthread_create(&net_read_thread, &attr, net_Reader, NULL);
	pthread_create(&net_write_thread, &attr, net_Writer, NULL);

	pthread_attr_destroy(&attr);
}

void NetInvalidate(GUID id)
{
	
	struct invalidateRequest* req;
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
			//printf(WHERESTR "Processing invalidates, target machine: %d, GUID: %d\n", WHEREARG, i, id);
			if((req = (struct invalidateRequest*)MALLOC(sizeof(struct invalidateRequest))) == NULL)
				REPORT_ERROR("MALLOC error");
				
			req->dataItem = id;
			req->packageCode = PACKAGE_INVALIDATE_REQUEST;
			req->requestID = 0;
			g_queue_push_tail(Gnet_requestQueues[i], req);
		}
	}
	
	pthread_cond_signal(&net_work_ready);

	//printf(WHERESTR "Releasing lock: %d\n", WHEREARG, (int)&net_work_mutex);
	pthread_mutex_unlock(&net_work_mutex);
	//printf(WHERESTR "Processed invalidates\n", WHEREARG);
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
	
	pthread_mutex_destroy(&net_leaseLock);
	
	pthread_mutex_destroy(&net_work_mutex);
	pthread_cond_destroy(&net_work_ready);
}

void* net_Reader(void* data)
{
	
	struct pollfd* sockets;
	size_t i;
	int res;
	
	//printf(WHERESTR "Remote hosts %u\n", WHEREARG, net_remote_hosts);
	
	if ((sockets = MALLOC(sizeof(struct pollfd) * net_remote_hosts)) == NULL)
		REPORT_ERROR("MALLOC error");
		
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
		
		//printf(WHERESTR "Network packaged recieved\n", WHEREARG);
		
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
	GUID itemid;
	int initiatorNo;
	
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
				//printf(WHERESTR "Recieved signal\n", WHEREARG);
			}
		}
		
		//printf(WHERESTR "Unlocking\n", WHEREARG);
		pthread_mutex_unlock(&net_work_mutex);
		//printf(WHERESTR "Unlocked\n", WHEREARG);
		
		if (net_terminate || package == NULL)
			continue;

		//Catch and filter invalidates
		if (((struct createRequest*)package)->packageCode == PACKAGE_INVALIDATE_REQUEST)
		{
			//printf(WHERESTR "locking mutex\n", WHEREARG);
			pthread_mutex_lock(&net_leaseLock);
			//printf(WHERESTR "locked mutex\n", WHEREARG);
			
			itemid = ((struct invalidateRequest*)package)->dataItem;
			
			if (g_hash_table_lookup(Gnet_leaseTable, (void*)itemid) == NULL)
				g_hash_table_insert(Gnet_leaseTable, (void*)itemid, g_hash_table_new(NULL, NULL));
				
			//printf(WHERESTR "Processing invalidate package to: %d\n", WHEREARG, hostno);
			
			if (g_hash_table_lookup(g_hash_table_lookup(Gnet_leaseTable, (void*)itemid), (void*)hostno) != NULL)
			{
				initiatorNo = (int)g_hash_table_lookup(Gnet_writeInitiator, (void*)((struct invalidateRequest*)package)->dataItem) - 1;
				
				if ((int)hostno != initiatorNo)
				{
					//Regular invalidate, register as cleared
					//printf(WHERESTR "Invalidate, unregistered machine: %d for package %d\n", WHEREARG, hostno, itemid);
					((struct invalidateRequest*)package)->requestID = NET_MAX_SEQUENCE + 1;
					g_hash_table_remove(g_hash_table_lookup(Gnet_leaseTable, (void*)itemid), (void*)hostno);
				}
				else
				{
					//This host initiated the invalidate
					//printf(WHERESTR "Invalidate to %d was discarded because the host initiated the invalidate, id: %d\n", WHEREARG, hostno, ((struct invalidateRequest*)package)->dataItem);
					g_hash_table_remove(Gnet_writeInitiator, (void*)((struct invalidateRequest*)package)->dataItem);
					FREE(package);
					package = NULL;
				}
			}
			else
			{
				//printf(WHERESTR "Invalidate to %d was discarded, because the recipient does not have the data, id: %d.\n", WHEREARG, hostno, ((struct invalidateRequest*)package)->dataItem);
				//The host has newer seen the data, or actively destroyed it
				FREE(package);
				package = NULL;
			}
			pthread_mutex_unlock(&net_leaseLock);
		}
		else if (((struct createRequest*)package)->packageCode == PACKAGE_ACQUIRE_RESPONSE)
		{
			//printf(WHERESTR "locking mutex\n", WHEREARG);
			pthread_mutex_lock(&net_leaseLock);
			//printf(WHERESTR "locked mutex\n", WHEREARG);
			itemid = ((struct acquireResponse*)package)->dataItem;
			
			//printf(WHERESTR "Registering %d as holder of %d\n", WHEREARG, hostno, itemid);
			
			if (g_hash_table_lookup(Gnet_leaseTable, (void*)itemid) == NULL)
				g_hash_table_insert(Gnet_leaseTable, (void*)itemid, g_hash_table_new(NULL, NULL));
				
			if (g_hash_table_lookup(g_hash_table_lookup(Gnet_leaseTable, (void*)itemid), (void*)hostno) == NULL)
			{
				//printf(WHERESTR "Registering %d as holder of %d\n", WHEREARG, hostno, itemid);
				g_hash_table_insert(g_hash_table_lookup(Gnet_leaseTable, (void*)itemid), (void*)hostno, (void*)1);
			}
				
			if (((struct acquireResponse*)package)->mode == ACQUIRE_MODE_WRITE)
			{
				//printf(WHERESTR "Registering host %d as initiator for package %d\n", WHEREARG, hostno, itemid);
				if (g_hash_table_lookup(Gnet_writeInitiator, (void*)itemid) != NULL) {
					REPORT_ERROR("Same Host was registered twice for write");
				} else {
					if (hostno == 0)
						printf(WHERESTR "Inserting NULL into hashtable Gnet_leaseTable\n", WHEREARG);
						
					g_hash_table_insert(Gnet_writeInitiator, (void*)itemid, (void*)hostno+1);
				}
			}

			//printf(WHERESTR "Registered %d as holder of %d\n", WHEREARG, hostno, ((struct acquireResponse*)package)->dataItem);
			pthread_mutex_unlock(&net_leaseLock);
		}
	
		if (package != NULL)
		{
			//printf(WHERESTR "Sending package with type: %d, to %d for id: %d.\n", WHEREARG, ((struct createRequest*)package)->packageCode, hostno, ((struct invalidateRequest*)package)->dataItem);
			net_sendPackage(package, hostno);

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
	GUID itemid;
	
	if (data == NULL)
		return;
	
	if (machineId > net_remote_hosts)
	{
		REPORT_ERROR("Invalid machineId detected");
		return;
	}
	
#ifdef TRACE_NETWORK_PACKAGES
		printf(WHERESTR "Recieved a package from machine: %d, type: %s (%d), reqId: %d, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)data)->packageCode), ((struct createRequest*)data)->packageCode, ((struct createRequest*)data)->requestID, ((struct createRequest*)data)->dataItem);
#endif
	
	
	switch(((struct createRequest*)data)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_RELEASE_REQUEST:
		case PACKAGE_INVALIDATE_REQUEST:
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:

			//printf(WHERESTR "Processing network package from %d, with type: %s, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)data)->packageCode), ((struct acquireRequest*)data)->dataItem);
			
			if (((struct createRequest*)data)->packageCode == PACKAGE_RELEASE_REQUEST)
			{
				itemid = ((struct releaseRequest*)data)->dataItem;
				//printf(WHERESTR "Processing release request from %d, GUID: %d\n", WHEREARG, machineId, itemid);
				
				//printf(WHERESTR "locking mutex\n", WHEREARG);			
				pthread_mutex_lock(&net_leaseLock);
				//printf(WHERESTR "locked mutex\n", WHEREARG);
				//The read release request implies that the sender has destroyed the copy
				if (g_hash_table_lookup(Gnet_leaseTable, (void*)itemid) == NULL)
					g_hash_table_insert(Gnet_leaseTable, (void*)itemid, g_hash_table_new(NULL, NULL));

				if (((struct releaseRequest*)data)->mode == ACQUIRE_MODE_READ)
				{
					//printf(WHERESTR "Processing READ release request from %d, GUID: %d\n", WHEREARG, machineId, itemid);
					if (g_hash_table_lookup(g_hash_table_lookup(Gnet_leaseTable, (void*)itemid), (void*)machineId) != NULL)
						g_hash_table_remove(g_hash_table_lookup(Gnet_leaseTable, (void*)itemid), (void*)machineId);
					//printf(WHERESTR "Release recieved for READ %d, unregistering requestor %d\n", WHEREARG, itemid, machineId);
					FREE(data);
					data = NULL;
					return;
				}

				pthread_mutex_unlock(&net_leaseLock);				
			}
		
		
			if ((ui = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
				REPORT_ERROR("MALLOC error");
			ui->dataRequest = data;
			ui->Gqueue = &Gnet_requestQueues[machineId];
			ui->mutex = &net_work_mutex;
			ui->event = &net_work_ready;
			EnqueItem(ui);
			break;
		
		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_RELEASE_RESPONSE:
		case PACKAGE_INVALIDATE_RESPONSE:
		case PACKAGE_MIGRATION_RESPONSE:
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
		//case PACKAGE_NACK:
		
			//printf(WHERESTR "locking mutex\n", WHEREARG);
			pthread_mutex_lock(&net_work_mutex);
			//printf(WHERESTR "locked mutex\n", WHEREARG);
			
			if (((struct createRequest*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE)
			{
				//printf(WHERESTR "Processing Acquire Response package from %d, with type: %d, for id: %d\n", WHEREARG, machineId, ((struct acquireResponse*)data)->packageCode, ((struct acquireResponse*)data)->dataItem);
			}
			

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

			if (((struct createRequest*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE || ((struct createRequest*)data)->packageCode == PACKAGE_MIGRATION_RESPONSE || ((struct createRequest*)data)->packageCode == PACKAGE_ACQUIRE_BARRIER_RESPONSE)
			{
				//printf(WHERESTR "Acquire response package from %d, for guid: %d, reqId: %d\n", WHEREARG, machineId, ((struct acquireResponse*)data)->dataItem, ((struct acquireResponse*)data)->requestID);
				if ((ui = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
					REPORT_ERROR("malloc error");

				ui->event = NULL;
				ui->mutex = NULL;
				ui->Gqueue = NULL;
				ui->dataRequest = data;				

				//Forward this to the request coordinator, so it may record the data and propagate it
				EnqueItem(ui);
			}
			else
			{
				//printf(WHERESTR "Forwarding response directly\n", WHEREARG);
				
				//Other responses are sent directly to the reciever
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
	
	data = NULL;

	if (recv(fd, &type, sizeof(unsigned int), MSG_PEEK) != 0)
	{
		//printf(WHERESTR "Reading network package, type: %d\n", WHEREARG, type);
		switch(type)
		{
		case PACKAGE_CREATE_REQUEST:
			blocksize = sizeof(struct createRequest);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire create package"); 
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			blocksize = sizeof(struct acquireRequest);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire read package"); 
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			blocksize = sizeof(struct acquireRequest);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire write package"); 
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			blocksize = sizeof(struct acquireResponse);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			blocksize -= sizeof(void*);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire response package"); 

			blocksize = ((struct acquireResponse*)data)->dataSize;
			if (blocksize != 0)
			{
				if ((((struct acquireResponse*)data)->data = MALLOC_ALIGN(blocksize, 7)) == NULL)
					REPORT_ERROR("Failed to allocate space for acquire response data");
				if (recv(fd, ((struct acquireResponse*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from acquire response");
			}
			else 
				((struct acquireResponse*)data)->data = NULL;
			
			break;
		case PACKAGE_RELEASE_REQUEST:
			blocksize = sizeof(struct releaseRequest);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			blocksize -= sizeof(unsigned long);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire release request package"); 

			blocksize = ((struct releaseRequest*)data)->dataSize;
			if (blocksize != 0)
			{
				if ((((struct releaseRequest*)data)->data = MALLOC_ALIGN(blocksize, 7)) == NULL)
					REPORT_ERROR("Failed to allocate space for release request data");
				if (recv(fd, ((struct releaseRequest*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from release request");
			} else {
				((struct releaseRequest*)data)->data = NULL;
			}
			break;
		case PACKAGE_RELEASE_RESPONSE:
			blocksize = sizeof(struct releaseResponse);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire release response package"); 
			break;
		case PACKAGE_INVALIDATE_REQUEST:
			blocksize = sizeof(struct invalidateRequest);
			//printf(WHERESTR "Reading network package, type: %d, size: %d\n", WHEREARG, type, blocksize);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			//printf(WHERESTR "Reading network package, type: %d, pointer:%d, fd: %d, size: %d\n", WHEREARG, type, (int)data, fd, blocksize);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire invalidate request package"); 
			//printf(WHERESTR "Read network package, type: %d\n", WHEREARG, type);
			break;
		case PACKAGE_INVALIDATE_RESPONSE:
			blocksize = sizeof(struct invalidateResponse);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire invalidate response package"); 
			break;
		case PACKAGE_NACK:
			blocksize = sizeof(struct NACK);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire nack package"); 
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			blocksize = sizeof(struct migrationResponse);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			
			blocksize -= (sizeof(unsigned long) * 2);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire migration package"); 

			blocksize = ((struct migrationResponse*)data)->dataSize;
			if (blocksize != 0)
			{
				if ((((struct migrationResponse*)data)->data = MALLOC_ALIGN(blocksize, 7)) == NULL)
					REPORT_ERROR("Failed to allocate space for migration response data");
				if (recv(fd, ((struct migrationResponse*)data)->data, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read package data from migration response");
			}
			else
				((struct migrationResponse*)data)->data = NULL;
				
			blocksize = ((struct migrationResponse*)data)->waitListSize;
			if (blocksize != 0)
			{
				if ((((struct migrationResponse*)data)->waitList = MALLOC(blocksize)) == NULL)
					REPORT_ERROR("Failed to allocate space for migration response waitlist");
				if (recv(fd, ((struct migrationResponse*)data)->waitList, blocksize, MSG_WAITALL) != blocksize)
					REPORT_ERROR("Failed to read waitlist from migration response");
			}
			else
				((struct migrationResponse*)data)->waitList = NULL;
		
			break;
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			blocksize = sizeof(struct acquireBarrierRequest);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire barrier package"); 
			break;

		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			blocksize = sizeof(struct acquireBarrierResponse);
			if ((data = MALLOC(blocksize)) == NULL)
				REPORT_ERROR("MALLOC error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire barrier package"); 
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
	
	fd = net_remote_handles[machineId];
	
	if (package == NULL) { REPORT_ERROR("NULL pointer"); }

#ifdef TRACE_NETWORK_PACKAGES
		printf(WHERESTR "Sending a package to machine: %d, type: %s (%d), reqId: %d, possible id: %d\n", WHEREARG, machineId, PACKAGE_NAME(((struct createRequest*)package)->packageCode), ((struct createRequest*)package)->packageCode, ((struct createRequest*)package)->requestID, ((struct createRequest*)package)->dataItem);
#endif
		
	switch(((struct createRequest*)package)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct createRequest), 0) != sizeof(struct createRequest))
				REPORT_ERROR("Failed to send entire create request read package");			 
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct acquireRequest), 0) != sizeof(struct acquireRequest))
				REPORT_ERROR("Failed to send entire acquire request read package");
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct acquireRequest), 0) != sizeof(struct acquireRequest))
				REPORT_ERROR("Failed to send entire acquire request write package");			 
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct acquireResponse) - sizeof(void*), MSG_MORE) != sizeof(struct acquireResponse) - sizeof(void*))
				REPORT_ERROR("Failed to send entire acquire response package");
			if (((struct acquireResponse*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			printf(WHERESTR "Data is %u, size is %u\n", WHEREARG, ((struct acquireResponse*)package)->data, ((struct acquireResponse*)package)->dataSize);
			if (send(fd, ((struct acquireResponse*)package)->data, ((struct acquireResponse*)package)->dataSize, 0) != ((struct acquireResponse*)package)->dataSize)
				REPORT_ERROR("Failed to send entire acquire response data package");			 
			break;
		case PACKAGE_RELEASE_REQUEST:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct releaseRequest) - sizeof(void*), MSG_MORE) != sizeof(struct releaseRequest) - sizeof(void*))
				REPORT_ERROR("Failed to send entire release request package");
			if (((struct releaseRequest*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, ((struct releaseRequest*)package)->data, ((struct releaseRequest*)package)->dataSize, 0) != ((struct releaseRequest*)package)->dataSize)
				REPORT_ERROR("Failed to send entire release request data package");				  
			break;
		case PACKAGE_RELEASE_RESPONSE:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct releaseResponse), 0) != sizeof(struct releaseResponse))
				REPORT_ERROR("Failed to send entire release response package"); 
			break;
		case PACKAGE_INVALIDATE_REQUEST:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct invalidateRequest), 0) != sizeof(struct invalidateRequest))
				REPORT_ERROR("Failed to send entire invalidate request package");
			break;
		case PACKAGE_INVALIDATE_RESPONSE:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct invalidateResponse), 0) != sizeof(struct invalidateResponse))
				REPORT_ERROR("Failed to send entire invalidate response package");
			break;
		case PACKAGE_NACK:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct NACK), 0) != sizeof(struct NACK))
				REPORT_ERROR("Failed to send entire nack package");				
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			send(fd, package, sizeof(struct migrationResponse) - (2 * sizeof(unsigned long)), MSG_MORE);
			if (((struct migrationResponse*)package)->data == NULL) { REPORT_ERROR("NULL pointer"); }
			send(fd, ((struct migrationResponse*)package)->data, ((struct migrationResponse*)package)->dataSize, MSG_MORE); 
			if (((struct migrationResponse*)package)->waitList == NULL) { REPORT_ERROR("NULL pointer"); }
			send(fd, ((struct migrationResponse*)package)->waitList, ((struct migrationResponse*)package)->waitListSize, 0); 
			break;
		case PACKAGE_ACQUIRE_BARRIER_REQUEST:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct acquireBarrierRequest), 0) != sizeof(struct acquireBarrierRequest))
				REPORT_ERROR("Failed to send entire acquire barrier request package");				
			break;
		case PACKAGE_ACQUIRE_BARRIER_RESPONSE:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			if (send(fd, package, sizeof(struct acquireBarrierResponse), 0) != sizeof(struct acquireBarrierResponse))
				REPORT_ERROR("Failed to send entire acquire barrier response package");				
			break;
		case PACKAGE_WRITEBUFFER_READY:
			if (package == NULL) { REPORT_ERROR("NULL pointer"); }
			break;
		default:
			fprintf(stderr, WHERESTR "Invalid package type (%u) detected\n", WHEREARG, ((struct createRequest*)package)->packageCode);
	}
}

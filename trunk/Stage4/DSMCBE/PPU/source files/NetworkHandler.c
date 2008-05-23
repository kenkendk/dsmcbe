#include <malloc.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <poll.h>
#include <stropts.h>

#include "../../common/datastructures.h"
#include "../header files/RequestCoordinator.h"
#include "../../dsmcbe.h"
#include "../../common/debug.h"
#include "../header files/NetworkHandler.h"

int lessint(void* a, void* b);
int hashfc(void* a, unsigned int count);

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
queue* net_requestQueues;

//Keep track of which hosts have what data, this minimizes the amount of invalidate messages
hashtable net_leaseTable;
hashtable net_writeInitiator;

//The number of hosts
unsigned int net_remote_hosts;
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
hashtable* net_idlookups;

//These are the sequence numbers assigned to the packages, a unique sequence for each host
unsigned int* net_sequenceNumbers;

void NetRequest(QueueableItem item, unsigned int machineId)
{
	unsigned int nextId;
	struct QueueableItemWrapper* w;
	
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
	
	if ((w = malloc(sizeof(struct QueueableItemWrapper))) == NULL)
	{
		REPORT_ERROR("malloc error");
		return;
	} 
	
	w->ui = item;
	w->origId = ((struct createRequest*)(item->dataRequest))->requestID;; 
	
	pthread_mutex_lock(&net_work_mutex);

	nextId = NEXT_SEQ_NO(net_sequenceNumbers[machineId], NET_MAX_SEQUENCE);
	((struct createRequest*)(item->dataRequest))->requestID = nextId;
	ht_insert(net_idlookups[machineId], (void*)nextId, w);
	queue_enq(net_requestQueues[machineId], item->dataRequest);
	
	pthread_mutex_unlock(&net_work_mutex);
}

void InitializeNetworkHandler(int* remote_handles, unsigned int remote_hosts)
{
	size_t i;
	pthread_attr_t attr;
	net_terminate = 0;
	
	net_remote_hosts = remote_hosts;
	net_remote_handles = remote_handles;
	
	pthread_mutex_init(&net_work_mutex, NULL);
	pthread_cond_init (&net_work_ready, NULL);
	
	if((net_requestQueues = (queue*)malloc(sizeof(queue) * net_remote_hosts)) == NULL)
		REPORT_ERROR("malloc error");

	if((net_idlookups = (hashtable*)malloc(sizeof(hashtable) * net_remote_hosts)) == NULL)
		REPORT_ERROR("malloc error");
		
	if((net_sequenceNumbers = (unsigned int*)malloc(sizeof(unsigned int) * net_remote_hosts)) == NULL)
		REPORT_ERROR("malloc error");

	for(i = 0; i < net_remote_hosts; i++)
	{
		net_requestQueues[i] = queue_create();
		net_idlookups[i] = ht_create(10, lessint, hashfc);
		net_sequenceNumbers[i] = 0;
	}
	
	net_leaseTable = ht_create(10, lessint, hashfc);
	net_writeInitiator = ht_create(10, lessint, hashfc);

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
	
	pthread_mutex_lock(&net_work_mutex);
	for(i = 0; i < net_remote_hosts; i++)
	{
		if((req = (struct invalidateRequest*)malloc(sizeof(struct invalidateRequest))) == NULL)
			REPORT_ERROR("malloc error");
		req->dataItem = id;
		req->packageCode = PACKAGE_INVALIDATE_REQUEST;
		req->requestID = 0;
		queue_enq(net_requestQueues[i], req);
	}
	pthread_mutex_unlock(&net_work_mutex);
}

void TerminateNetworkHandler(int force)
{
	size_t i;

	net_terminate = force ? 1 : 1;
	
	pthread_mutex_lock(&net_work_mutex);
	pthread_cond_signal(&net_work_ready);
	pthread_mutex_unlock(&net_work_mutex);
	
	pthread_join(net_read_thread, NULL);
	pthread_join(net_write_thread, NULL);

	for(i = 0; i < net_remote_hosts; i++)
	{
		UnregisterInvalidateSubscriber(&net_requestQueues[i]);
		queue_destroy(net_requestQueues[i]);
		ht_destroy(net_idlookups[i]);
	}
	free(net_requestQueues);
	free(net_idlookups);
	free(net_sequenceNumbers);
	
	pthread_mutex_destroy(&net_work_mutex);
	pthread_cond_destroy(&net_work_ready);
}

void* net_Reader(void* data)
{
	struct pollfd* sockets;
	size_t i;
	int res;
	
	if ((sockets = malloc(sizeof(struct pollfd) * net_remote_hosts)) == NULL)
		REPORT_ERROR("malloc error");
		
	for(i = 0; i < net_remote_hosts; i++)
	{
		sockets[i].fd = net_remote_handles[i];
		sockets[i].events = POLLIN | POLLHUP;
	}
		
	while(!net_terminate)
	{
		//We check each 5 seconds for the termination event
		res = poll(sockets, net_remote_hosts, 5);
		if (res < 0) {
			REPORT_ERROR("Poll reported error");
		} else if (res == 0)
			continue;		
		
		for(i = 0; i < net_remote_hosts; i++)
		{
			if (sockets[i].revents & POLLIN)
				net_processPackage(net_readPackage(sockets[i].fd), i);
			else if (sockets[i].revents & POLLHUP)
				REPORT_ERROR("Socked closed unexpectedly");
		}
	}
	
	free(sockets);
	
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
	
	while(!net_terminate)
	{
		hostno = net_remote_hosts + 1;
		package = NULL;
		pthread_mutex_lock(&net_work_mutex);
		while(package == NULL && !net_terminate)
		{
			for(i = 0; i < net_remote_hosts; i++)
				if (!queue_empty(net_requestQueues[i]))
				{
					package = queue_deq(net_requestQueues[i]);
					hostno = i;
					break;
				}
			if (package == NULL && !net_terminate)
				pthread_cond_wait(&net_work_ready, &net_work_mutex);
		}
		
		pthread_mutex_unlock(&net_work_mutex);
		
		if (net_terminate || package == NULL)
			continue;


		//Catch and filter invalidates
		if (((struct createRequest*)package)->packageCode == PACKAGE_INVALIDATE_REQUEST)
		{
			if (slset_member((slset)ht_get(net_leaseTable, (void*)itemid), (void*)hostno))
			{
				initiatorNo = -1;
				if (ht_member(net_writeInitiator, (void*)((struct invalidateRequest*)package)->dataItem))
					initiatorNo = (int)ht_get(net_writeInitiator, (void*)((struct invalidateRequest*)package)->dataItem);
				
				if ((int)hostno != initiatorNo)
				{
					//Regular invalidate, register as cleared
					//printf(WHERESTR "Forwarding invalidate for id %d to SPU %d\n", WHEREARG, itemid, i);
					slset_delete((slset)ht_get(net_leaseTable, (void*)itemid), (void*)i);
				}
				else
				{
					//This host initiated the invalidate
					ht_delete(net_writeInitiator, (void*)((struct invalidateRequest*)package)->dataItem);
					free(package);
					package = NULL;
					//printf(WHERESTR "Got \"invalidateRequest\" message, but skipping because SPU is initiator, ID %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, i);
				}
			}
			else
			{
				//The host has newer seen the data, or actively destroyed it
				free(package);
				package = NULL;
			}
		}

		
		if (package != NULL)
		{
			net_sendPackage(package, hostno);
			free(package);
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
	
	switch(((struct createRequest*)data)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
		case PACKAGE_ACQUIRE_REQUEST_READ:
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
		case PACKAGE_RELEASE_REQUEST:
		case PACKAGE_INVALIDATE_REQUEST:
		
			if (((struct createRequest*)data)->packageCode == PACKAGE_RELEASE_REQUEST)
			{
				itemid = ((struct releaseRequest*)data)->dataItem;
				//The release request implies that the sender has destroyed the copy
				if (!ht_member(net_leaseTable, (void*)itemid))
					ht_insert(net_leaseTable, (void*)itemid, slset_create(lessint));

				if (((struct releaseRequest*)data)->mode == READ)
				{
					if (slset_member((slset)ht_get(net_leaseTable, (void*)itemid), (void*)machineId))
						slset_delete((slset)ht_get(net_leaseTable, (void*)itemid), (void*)machineId);
					//printf(WHERESTR "Release recieved for READ %d, unregistering requestor %d\n", WHEREARG, itemid, i);
					free(data);
					return;
				}
				else
				{
					//Register host for invalidate messages, if required
					if (!slset_member((slset)ht_get(net_leaseTable, (void*)itemid), (void*)machineId))
						slset_insert((slset)ht_get(net_leaseTable, (void*)itemid), (void*)machineId, NULL);
						
					//printf(WHERESTR "Registering SPU %d as initiator for package %d\n", WHEREARG, i, itemid);
					if (ht_member(net_writeInitiator, (void*)itemid)) {
						REPORT_ERROR("Same Host was registered twice for write");
					} else {							
						ht_insert(net_writeInitiator, (void*)itemid, (void*)machineId);
					}
				}
			}
		
		
			if ((ui = malloc(sizeof(struct QueueableItemStruct))) == NULL)
				REPORT_ERROR("malloc error");
			ui->dataRequest = data;
			ui->queue = &net_requestQueues[machineId];
			ui->mutex = &net_work_mutex;
			ui->event = &net_work_ready;
			EnqueItem(ui);		
			break;
		
		case PACKAGE_ACQUIRE_RESPONSE:
		case PACKAGE_RELEASE_RESPONSE:
		case PACKAGE_INVALIDATE_RESPONSE:
		case PACKAGE_MIGRATION_RESPONSE:
		
			if (!ht_member(net_idlookups[machineId], (void*)((struct createRequest*)data)->requestID))
			{
				REPORT_ERROR("Incoming response did not match an outgoing request");
				return;
			}
			
			pthread_mutex_lock(&net_work_mutex);

			w = ht_get(net_idlookups[machineId], (void*)((struct createRequest*)data)->requestID);
			ht_delete(net_idlookups[machineId], (void*)((struct createRequest*)data)->requestID);

			pthread_mutex_unlock(&net_work_mutex);

			((struct createRequest*)data)->requestID = w->origId;
			ui = w->ui;
			free(w);

			if (((struct createRequest*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE || ((struct createRequest*)data)->packageCode == PACKAGE_MIGRATION_RESPONSE)
			{
				//Forward this to the request coordinator, so it may record the data and propagate it
				EnqueItem(ui);
			}
			else
			{
				//Other responses are sent directly to the reciever
				if (ui->mutex != NULL)
					pthread_mutex_lock(ui->mutex);
				if (ui->queue != NULL)
					queue_enq(*(ui->queue), data);
				if (ui->event != NULL)
					pthread_cond_signal(ui->event);
				if (ui->mutex != NULL)
					pthread_mutex_unlock(ui->mutex);
	
				free(ui);
			}
			
			break;
		
		default:
			REPORT_ERROR("Invalid package type detected");
		 
	}
	
}

void* net_readPackage(int fd)
{
	unsigned char type;
	void* data;
	int blocksize;
	
	data = NULL;
	
	if (recv(fd, &type, sizeof(unsigned char), MSG_PEEK) != 0)
	{
		switch(type)
		{
		case PACKAGE_CREATE_REQUEST:
			blocksize = sizeof(struct createRequest);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire create package"); 
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			blocksize = sizeof(struct acquireRequest);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire read package"); 
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			blocksize = sizeof(struct acquireRequest);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire write package"); 
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			blocksize = sizeof(struct acquireResponse);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			blocksize -= sizeof(unsigned long);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire acquire response package"); 

			blocksize = ((struct acquireResponse*)data)->dataSize;
			if ((((struct acquireResponse*)data)->data = _malloc_align(blocksize, 7)) == NULL)
				REPORT_ERROR("Failed to allocate space for acquire response data");
			if (recv(fd, ((struct acquireResponse*)data)->data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read package data from acquire response");
			
			break;
		case PACKAGE_RELEASE_REQUEST:
			blocksize = sizeof(struct releaseRequest);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			blocksize -= sizeof(unsigned long);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire release request package"); 

			blocksize = ((struct releaseRequest*)data)->dataSize;
			if ((((struct releaseRequest*)data)->data = _malloc_align(blocksize, 7)) == NULL)
				REPORT_ERROR("Failed to allocate space for release request data");
			if (recv(fd, ((struct releaseRequest*)data)->data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read package data from release request");

			break;
		case PACKAGE_RELEASE_RESPONSE:
			blocksize = sizeof(struct releaseResponse);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire release response package"); 
			break;
		case PACKAGE_INVALIDATE_REQUEST:
			blocksize = sizeof(struct invalidateRequest);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire invalidate request package"); 
			break;
		case PACKAGE_INVALIDATE_RESPONSE:
			blocksize = sizeof(struct invalidateResponse);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire invalidate response package"); 
			break;
		case PACKAGE_NACK:
			blocksize = sizeof(struct NACK);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire nack package"); 
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			blocksize = sizeof(struct migrationResponse);
			if ((data = malloc(blocksize)) == NULL)
				REPORT_ERROR("malloc error");
			
			blocksize -= (sizeof(unsigned long) * 2);
			if (recv(fd, data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read entire migration package"); 

			blocksize = ((struct migrationResponse*)data)->dataSize;
			if ((((struct migrationResponse*)data)->data = _malloc_align(blocksize, 7)) == NULL)
				REPORT_ERROR("Failed to allocate space for migration response data");
			if (recv(fd, ((struct migrationResponse*)data)->data, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read package data from migration response");
		
			blocksize = ((struct migrationResponse*)data)->waitListSize;
			if ((((struct migrationResponse*)data)->waitList = malloc(blocksize)) == NULL)
				REPORT_ERROR("Failed to allocate space for migration response waitlist");
			if (recv(fd, ((struct migrationResponse*)data)->waitList, blocksize, MSG_WAITALL) != blocksize)
				REPORT_ERROR("Failed to read waitlist from migration response");
		
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
		
	switch(((struct createRequest*)package)->packageCode)
	{
		case PACKAGE_CREATE_REQUEST:
			send(fd, package, sizeof(struct createRequest), 0);
			break;
		case PACKAGE_ACQUIRE_REQUEST_READ:
			send(fd, package, sizeof(struct acquireRequest), 0);
			break;
		case PACKAGE_ACQUIRE_REQUEST_WRITE:
			send(fd, package, sizeof(struct acquireRequest), 0);
			break;
		case PACKAGE_ACQUIRE_RESPONSE:
			send(fd, package, sizeof(struct acquireResponse) - sizeof(unsigned long), 0);
			send(fd, ((struct acquireResponse*)package)->data, ((struct acquireResponse*)package)->dataSize, 0); 
			break;
		case PACKAGE_RELEASE_REQUEST:
			send(fd, package, sizeof(struct releaseRequest) - sizeof(unsigned long), 0);
			send(fd, ((struct releaseRequest*)package)->data, ((struct releaseRequest*)package)->dataSize, 0); 
			break;
		case PACKAGE_RELEASE_RESPONSE:
			send(fd, package, sizeof(struct releaseResponse), 0);
			break;
		case PACKAGE_INVALIDATE_REQUEST:
			send(fd, package, sizeof(struct invalidateRequest), 0);
			break;
		case PACKAGE_INVALIDATE_RESPONSE:
			send(fd, package, sizeof(struct invalidateResponse), 0);
			break;
		case PACKAGE_NACK:
			send(fd, package, sizeof(struct NACK), 0);
			break;
		case PACKAGE_MIGRATION_RESPONSE:
			send(fd, package, sizeof(struct migrationResponse) - (2 * sizeof(unsigned long)), 0);
			send(fd, ((struct migrationResponse*)package)->data, ((struct migrationResponse*)package)->dataSize, 0); 
			send(fd, ((struct migrationResponse*)package)->waitList, ((struct migrationResponse*)package)->waitListSize, 0); 
			break;
	}
}

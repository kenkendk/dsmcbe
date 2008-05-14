/*
 *
 * This module contains code that handles requests 
 * from SPE units via Mailbox messages
 *
 */
 
#include <pthread.h>
#include "../header files/SPUEventHandler.h"
#include "../../common/datastructures.h"
#include "../header files/RequestCoordinator.h"

#include "../../common/debug.h"

int lessint(void* a, void* b);
int hashfc(void* a, unsigned int count);

//This is used to terminate the thread
volatile int spu_terminate;

//This is a handle to the thread
pthread_t spu_workthread;

//This is the mutex and condition that protects the work queues
pthread_mutex_t spu_work_mutex;
pthread_cond_t spu_work_ready;

//These keep track of the SPE threads
spe_context_ptr_t* spe_threads;
unsigned int spe_thread_count;

//Each thread has its own requestQueue and mailboxQueue
queue* spu_requestQueues;
queue* spu_mailboxQueues;

//This table contains all items that are forwarded, key is the id, 
//value is a sorted list with SPU id's  
hashtable spu_leaseTable;

//This table contains the initiator of a write, key is GUID, value is SPU id
hashtable spu_writeInitiator;


void* SPU_Worker(void* data);

void TerminateSPUHandler(int force)
{
	size_t i;
	
	spu_terminate = 1;
	pthread_join(spu_workthread, NULL);
	
	
	pthread_mutex_destroy(&spu_work_mutex);
	pthread_cond_destroy(&spu_work_ready);
	
	
	for(i = 0; i < spe_thread_count; i++)
	{
		queue_destroy(spu_mailboxQueues[i]);
		queue_destroy(spu_requestQueues[i]);
		UnregisterInvalidateSubscriber(&spu_mailboxQueues[i]);
	}
	
	ht_destroy(spu_leaseTable);
	ht_destroy(spu_writeInitiator);
	free(spu_requestQueues);
	free(spu_mailboxQueues);
}

void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count)
{
	size_t i;
	
	pthread_attr_t attr;
	spu_terminate = 0;

	spe_thread_count = thread_count;
	spe_threads = threads;

	/* Setup queues */

	if((spu_requestQueues = (queue*)malloc(sizeof(queue) * spe_thread_count)) == NULL)
		perror("SPUEventHandler.c: malloc error");
		
	if((spu_mailboxQueues = (queue*)malloc(sizeof(queue) * spe_thread_count)) == NULL)
		perror("SPUEventHandler.c: malloc error");;
	
	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&spu_work_mutex, NULL);
	pthread_cond_init (&spu_work_ready, NULL);

	for(i = 0; i < spe_thread_count; i++)
	{
		spu_requestQueues[i] = queue_create();
		spu_mailboxQueues[i] = queue_create();
		RegisterInvalidateSubscriber(&spu_work_mutex, &spu_requestQueues[i]);
	}

	/* Setup the lease table */
	spu_leaseTable = ht_create(10, lessint, hashfc);
	spu_writeInitiator = ht_create(10, lessint, hashfc);

	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&spu_workthread, &attr, SPU_Worker, NULL);
	pthread_attr_destroy(&attr);
}

void ReadMBOXBlocking(spe_context_ptr_t spe, unsigned int* target, unsigned int count)
{
	unsigned int readcount;
	while(count > 0)
	{
		while (spe_out_mbox_status(spe) == 0)
			;
		readcount = spe_out_mbox_read(spe, target, count);
		count -= readcount;
		target = &target[readcount];		
	}
}

void* SPU_Worker(void* data)
{
	size_t i;
	
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	void* datapointer;
	GUID itemid;
	void* dataItem = NULL;
	QueueableItem queueItem;
	int mode;
	int initiatorNo;
	
	while(!spu_terminate)
	{
		//printf(WHERESTR "Inside loop\n", WHEREARG);
		//Step 1, process SPU mailboxes
		for(i = 0; i < spe_thread_count; i++)
			if (spe_out_mbox_status(spe_threads[i]) != 0)
			{
				datatype = 8000;
				dataItem = NULL;
				//printf(WHERESTR "SPU mailbox message detected\n", WHEREARG);
				if (spe_out_mbox_read(spe_threads[i], &datatype, 1) != 1)
					REPORT_ERROR("Read MBOX failed (1)!");
					
				switch(datatype)
				{
					case PACKAGE_CREATE_REQUEST:
						//printf(WHERESTR "Create recieved\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct createRequest))) == NULL)
							REPORT_ERROR("malloc error");;
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&datasize, 1);
												
						((struct createRequest*)dataItem)->dataItem = itemid;
						((struct createRequest*)dataItem)->packageCode = datatype;
						((struct createRequest*)dataItem)->requestID = requestID;
						((struct createRequest*)dataItem)->dataSize = datasize;
						break;
						
					case PACKAGE_ACQUIRE_REQUEST_READ:
						//printf(WHERESTR "Acquire READ recieved\n", WHEREARG);
					case PACKAGE_ACQUIRE_REQUEST_WRITE:
						//printf(WHERESTR "Acquire WRITE recieved\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct acquireRequest))) == NULL)
							REPORT_ERROR("malloc error");
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
													
						((struct acquireRequest*)dataItem)->dataItem = itemid;
						((struct acquireRequest*)dataItem)->packageCode = datatype;
						((struct acquireRequest*)dataItem)->requestID = requestID;
						//printf(WHERESTR "Got %d, %d, %d\n", WHEREARG, itemid, datatype, requestID);
						break;
						
					case PACKAGE_RELEASE_REQUEST:
						//printf(WHERESTR "Release recieved\n", WHEREARG);
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&mode, 1);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&datasize, 1);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&datapointer, 1);


						//Only forward write releases
						if (mode == WRITE)
						{
							//printf(WHERESTR "Release recieved for WRITE, forwarding request and registering initiator\n", WHEREARG);
							if ((dataItem = malloc(sizeof(struct releaseRequest))) == NULL)
								REPORT_ERROR("malloc failed");
	
							//printf(WHERESTR "Release ID: %i\n", WHEREARG, itemid);
							((struct releaseRequest*)dataItem)->packageCode = datatype;
							((struct releaseRequest*)dataItem)->dataItem = itemid;
							((struct releaseRequest*)dataItem)->mode = mode;
							((struct releaseRequest*)dataItem)->requestID = requestID;
							((struct releaseRequest*)dataItem)->dataSize = datasize;
							((struct releaseRequest*)dataItem)->data = datapointer;
						}
						else
						{
							//printf(WHERESTR "Release recieved for READ, unregistering requestor\n", WHEREARG);
							//The release request implies that the sender has destroyed the copy
							if (!ht_member(spu_leaseTable, (void*)itemid))
								ht_insert(spu_leaseTable, (void*)itemid, slset_create(lessint));
							if (slset_member((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)i))
								slset_delete((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)i);
						}
						
						break;
			
					/*case PACKAGE_INVALIDATE_RESPONSE:
						printf(WHERESTR "Invalidate Response\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct invalidateResponse))) == NULL)
							perror("SPUEventHandler.c: malloc failed");

						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
					
						((struct invalidateResponse*)dataItem)->packageCode = datatype;
						((struct invalidateResponse*)dataItem)->requestID = requestID;
						break;*/
					default:
						fprintf(stderr, WHERESTR "Bad SPU request, ID was: %i, message: %s\n", WHEREARG, datatype, strerror(errno));
						break;
				}
				
				//If the request 
				if (dataItem != NULL)
				{
					queueItem = (QueueableItem)malloc(sizeof(struct QueueableItemStruct));
					queueItem->dataRequest = dataItem;
					queueItem->event = &spu_work_ready;
					queueItem->mutex = &spu_work_mutex;
					queueItem->queue = &spu_requestQueues[i];
					
					//printf(WHERESTR "Got message from SPU, enqued as %i\n", WHEREARG, (int)queueItem);
					
					EnqueItem(queueItem);
					//printf(WHERESTR "Got message from SPU, mutex is %i\n", WHEREARG, (int)queueItem->mutex);
					dataItem = NULL;
					queueItem = NULL;
				}
				
			}
		
		//Step 2, proccess any responses
		//printf(WHERESTR "checking for coordinator reponses\n", WHEREARG);
		
		pthread_mutex_lock(&spu_work_mutex);
		for(i = 0; i < spe_thread_count; i++)
			while (!queue_empty(spu_requestQueues[i]))
			{
				//printf(WHERESTR "Detected coordinator response\n", WHEREARG);
				
				dataItem = queue_deq(spu_requestQueues[i]);
				pthread_mutex_unlock(&spu_work_mutex);
				
				datatype = ((unsigned char*)dataItem)[0];
				//printf(WHERESTR "Got response from Coordinator\n", WHEREARG);
				switch(datatype)
				{
					case PACKAGE_ACQUIRE_RESPONSE:
						//printf(WHERESTR "Got acquire response message, converting to MBOX messages (%d:%d)\n", WHEREARG, (int)((struct acquireResponse*)dataItem)->data, (int)((struct acquireResponse*)dataItem)->dataSize);
						
						//Register this ID for the current SPE
						itemid = ((struct acquireResponse*)dataItem)->dataItem;
						if (!ht_member(spu_leaseTable,  (void*)itemid))
							ht_insert(spu_leaseTable, (void*)itemid, slset_create(lessint));
						if (!slset_member((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)i))
							slset_insert((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)i, NULL);
							
						queue_enq(spu_mailboxQueues[i], (void*)datatype);
						queue_enq(spu_mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->requestID);
						queue_enq(spu_mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->dataItem);
						queue_enq(spu_mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->mode);
						queue_enq(spu_mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->dataSize);
						queue_enq(spu_mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->data);
						
						//Register this SPU as the initiator
						if (((struct acquireResponse*)dataItem)->mode == WRITE)
						{
							//printf(WHERESTR "Registering SPU %d as initiator for package %d\n", WHEREARG, i, itemid);							
							ht_insert(spu_writeInitiator, (void*)itemid, (void*)i);
						}
						
						break;
					case PACKAGE_MIGRATION_RESPONSE:
						//printf(WHERESTR "Got migration response message, converting to MBOX messages\n", WHEREARG);
						break;
					case PACKAGE_RELEASE_RESPONSE:
						//printf(WHERESTR "Got acquire release message, converting to MBOX messages\n", WHEREARG);
						queue_enq(spu_mailboxQueues[i], (void*)datatype);
						queue_enq(spu_mailboxQueues[i], (void*)((struct releaseResponse*)dataItem)->requestID);
						break;
					case PACKAGE_NACK:
						//printf(WHERESTR "Got acquire nack message, converting to MBOX messages\n", WHEREARG);
						queue_enq(spu_mailboxQueues[i], (void*)datatype);
						queue_enq(spu_mailboxQueues[i], (void*)((struct NACK*)dataItem)->requestID);
						queue_enq(spu_mailboxQueues[i], (void*)((struct NACK*)dataItem)->hint);
						break;
					case PACKAGE_INVALIDATE_REQUEST:
						initiatorNo = -1;
						if (ht_member(spu_writeInitiator, (void*)((struct invalidateRequest*)dataItem)->dataItem))
							initiatorNo = (int)ht_get(spu_writeInitiator, (void*)((struct invalidateRequest*)dataItem)->dataItem);
						
						if ((int)i != initiatorNo)
						{
							//printf(WHERESTR "Got \"invalidateRequest\" message, converting to MBOX messages for %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, i);
							queue_enq(spu_mailboxQueues[i], (void*)datatype);
							queue_enq(spu_mailboxQueues[i], (void*)((struct invalidateRequest*)dataItem)->requestID);
							queue_enq(spu_mailboxQueues[i], (void*)((struct invalidateRequest*)dataItem)->dataItem);
						}
						else
						{
							ht_delete(spu_writeInitiator, (void*)((struct invalidateRequest*)dataItem)->dataItem);
							//printf(WHERESTR "Got \"invalidateRequest\" message, but skipping because SPU is initiator, ID %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, i);
						}
							
						break;
					default:						
						perror("SPUEventHandler.c: Bad Coordinator response");
						break;
				}
				
				free(dataItem);
								
				pthread_mutex_lock(&spu_work_mutex);
			}
	
		pthread_mutex_unlock(&spu_work_mutex);
	
		//printf(WHERESTR "Processing mailbox messages\n");
		
		//Step 3, forward messages to SPU
		for(i = 0; i < spe_thread_count; i++) 
		{
			while (!queue_empty(spu_mailboxQueues[i]) && spe_in_mbox_status(spe_threads[i]) != 0)	
			{
				//printf(WHERESTR "Sending Mailbox message: %i\n", WHEREARG, (unsigned int)mailboxQueues[i]->head->element);
				if (spe_in_mbox_write(spe_threads[i], (unsigned int*)&spu_mailboxQueues[i]->head->element, 1, SPE_MBOX_ALL_BLOCKING) != 1)
					perror("SPUEventHandler.c: Failed to send message, even though it was blocking!"); 

				queue_deq(spu_mailboxQueues[i]);
			}
		}				
		
	}
	
	//Returning the unused argument removes a warning
	return data;
}

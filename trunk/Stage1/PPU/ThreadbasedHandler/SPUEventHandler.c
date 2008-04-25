/*
 *
 * This module contains code that handles requests 
 * from SPE units via Mailbox messages
 *
 */
 
#include "SPUEventHandler.h"
#include <pthread.h>
#include "../datastructures.h"
#include "RequestCoordinator.h"
#include "../Common/debug.h"

volatile int terminate;
pthread_t workthread;
pthread_mutex_t work_mutex;
pthread_cond_t work_ready;

spe_context_ptr_t* spe_threads;
unsigned int spe_thread_count;

queue* requestQueues;
queue* mailboxQueues;



void* ProcessMessages(void* data);

void TerminateSPUHandler(int force)
{
	//size_t i;
	
	terminate = 1;
	pthread_join(workthread, NULL);
	
	pthread_mutex_destroy(&work_mutex);
	pthread_cond_destroy(&work_ready);
	
	/*
	for(i = 0; i < spe_thread_count; i++)
	{
		queue_free(mailboxQueues[i]);
		queue_free(requestQueues[i]);
	}
	*/
	
	free(requestQueues);
}

void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count)
{
	size_t i;
	
	pthread_attr_t attr;
	terminate = 0;

	spe_thread_count = thread_count;
	spe_threads = threads;

	/* Setup queues */
	requestQueues = (queue*)malloc(sizeof(queue) * spe_thread_count);
	mailboxQueues = (queue*)malloc(sizeof(queue) * spe_thread_count);
	for(i = 0; i < spe_thread_count; i++)
	{
		requestQueues[i] = queue_create();
		mailboxQueues[i] = queue_create();
	}
	
	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&work_mutex, NULL);
	pthread_cond_init (&work_ready, NULL);

	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&workthread, &attr, ProcessMessages, NULL);
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

void* ProcessMessages(void* data)
{
	size_t i;
	
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	void* datapointer;
	GUID itemid;
	void* dataItem;
	QueueableItem queueItem;
	
	while(!terminate)
	{
		//printf(WHERESTR "Inside loop\n", WHEREARG);
		//Step 1, process SPU mailboxes
		for(i = 0; i < spe_thread_count; i++)
			if (spe_out_mbox_status(spe_threads[i]) != 0)
			{
				datatype = 8000;
				printf(WHERESTR "SPU mailbox message detected\n", WHEREARG);
				if (spe_out_mbox_read(spe_threads[i], &datatype, 1) != 1)
					perror("Read MBOX failed (1)!");
					
				switch(datatype)
				{
					case PACKAGE_CREATE_REQUEST:
						printf(WHERESTR "Create recieved\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct createRequest))) == NULL)
							perror("SPUEventHandler.c: malloc error");;
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&datasize, 2);
												
						((struct createRequest*)dataItem)->dataItem = itemid;
						((struct createRequest*)dataItem)->packageCode = datatype;
						((struct createRequest*)dataItem)->requestID = requestID;
						((struct createRequest*)dataItem)->dataSize = datasize;
						break;
						
					case PACKAGE_ACQUIRE_REQUEST_READ:
					case PACKAGE_ACQUIRE_REQUEST_WRITE:
						printf(WHERESTR "Acquire recieved\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct acquireRequest))) == NULL)
							perror("SPUEventHandler.c: malloc error");
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
													
						((struct acquireRequest*)dataItem)->dataItem = itemid;
						((struct acquireRequest*)dataItem)->packageCode = datatype;
						((struct acquireRequest*)dataItem)->requestID = requestID;
						break;
						
					case PACKAGE_RELEASE_REQUEST:
						printf(WHERESTR "Release recieved\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct releaseRequest))) == NULL)
							perror("SPUEventHandler.c: malloc failed");
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&datasize, 2);
						ReadMBOXBlocking(spe_threads[i], (unsigned int*)&datapointer, 1);

						printf(WHERESTR "Release ID: %i\n", WHEREARG, itemid);
						((struct releaseRequest*)dataItem)->dataItem = itemid;
						((struct releaseRequest*)dataItem)->packageCode = datatype;
						((struct releaseRequest*)dataItem)->requestID = requestID;
						((struct releaseRequest*)dataItem)->dataSize = datasize;
						((struct releaseRequest*)dataItem)->data = datapointer;
						break;
			
					case PACKAGE_INVALIDATE_REQUEST:
						printf(WHERESTR "Invalidate recieved\n", WHEREARG);
						if ((dataItem = malloc(sizeof(struct invalidateRequest))) == NULL)
							perror("SPUEventHandler.c: malloc error");;
						ReadMBOXBlocking(spe_threads[i], &requestID, 1);
						ReadMBOXBlocking(spe_threads[i], &itemid, 1);
					
						((struct invalidateRequest*)dataItem)->dataItem = itemid;
						((struct invalidateRequest*)dataItem)->packageCode = datatype;
						((struct invalidateRequest*)dataItem)->requestID = requestID;
						break;
					default:
						perror("Bad SPU request!");
						break;
				}
				
				//If the request 
				if (dataItem != NULL)
				{
					queueItem = (QueueableItem)malloc(sizeof(struct QueueableItemStruct));
					queueItem->dataRequest = dataItem;
					queueItem->event = &work_ready;
					queueItem->mutex = &work_mutex;
					queueItem->queue = &requestQueues[i];
					
					printf(WHERESTR "Got message from SPU, enqued as %i\n", WHEREARG, (int)queueItem);
					
					EnqueItem(queueItem);
					dataItem = NULL;
					queueItem = NULL;
				}
				
			}
		
		//Step 2, proccess any responses
		//printf(WHERESTR "checking for coordinator reponses\n", WHEREARG);
		
		pthread_mutex_lock(&work_mutex);
		for(i = 0; i < spe_thread_count; i++)
			if (!queue_empty(requestQueues[i]))
			{
				printf(WHERESTR "Detected coordinator response\n", WHEREARG);
				
				dataItem = queue_deq(requestQueues[i]);
				pthread_mutex_unlock(&work_mutex);
				
				datatype = ((unsigned char*)dataItem)[0];
				printf(WHERESTR "Got response from Coordinator\n", WHEREARG);
				switch(datatype)
				{
					case PACKAGE_ACQUIRE_RESPONSE:
						printf(WHERESTR "Got acquire response message, converting to MBOX messages\n", WHEREARG);
						queue_enq(mailboxQueues[i], (void*)datatype);
						queue_enq(mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->requestID);
						queue_enq(mailboxQueues[i], (void*)((unsigned int*)&((struct acquireResponse*)dataItem)->dataSize)[0]);
						queue_enq(mailboxQueues[i], (void*)((unsigned int*)&((struct acquireResponse*)dataItem)->dataSize)[1]);
						queue_enq(mailboxQueues[i], (void*)((struct acquireResponse*)dataItem)->data);
						
						break;
					case PACKAGE_MIGRATION_RESPONSE:
						printf(WHERESTR "Got migration response message, converting to MBOX messages\n", WHEREARG);
						break;
					case PACKAGE_RELEASE_RESPONSE:
						printf(WHERESTR "Got acquire release message, converting to MBOX messages\n", WHEREARG);
						queue_enq(mailboxQueues[i], (void*)datatype);
						queue_enq(mailboxQueues[i], (void*)((struct releaseResponse*)dataItem)->requestID);
						break;
					case PACKAGE_NACK:
						printf(WHERESTR "Got acquire nack message, converting to MBOX messages\n", WHEREARG);
						queue_enq(mailboxQueues[i], (void*)datatype);
						queue_enq(mailboxQueues[i], (void*)((struct NACK*)dataItem)->requestID);
						queue_enq(mailboxQueues[i], (void*)((struct NACK*)dataItem)->hint);
						break;
					case PACKAGE_INVALIDATE_RESPONSE:
						printf(WHERESTR "Got acquire invalidate message, converting to MBOX messages\n", WHEREARG);
						queue_enq(mailboxQueues[i], (void*)datatype);
						queue_enq(mailboxQueues[i], (void*)((struct invalidateResponse*)dataItem)->requestID);
						break;
					default:
						
						perror("SPUEventHandler.c: Bad Coordinator response");
						break;
				}
				
				free(dataItem);
								
				pthread_mutex_lock(&work_mutex);
			}
	
		pthread_mutex_unlock(&work_mutex);
		
		//printf(WHERESTR "Processing mailbox messages\n");
		
		//Step 3, forward messages to SPU
		for(i = 0; i < spe_thread_count; i++) 
		{
			while (!queue_empty(mailboxQueues[i]) && spe_in_mbox_status(spe_threads[i]) != 0)	
			{
				printf(WHERESTR "Sending Mailbox message: %i\n", WHEREARG, (unsigned int)mailboxQueues[i]->head->element);
				if (spe_in_mbox_write(spe_threads[i], (unsigned int*)&mailboxQueues[i]->head->element, 1, SPE_MBOX_ALL_BLOCKING) != 1)
					perror("SPUEventHandler.c: Failed to send message, even though it was blocking!"); 

				queue_deq(mailboxQueues[i]);
			}
		}				
		
	}
	
	//Returning the unused argument removes a warning
	return data;
}

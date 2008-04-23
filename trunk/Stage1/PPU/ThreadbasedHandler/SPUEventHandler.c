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

volatile int terminate;
pthread_t workthread;
pthread_mutex_t work_mutex;
pthread_cond_t work_ready;

spe_context_ptr_t* spe_threads;
unsigned int spe_thread_count;

queue* requestQueues;



void* ProcessMessages(void* data);

void TerminateSPUHandler(int force)
{
	//size_t i;
	
	terminate = 1;
	pthread_join(workthread, NULL);
	
	pthread_mutex_destroy(&work_mutex);
	pthread_cond_destroy(&work_ready);
	
	/*for(i = 0; i < spe_thread_count; i++)
		queue_free(requestQueues[i]);*/
	
	free(requestQueues);
}

void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count)
{
	size_t i;
	
	pthread_attr_t attr;
	terminate = 0;
	
	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&work_mutex, NULL);
	pthread_cond_init (&work_ready, NULL);

	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&workthread, &attr, ProcessMessages, NULL);
	pthread_attr_destroy(&attr);
	
	spe_thread_count = thread_count;
	spe_threads = threads;
	
	requestQueues = (queue*)malloc(sizeof(queue) * spe_thread_count);
	for(i = 0; i < spe_thread_count; i++)
		requestQueues[i] = queue_create();
}

void* ProcessMessages(void* data)
{
	size_t i;
	
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	GUID itemid;
	void* dataItem;
	QueueableItem queueItem;
	
	while(!terminate)
	{
		//Step 1, process SPU mailboxes
		for(i = 0; i < spe_thread_count; i++)
			if (spe_out_mbox_status(spe_threads[i]) != 0)
			{
				if (spe_out_mbox_read(spe_threads[i], &datatype, 1) != 1)
					perror("Read MBOX failed!");
					
				switch(datatype)
				{
					case PACKAGE_CREATE_REQUEST:
						dataItem = malloc(sizeof(struct createRequest));

						if (spe_out_mbox_read(spe_threads[i], &requestID, 1) != 1)
							perror("Read MBOX failed!");
						if (spe_out_mbox_read(spe_threads[i], &itemid, 1) != 1)
							perror("Read MBOX failed!");
						if (spe_out_mbox_read(spe_threads[i], (unsigned int*)&datasize, 2) != 2)
							perror("Read MBOX failed!");
												
						((struct createRequest*)dataItem)->dataItem = itemid;
						((struct createRequest*)dataItem)->packageCode = datatype;
						((struct createRequest*)dataItem)->requestID = requestID;
						((struct createRequest*)dataItem)->dataSize = datasize;
						break;
						
					case PACKAGE_ACQUIRE_REQUEST_READ:
					case PACKAGE_ACQUIRE_REQUEST_WRITE:
						dataItem = malloc(sizeof(struct acquireRequest));

						if (spe_out_mbox_read(spe_threads[i], &requestID, 1) != 1)
							perror("Read MBOX failed!");
						if (spe_out_mbox_read(spe_threads[i], &itemid, 1) != 1)
							perror("Read MBOX failed!");
												
						((struct acquireRequest*)dataItem)->dataItem = itemid;
						((struct acquireRequest*)dataItem)->packageCode = datatype;
						((struct acquireRequest*)dataItem)->requestID = requestID;
						break;
						
					case PACKAGE_RELEASE_REQUEST:
						dataItem = malloc(sizeof(struct releaseRequest));

						if (spe_out_mbox_read(spe_threads[i], &requestID, 1) != 1)
							perror("Read MBOX failed!");
						if (spe_out_mbox_read(spe_threads[i], &itemid, 1) != 1)
							perror("Read MBOX failed!");
						if (spe_out_mbox_read(spe_threads[i], (unsigned int*)&datasize, 2) != 2)
							perror("Read MBOX failed!");
												
						((struct releaseRequest*)dataItem)->dataItem = itemid;
						((struct releaseRequest*)dataItem)->packageCode = datatype;
						((struct releaseRequest*)dataItem)->requestID = requestID;
						((struct releaseRequest*)dataItem)->dataSize = datasize;
						break;
			
					case PACKAGE_INVALIDATE_REQUEST:
						dataItem = malloc(sizeof(struct invalidateRequest));

						if (spe_out_mbox_read(spe_threads[i], &requestID, 1) != 1)
							perror("Read MBOX failed!");
						if (spe_out_mbox_read(spe_threads[i], &itemid, 1) != 1)
							perror("Read MBOX failed!");
												
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
					queueItem->event = work_ready;
					queueItem->mutex = work_mutex;
					queueItem->queue = requestQueues[i];
					
					EnqueItem(queueItem);
					dataItem = NULL;
					queueItem = NULL;
				}
				
			}
			
		//Step 2, proccess any responses
		pthread_mutex_lock(&work_mutex);
		for(i = 0; i < spe_thread_count; i++)
			if (!queue_empty(requestQueues[i]))
			{
				dataItem = queue_deq(requestQueues[i]);
				pthread_mutex_unlock(&work_mutex);
				
				datatype = ((unsigned char*)dataItem)[0];
				switch(datatype)
				{
					case PACKAGE_ACQUIRE_RESPONSE:
						if (spe_in_mbox_write(spe_threads[i], &datatype, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], &((struct acquireResponse*)datatype)->requestID, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], (unsigned int*)&((struct acquireResponse*)datatype)->dataSize, 2, SPE_MBOX_ANY_NONBLOCKING) != 2)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], (unsigned int*)&((struct acquireResponse*)datatype)->data, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						break;
					case PACKAGE_MIGRATION_RESPONSE:
						break;
					case PACKAGE_RELEASE_RESPONSE:
						if (spe_in_mbox_write(spe_threads[i], &datatype, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], &((struct releaseResponse*)datatype)->requestID, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						break;
					case PACKAGE_NACK:
						if (spe_in_mbox_write(spe_threads[i], &datatype, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], &((struct NACK*)datatype)->requestID, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], &((struct NACK*)datatype)->hint, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						break;
					case PACKAGE_INVALIDATE_RESPONSE:
						if (spe_in_mbox_write(spe_threads[i], &datatype, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						if (spe_in_mbox_write(spe_threads[i], &((struct invalidateResponse*)datatype)->requestID, 1, SPE_MBOX_ANY_NONBLOCKING) != 1)
							perror("MBOX write error");
						break;
					default:
						perror("Bad Coordinator response");
						break;
				}
				
				free(dataItem);
								
				pthread_mutex_lock(&work_mutex);
			}
		
		pthread_mutex_unlock(&work_mutex);
	}
	
	//Returning the unused argument removes a warning
	return data;
}

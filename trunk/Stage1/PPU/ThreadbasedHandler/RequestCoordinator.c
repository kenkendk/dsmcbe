/*
 *
 * This module contains implementation code for
 * Coordinating requests from various sources
 *
 */
 
#include "RequestCoordinator.h"

volatile int terminate;

pthread_mutex_t queue_mutex;
pthread_cond_t queue_ready;
queue bagOfTasks = NULL;
pthread_t workthread;


void* ProccessWork(void* data);

void TerminateCoordinator(int force)
{
	int queueEmpty;
	
	if (force)
		terminate = 1;
		
	queueEmpty = 0;
	while(!queueEmpty)
	{
	 	pthread_mutex_lock(&queue_mutex);
	 	queueEmpty = queue_empty(bagOfTasks);
	 	if (queueEmpty)
	 	{
	 		terminate = 1;
	 		pthread_cond_signal(&queue_ready);
	 	}
	 	pthread_mutex_unlock(&queue_mutex);
	}
		
	pthread_join(workthread, NULL);
	
	pthread_mutex_destroy(&queue_mutex);
	pthread_cond_destroy(&queue_ready);
}

void InitializeCoordinator()
{
	pthread_attr_t attr;

	if (bagOfTasks == NULL)
	{
		bagOfTasks = queue_create();
		terminate = 0;
	
		/* Initialize mutex and condition variable objects */
		pthread_mutex_init(&queue_mutex, NULL);
		pthread_cond_init (&queue_ready, NULL);

		/* For portability, explicitly create threads in a joinable state */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_create(&workthread, &attr, ProccessWork, NULL);
		pthread_attr_destroy(&attr);
	}
}
 
void EnqueItem(QueueableItem item)
{
 	pthread_mutex_lock(&queue_mutex);
 	
 	queue_enq(bagOfTasks, (void*)item);
 	
 	pthread_cond_signal(&queue_ready);
 	pthread_mutex_unlock(&queue_mutex);
}



void* ProccessWork(void* data)
{
	QueueableItem item;
	unsigned int datatype;
	
	while(!terminate)
	{
		pthread_mutex_lock(&queue_mutex);
		if (queue_empty(bagOfTasks))
			pthread_cond_wait(&queue_ready, &queue_mutex);
		item = (QueueableItem)queue_deq(bagOfTasks);
		pthread_mutex_unlock(&queue_mutex);	
		
		datatype = ((unsigned char*)item->dataRequest)[0];
		switch(datatype)
		{
			case PACKAGE_ACQUIRE_REQUEST_READ:
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				break;
			
			case PACKAGE_RELEASE_REQUEST:
				break;
			
			case PACKAGE_INVALIDATE_REQUEST:
				break;
			
		};	
		
		//TODO: Remember to free(item) and free(item->dataRequest) when it is no longer needed
	}
	
	//Returning the unused argument removes a warning
	return data;
}

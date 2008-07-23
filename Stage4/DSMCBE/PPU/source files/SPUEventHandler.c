/*
 *
 * This module contains code that handles requests 
 * from SPE units via Mailbox messages
 *
 */
 
#define USE_INTR_MBOX 
//#define EVENT_BASED

#ifdef EVENT_BASED
	#define OUTBOUND_BEGIN pthread_mutex_lock(&spu_outbound_mutex);
	#define OUTBOUND_END pthread_cond_signal(&spu_outbound_cond); pthread_mutex_unlock(&spu_outbound_mutex);
	
	#define LEASE_BEGIN pthread_mutex_lock(&spu_lease_lock);
	#define LEASE_END pthread_mutex_unlock(&spu_lease_lock);
#else
	#define OUTBOUND_BEGIN
	#define OUTBOUND_END

	#define LEASE_BEGIN
	#define LEASE_END
#endif

#include <pthread.h>
#include "../header files/SPUEventHandler.h"
#include "../../common/datastructures.h"
#include "../header files/RequestCoordinator.h"

#include "../../common/debug.h"

int lessint(void* a, void* b);
int hashfc(void* a, unsigned int count);

struct DMAtranfersCompleteStruct 
{
	volatile unsigned int status;
	unsigned int requestID;
};

typedef struct DMAtranfersCompleteStruct DMAtranfersComplete;

//This is used to terminate the thread
volatile int spu_terminate;

//This mutex and condition is used to signal and protect incoming data from the requestCoordinator
pthread_mutex_t spu_requestCoordinator_mutex;

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


//This table contains list with completed DMA transfers, key is GUID, value is 1
hashtable* spu_InCompleteDMAtransfers;

DMAtranfersComplete** DMAtransfers;
unsigned int* DMAtransfersCount;

int* spe_isAlive;

#ifdef EVENT_BASED

	//This thread handles all events from the SPU's
	pthread_t spu_eventthread;
	
	//This thread handles all inbound message from the SPU
	pthread_t spu_inbound_thread;
	
	//This thread handles all outbound message to the SPU
	pthread_t spu_outbound_thread;
	
	//This thread handles all messages from the request coordinator
	pthread_t spu_requestCoordinator_thread;

	//This mutex and condition is used to signal inbound messages from the spu
	pthread_mutex_t spu_inbound_mutex;
	pthread_cond_t spu_inbound_cond;

	//This mutex and condition is used to signal and protect the outbound spu message queue
	pthread_mutex_t spu_outbound_mutex;
	pthread_cond_t spu_outbound_cond;

	//This condition is used to signal from the request coordinator
	pthread_cond_t spu_requestCoordinator_cond;

	//This lock protects the lease table and the WriteInitiator table
	pthread_mutex_t spu_lease_lock;

	//The worker thread for SPU events
	void* SPU_EventThread(void* data);
	
	//The worker thread for all messages from the request coordinator
	void* SPU_RequestCoordinatorThread(void* data);
	
	//The worker thread for all messages from the SPU
	void* SPU_InboundThread(void* data);
	
	//The worker thread for all messages to the SPU
	void* SPU_OutboundThread(void* data);
	
	//Each call to wait should not return more than this many events
	#define SPE_MAX_EVENT_COUNT 100
	
	//This is the main SPE event handler
	spe_event_handler_ptr_t spu_event_handler;
	
	//This is the registered SPE events
	spe_event_unit_t* registered_events;
#else
	pthread_t spu_mainthread;
	void* SPU_MainThread(void* data);
#endif

void TerminateSPUHandler(int force)
{
	size_t i, j;
	//Remove warning about unused parameter
	spu_terminate = force ? 1 : 1;
	
	if (spe_thread_count == 0)
		return;

#ifdef EVENT_BASED	
	//Notify all threads that they are done
	pthread_mutex_lock(&spu_inbound_mutex);
	pthread_cond_signal(&spu_inbound_cond);
	pthread_mutex_unlock(&spu_inbound_mutex);

	pthread_mutex_lock(&spu_outbound_mutex);
	pthread_cond_signal(&spu_outbound_cond);
	pthread_mutex_unlock(&spu_outbound_mutex);

	pthread_mutex_lock(&spu_requestCoordinator_mutex);
	pthread_cond_signal(&spu_requestCoordinator_cond);
	pthread_mutex_unlock(&spu_requestCoordinator_mutex);

	//Wait for them to terminate
	pthread_join(spu_eventthread, NULL);
	pthread_join(spu_inbound_thread, NULL);
	pthread_join(spu_outbound_thread, NULL);
	pthread_join(spu_requestCoordinator_thread, NULL);
#else
	pthread_join(spu_mainthread, NULL);
#endif

	//Destroy all data variables
#ifdef EVENT_BASED
	pthread_mutex_destroy(&spu_inbound_mutex);
	pthread_cond_destroy(&spu_inbound_cond);
	pthread_mutex_destroy(&spu_outbound_mutex);
	pthread_cond_destroy(&spu_outbound_cond);
	pthread_mutex_destroy(&spu_lease_lock);

	pthread_cond_destroy(&spu_requestCoordinator_cond);
#endif

	pthread_mutex_destroy(&spu_requestCoordinator_mutex);
	
	for(i = 0; i < spe_thread_count; i++)
	{
		queue_destroy(spu_mailboxQueues[i]);
		queue_destroy(spu_requestQueues[i]);
		UnregisterInvalidateSubscriber(&spu_mailboxQueues[i]);
	}


	for(i = 0; i < spe_thread_count; i++)
	{	
		ht_destroy(spu_InCompleteDMAtransfers[i]);
	}
	
	for(i = 0; i < spe_thread_count; i++)
	{	
		for(j= 0; j < 32; j++)
		{
			FREE(DMAtransfers[(i * 32) + j]);	
		}
	}
	
	FREE(DMAtransfers);
	FREE(DMAtransfersCount);

#ifdef EVENT_BASED
	for(i = 0; i < spe_thread_count; i++)
		spe_event_handler_deregister(spu_event_handler, &registered_events[i]);
	
	FREE(registered_events);

	spe_event_handler_destroy(spu_event_handler);
#endif
		
	ht_destroy(spu_leaseTable);
	ht_destroy(spu_writeInitiator);
	FREE(spu_requestQueues);
	spu_requestQueues = NULL;
	FREE(spu_mailboxQueues);
	spu_mailboxQueues = NULL;
	FREE(spe_isAlive);
}

void InitializeSPUHandler(spe_context_ptr_t* threads, unsigned int thread_count)
{
	size_t i, j;
	
	pthread_attr_t attr;
	spu_terminate = 0;

	spe_thread_count = thread_count;
	spe_threads = threads;

	if (spe_thread_count == 0)
		return;
		
	/* Setup queues */
	//printf(WHERESTR "Starting SPU event handler\n", WHEREARG);

	if((spu_requestQueues = (queue*)MALLOC(sizeof(queue) * spe_thread_count)) == NULL)
		perror("malloc error");
		
	if((spu_mailboxQueues = (queue*)MALLOC(sizeof(queue) * spe_thread_count)) == NULL)
		perror("malloc error");;

	if((spu_InCompleteDMAtransfers = (hashtable*)MALLOC(sizeof(hashtable) * spe_thread_count)) == NULL)
		perror("malloc error");
	
	/* Initialize mutex and condition variable objects */
#ifdef EVENT_BASED
	if (pthread_mutex_init(&spu_inbound_mutex, NULL) != 0) REPORT_ERROR("pthread_mutex_init");
	if (pthread_cond_init (&spu_inbound_cond, NULL) != 0) REPORT_ERROR("pthread_cond_init");
	if (pthread_mutex_init(&spu_outbound_mutex, NULL) != 0) REPORT_ERROR("pthread_mutex_init");
	if (pthread_cond_init (&spu_outbound_cond, NULL) != 0) REPORT_ERROR("pthread_cond_init");
	if (pthread_mutex_init(&spu_lease_lock, NULL) != 0) REPORT_ERROR("pthread_mutex_init");

	if (pthread_cond_init (&spu_requestCoordinator_cond, NULL) != 0) REPORT_ERROR("pthread_cond_init");
#endif

	if (pthread_mutex_init(&spu_requestCoordinator_mutex, NULL) != 0) REPORT_ERROR("pthread_mutex_init");

	if ((spe_isAlive = MALLOC(sizeof(int) * spe_thread_count)) == NULL)
		REPORT_ERROR("malloc error");  

	for(i = 0; i < spe_thread_count; i++)
	{
		spu_requestQueues[i] = queue_create();
		spu_mailboxQueues[i] = queue_create();
		spe_isAlive[i] = 1;
		
#ifdef EVENT_BASED		
		RegisterInvalidateSubscriber(&spu_requestCoordinator_mutex, &spu_requestCoordinator_cond, &spu_requestQueues[i]);
#else
		RegisterInvalidateSubscriber(&spu_requestCoordinator_mutex, NULL, &spu_requestQueues[i]);
#endif
	}

	/* Setup the lease table */
	spu_leaseTable = ht_create(10, lessint, hashfc);
	spu_writeInitiator = ht_create(10, lessint, hashfc);
	
	//printf(WHERESTR "Starting init\n", WHEREARG);	   
	
	// Setup DMAtransfer array
	DMAtransfersCount = MALLOC(sizeof(unsigned int) * spe_thread_count);
	DMAtransfers = MALLOC(sizeof(void*) * spe_thread_count * 32);
	for(i = 0; i < spe_thread_count; i++)
	{
		spu_InCompleteDMAtransfers[i] = ht_create(50, lessint, hashfc);
		for(j= 0; j < 32; j++)
			DMAtransfers[(i * 32) + j] = MALLOC_ALIGN(sizeof(DMAtranfersComplete), 7);
	}

	//printf(WHERESTR "Creating SPU event handler\n", WHEREARG);

#ifdef EVENT_BASED
	spu_event_handler = spe_event_handler_create();
	//printf(WHERESTR "Created SPU event handler\n", WHEREARG);
	if (spu_event_handler == NULL)
		REPORT_ERROR("Broken event handler");
		
	//printf(WHERESTR "Created SPU event handler\n", WHEREARG);
	if ((registered_events = malloc(sizeof(spe_event_unit_t) * spe_thread_count)) == NULL)
		REPORT_ERROR("malloc error");
		
	//printf(WHERESTR "Created SPU event handler\n", WHEREARG);
	for(i = 0; i < spe_thread_count; i++)
	{
		//The SPE_EVENT_IN_MBOX is enabled whenever there is data in the queue
		//printf(WHERESTR "Created SPU event handler\n", WHEREARG);
		registered_events[i].spe = spe_threads[i];
		registered_events[i].events = SPE_EVENT_OUT_INTR_MBOX;
		registered_events[i].data.ptr = 0;

		if (spe_event_handler_register(spu_event_handler, &registered_events[i]) != 0)
			REPORT_ERROR("Register failed");
	}
#endif
		
	//printf(WHERESTR "Starting SPU event handler threads\n", WHEREARG);

	/* For portability, explicitly create threads in a joinable state */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	
#ifdef EVENT_BASED	
	pthread_create(&spu_eventthread, &attr, SPU_EventThread, NULL);
	pthread_create(&spu_inbound_thread, &attr, SPU_InboundThread, NULL);
	pthread_create(&spu_outbound_thread, &attr, SPU_OutboundThread, NULL);
	pthread_create(&spu_requestCoordinator_thread, &attr, SPU_RequestCoordinatorThread, NULL);
#else
	pthread_create(&spu_mainthread, &attr, SPU_MainThread, NULL);
#endif
	//printf(WHERESTR "Done Init\n", WHEREARG);
	
	pthread_attr_destroy(&attr);
}

void ReadMBOXBlocking(spe_context_ptr_t spe, unsigned int* target, unsigned int count)
{
#ifdef USE_INTR_MBOX
	//printf(WHERESTR "Reading %i MBOX messages\r\n", WHEREARG, count);
	if (spe_out_intr_mbox_read(spe, target, count, SPE_MBOX_ALL_BLOCKING) != (int)count)
		REPORT_ERROR("Failed to read all messages, even though BLOCKING was on");
	//printf(WHERESTR "Read %i MBOX messages\r\n", WHEREARG, count);
#else
	unsigned int readcount;
	while(count > 0)
	{
		while (spe_out_mbox_status(spe) == 0)
			;
		readcount = spe_out_mbox_read(spe, target, count);
		count -= readcount;
		target = &target[readcount];		
	}
#endif
}

int SPU_ProcessOutboundMessages()
{
	int isFull = 0;
	unsigned int i;
	
	for(i = 0; i < spe_thread_count; i++)
	{
		while (!queue_empty(spu_mailboxQueues[i]) && spe_in_mbox_status(spe_threads[i]) != 0)	
		{
			//printf(WHERESTR "Sending Mailbox message: %i\n", WHEREARG, (unsigned int)spu_mailboxQueues[i]->head->element);
			if (spe_in_mbox_write(spe_threads[i], (unsigned int*)&spu_mailboxQueues[i]->head->element, 1, SPE_MBOX_ALL_BLOCKING) != 1) {
				REPORT_ERROR("Failed to send message, even though it was blocking!"); 
			} else
				queue_deq(spu_mailboxQueues[i]);
		}
		
		if (!queue_empty(spu_mailboxQueues[i]))
			isFull = 1;		
	}
	
	return isFull;
}

void* SPU_GetRequestCoordinatorMessage(int* threadNo)
{
	int i;
	*threadNo = -1;

	for(i = 0; i < (int)spe_thread_count; i++)
		if (!queue_empty(spu_requestQueues[i]))
		{
			*threadNo = i;
			return queue_deq(spu_requestQueues[i]);
		}
	return NULL;		
}

void SPU_ProcessRequestCoordinatorMessage(void* dataItem, int threadNo)
{
	unsigned int datatype;
	unsigned int requestID;
	GUID itemid;
	int sendMessage;
	int initiatorNo;
	DMAtranfersComplete* DMAobj;

	if (dataItem != NULL && threadNo != -1)
	{
		datatype = ((unsigned int*)dataItem)[0];
		//printf(WHERESTR "Got response from Coordinator\n", WHEREARG);
		switch(datatype)
		{
			case PACKAGE_ACQUIRE_RESPONSE:
				//printf(WHERESTR "Got acquire response message, converting to MBOX messages (%d:%d), mode: %d\n", WHEREARG, (int)((struct acquireResponse*)dataItem)->data, (int)((struct acquireResponse*)dataItem)->dataSize, ((struct acquireResponse*)dataItem)->mode);
				
				//Register this ID for the current SPE
				LEASE_BEGIN
				itemid = ((struct acquireResponse*)dataItem)->dataItem;
				if (!ht_member(spu_leaseTable,  (void*)itemid))
					ht_insert(spu_leaseTable, (void*)itemid, slset_create(lessint));
				if (!slset_member((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)threadNo))
					slset_insert((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)threadNo, NULL);
				LEASE_END

				OUTBOUND_BEGIN
				queue_enq(spu_mailboxQueues[threadNo], (void*)datatype);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct acquireResponse*)dataItem)->requestID);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct acquireResponse*)dataItem)->dataItem);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct acquireResponse*)dataItem)->mode);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct acquireResponse*)dataItem)->dataSize);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct acquireResponse*)dataItem)->data);
				OUTBOUND_END
				
				//Register this SPU as the initiator
				if (((struct acquireResponse*)dataItem)->mode != ACQUIRE_MODE_READ)
				{
					//printf(WHERESTR "Registering SPU %d as initiator for package %d\n", WHEREARG, threadNo, itemid);
					LEASE_BEGIN
					if (ht_member(spu_writeInitiator, (void*)itemid)) {
						REPORT_ERROR("Same SPU was registered twice for write");
					} else {							
						ht_insert(spu_writeInitiator, (void*)itemid, (void*)threadNo);
					}
					LEASE_END
				}
				else
				{
					
					LEASE_BEGIN
					if(!ht_member(spu_InCompleteDMAtransfers[threadNo], (void*)((struct acquireResponse*)dataItem)->dataItem))
					{
						//printf(WHERESTR "Value: %i\n", WHEREARG, ((struct acquireResponse*)dataItem)->dataItem);
						DMAtransfersCount[threadNo] = (DMAtransfersCount[threadNo] + 1) % 32;
						DMAobj = DMAtransfers[(threadNo * 32) + DMAtransfersCount[threadNo]];
						DMAobj->status = 1;
						DMAobj->requestID = UINT_MAX;
					
						ht_insert(spu_InCompleteDMAtransfers[threadNo], (void*)((struct acquireResponse*)dataItem)->dataItem, DMAobj);
						LEASE_END
						
						OUTBOUND_BEGIN
						queue_enq(spu_mailboxQueues[threadNo], DMAobj);
						OUTBOUND_END
					}
					else								
					{
						LEASE_END
						//printf(WHERESTR "Value: %i\n", WHEREARG, ((struct acquireResponse*)dataItem)->dataItem);
						REPORT_ERROR("Could not insert into Incomplete DMA transfers HT");
					}
				}
				
				break;
			case PACKAGE_MIGRATION_RESPONSE:
				//printf(WHERESTR "Got migration response message, converting to MBOX messages\n", WHEREARG);
				break;
			case PACKAGE_RELEASE_RESPONSE:
				//printf(WHERESTR "Got acquire release message, converting to MBOX messages\n", WHEREARG);
				OUTBOUND_BEGIN
				
				queue_enq(spu_mailboxQueues[threadNo], (void*)datatype);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct releaseResponse*)dataItem)->requestID);
				
				OUTBOUND_END
				break;
			case PACKAGE_NACK:
				//printf(WHERESTR "Got acquire nack message, converting to MBOX messages\n", WHEREARG);
				OUTBOUND_BEGIN

				queue_enq(spu_mailboxQueues[threadNo], (void*)datatype);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct NACK*)dataItem)->requestID);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct NACK*)dataItem)->hint);

				OUTBOUND_END
				break;
			case PACKAGE_INVALIDATE_REQUEST:
			
				itemid = ((struct invalidateRequest*)dataItem)->dataItem;
				requestID = ((struct invalidateRequest*)dataItem)->requestID;
				sendMessage = 0;
				
				LEASE_BEGIN
				if (!ht_member(spu_leaseTable,  (void*)itemid))
					ht_insert(spu_leaseTable, (void*)itemid, slset_create(lessint));
					
				
				if ((spe_isAlive[threadNo] && slset_member((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)threadNo)))
				{
					initiatorNo = -1;
					if (ht_member(spu_writeInitiator, (void*)((struct invalidateRequest*)dataItem)->dataItem))
						initiatorNo = (int)ht_get(spu_writeInitiator, (void*)((struct invalidateRequest*)dataItem)->dataItem);
					
					if ((int)threadNo != initiatorNo)
					{
						//printf(WHERESTR "Got \"invalidateRequest\" message, converting to MBOX messages for %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, threadNo);
						sendMessage = 1;
													
						//printf(WHERESTR "Forwarding invalidate for id %d to SPU %d\n", WHEREARG, itemid, i);
						slset_delete((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)threadNo);
						
						if(!ht_member(spu_InCompleteDMAtransfers[threadNo], (void*)itemid))
						{
							//printf(WHERESTR "Sending invaliResp ID into thing %i %i %i\n", WHEREARG, requestID, threadNo, initiatorNo);
							EnqueInvalidateResponse(requestID);
						}
						else
						{
							//printf(WHERESTR "Inserting request ID into thing %i %i %i\n", WHEREARG, requestID, threadNo, initiatorNo);
							DMAobj = ht_get(spu_InCompleteDMAtransfers[threadNo], (void*)itemid);
							DMAobj->requestID = requestID;
						}
					}
					else
					{
						//printf(WHERESTR "Got \"invalidateRequest\" message, but skipping because SPU is initiator, ID %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, threadNo);
						EnqueInvalidateResponse(requestID);
						ht_delete(spu_writeInitiator, (void*)((struct invalidateRequest*)dataItem)->dataItem);
					}
				}
				else
				{
					//printf(WHERESTR "Got \"invalidateRequest\" message, but skipping because SPU does not have data, ID %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, threadNo);
					EnqueInvalidateResponse(requestID);
				}
				LEASE_END
				
				if (sendMessage)
				{
					//printf(WHERESTR "Got \"invalidateRequest\" message, converting to MBOX messages for %d, SPU %d\n", WHEREARG, ((struct invalidateRequest*)dataItem)->dataItem, i);
					OUTBOUND_BEGIN

					queue_enq(spu_mailboxQueues[threadNo], (void*)datatype);
					queue_enq(spu_mailboxQueues[threadNo], (void*)((struct invalidateRequest*)dataItem)->requestID);
					queue_enq(spu_mailboxQueues[threadNo], (void*)((struct invalidateRequest*)dataItem)->dataItem);

					OUTBOUND_END
				}
				
				
				break;
			case PACKAGE_WRITEBUFFER_READY:
				//printf(WHERESTR "Sending WRITEBUFFER_READY to SPU %d\n", WHEREARG, threadNo);
				// Could try to send a signal instead of at mailbox!
				//printf("Sending signal\n");
				//spe_signal_write(spe_threads[threadNo], SPE_SIG_NOTIFY_REG_1, ((struct writebufferReady*)dataItem)->dataItem);
				//printf("Signal send\n");
				OUTBOUND_BEGIN

				queue_enq(spu_mailboxQueues[threadNo], (void*)datatype);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct writebufferReady*)dataItem)->requestID);
				queue_enq(spu_mailboxQueues[threadNo], (void*)((struct writebufferReady*)dataItem)->dataItem);						

				OUTBOUND_END
				break;
			default:						
				perror("SPUEventHandler.c: Bad Coordinator response");
				break;
		}
		
		FREE(dataItem);
		dataItem = NULL;			
	}
}


int SPU_GetWaitingMailbox()
{
	int i;

	for(i = 0; i < (int)spe_thread_count; i++)
#ifdef USE_INTR_MBOX	
		if (spe_isAlive[i] && spe_out_intr_mbox_status(spe_threads[i]) != 0)
#else
		if (spe_isAlive[i] && spe_out_mbox_status(spe_threads[i]) != 0)
#endif
			return i;

	return -1;
}

void SPU_ProcessIncommingMailbox(int threadNo)
{
	unsigned int datatype;
	unsigned int requestID;
	unsigned long datasize;
	void* datapointer;
	GUID itemid;
	void* dataItem = NULL;
	QueueableItem queueItem;
	int mode;
	DMAtranfersComplete* DMAobj;

	if (threadNo != -1)
	{
		datatype = 8000;
		dataItem = NULL;
		//printf(WHERESTR "SPU mailbox message detected\n", WHEREARG);
	#ifdef USE_INTR_MBOX
		if (spe_out_intr_mbox_read(spe_threads[threadNo], &datatype, 1, SPE_MBOX_ALL_BLOCKING) != 1)
	#else
		ReadMBOXBlocking(spe_threads[threadNo], &datatype, 1);	
	#endif
			REPORT_ERROR("Read MBOX failed (1)!");
		//printf(WHERESTR "SPU mailbox message read\n", WHEREARG);
			
		switch(datatype)
		{					
			case PACKAGE_TERMINATE_REQUEST:
				OUTBOUND_BEGIN
				queue_enq(spu_mailboxQueues[threadNo], (void*)PACKAGE_TERMINATE_RESPONSE);
				spe_isAlive[threadNo] = 0;
				OUTBOUND_END
				break;
			case PACKAGE_CREATE_REQUEST:
				//printf(WHERESTR "Create recieved\n", WHEREARG);
				if ((dataItem = MALLOC(sizeof(struct createRequest))) == NULL)
					REPORT_ERROR("malloc error");;
				ReadMBOXBlocking(spe_threads[threadNo], &requestID, 1);
				ReadMBOXBlocking(spe_threads[threadNo], &itemid, 1);
				ReadMBOXBlocking(spe_threads[threadNo], (unsigned int*)&datasize, 1);
										
				((struct createRequest*)dataItem)->dataItem = itemid;
				((struct createRequest*)dataItem)->packageCode = datatype;
				((struct createRequest*)dataItem)->requestID = requestID;
				((struct createRequest*)dataItem)->dataSize = datasize;
				break;
				
			case PACKAGE_ACQUIRE_REQUEST_READ:
				//printf(WHERESTR "Acquire READ recieved\n", WHEREARG);
			case PACKAGE_ACQUIRE_REQUEST_WRITE:
				//printf(WHERESTR "Acquire WRITE recieved\n", WHEREARG);
				if ((dataItem = MALLOC(sizeof(struct acquireRequest))) == NULL)
					REPORT_ERROR("malloc error");
				ReadMBOXBlocking(spe_threads[threadNo], &requestID, 1);
				ReadMBOXBlocking(spe_threads[threadNo], &itemid, 1);
											
				((struct acquireRequest*)dataItem)->dataItem = itemid;
				((struct acquireRequest*)dataItem)->packageCode = datatype;
				((struct acquireRequest*)dataItem)->requestID = requestID;
				//((struct acquireRequest*)dataItem)->spe = spe_threads[threadNo];
				//printf(WHERESTR "Got %d, %d, %d\n", WHEREARG, itemid, datatype, requestID);
				break;
				
			case PACKAGE_RELEASE_REQUEST:
				//printf(WHERESTR "Release recieved\n", WHEREARG);
				ReadMBOXBlocking(spe_threads[threadNo], &requestID, 1);
				ReadMBOXBlocking(spe_threads[threadNo], &itemid, 1);
				ReadMBOXBlocking(spe_threads[threadNo], (unsigned int*)&mode, 1);
				ReadMBOXBlocking(spe_threads[threadNo], (unsigned int*)&datasize, 1);
				ReadMBOXBlocking(spe_threads[threadNo], (unsigned int*)&datapointer, 1);

				//Only forward write releases
				if (mode == ACQUIRE_MODE_WRITE)
				{
					//printf(WHERESTR "Release recieved for WRITE, forwarding request and registering initiator\n", WHEREARG);
					if ((dataItem = MALLOC(sizeof(struct releaseRequest))) == NULL)
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
					LEASE_BEGIN
					//The release request implies that the sender has destroyed the copy
					if (!ht_member(spu_leaseTable, (void*)itemid))
						ht_insert(spu_leaseTable, (void*)itemid, slset_create(lessint));
					if (slset_member((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)threadNo))
						slset_delete((slset)ht_get(spu_leaseTable, (void*)itemid), (void*)threadNo);
					//printf(WHERESTR "Release recieved for READ %d, unregistering requestor %d\n", WHEREARG, itemid, threadNo);
					LEASE_END
				}
				
				break;
	
			case PACKAGE_INVALIDATE_RESPONSE:
				//printf(WHERESTR "Invalidate Response\n", WHEREARG);

				ReadMBOXBlocking(spe_threads[threadNo], &requestID, 1);
				EnqueInvalidateResponse(requestID);
				
				break;
			case PACKAGE_DMA_TRANSFER_COMPLETE:
				//printf(WHERESTR "DMA Transfer complete\n", WHEREARG);
				ReadMBOXBlocking(spe_threads[threadNo], &itemid, 1);
				
				LEASE_BEGIN
				if (ht_member(spu_InCompleteDMAtransfers[threadNo], (void*)itemid))
				{
					DMAobj = ht_get(spu_InCompleteDMAtransfers[threadNo], (void*)itemid);
					ht_delete(spu_InCompleteDMAtransfers[threadNo], (void*)itemid);

					if (DMAobj->requestID != UINT_MAX)
						EnqueInvalidateResponse(DMAobj->requestID);
				}
				LEASE_END
				break;				
			default:
				fprintf(stderr, WHERESTR "Bad SPU request, ID was: %i, message: %s\n", WHEREARG, datatype, strerror(errno));
				break;
		}
		
		//If the request is valid and should be forwarded, do so
		if (dataItem != NULL)
		{
			queueItem = (QueueableItem)MALLOC(sizeof(struct QueueableItemStruct));
			queueItem->dataRequest = dataItem;
#ifdef EVENT_BASED			
			queueItem->event = &spu_requestCoordinator_cond;
#else
			queueItem->event = NULL;
#endif
			queueItem->mutex = &spu_requestCoordinator_mutex;
			queueItem->queue = &spu_requestQueues[threadNo];
			
			//printf(WHERESTR "Got message from SPU, enqued as %i\n", WHEREARG, (int)queueItem);
			
			EnqueItem(queueItem);
			//printf(WHERESTR "Got message from SPU, mutex is %i\n", WHEREARG, (int)queueItem->mutex);
			dataItem = NULL;
			queueItem = NULL;
		}
		
	}
}

#ifdef EVENT_BASED
/* This thread function converts all incomming messages to mailbox messages, and forwards them */
void* SPU_RequestCoordinatorThread(void* dummy)
{
	void* dataItem;
	int threadNo;
	
	//printf(WHERESTR "Starting request coordinator\n", WHEREARG);
	
	while(!spu_terminate)
	{
		pthread_mutex_lock(&spu_requestCoordinator_mutex);
		
		dataItem = NULL;
		
		while(dataItem == NULL && !spu_terminate)
		{ 
			dataItem = SPU_GetRequestCoordinatorMessage(&threadNo);
			if (dataItem == NULL && !spu_terminate)
			{
				//printf(WHERESTR "Waiting request coordinator\n", WHEREARG);
				pthread_cond_wait(&spu_requestCoordinator_cond, &spu_requestCoordinator_mutex);
			}
		}
			
		pthread_mutex_unlock(&spu_requestCoordinator_mutex);
		//printf(WHERESTR "Processing request coordinator\n", WHEREARG);
		
		SPU_ProcessRequestCoordinatorMessage(dataItem, threadNo);
	}
	
	return dummy;
}

/* This thread function basically extends the size of the mailbox buffer from 4 messages to nearly infinite */
void* SPU_OutboundThread(void* dummy)
{
	struct timespec waittime;
	
	//printf(WHERESTR "Starting outbound\n", WHEREARG);
	
	pthread_mutex_lock(&spu_outbound_mutex);

	while(!spu_terminate)
	{
		//Atomically unlock mutex and wait for a signal
		//Unfortunately the SPE_EVENT_IN_MBOX keeps fireing if there is no messages,
		// so we have to rely on a timed wait
		
		if (SPU_ProcessOutboundMessages())
		{
			//printf(WHERESTR "Waiting for outbound, TIMED\n", WHEREARG);
			clock_gettime(CLOCK_REALTIME, &waittime);
			waittime.tv_nsec += 10000000;
			pthread_cond_timedwait(&spu_outbound_cond, &spu_outbound_mutex, &waittime);
		}
		else
		{
			//printf(WHERESTR "Waiting for outbound: %d\n", WHEREARG, isFull);
			pthread_cond_wait(&spu_outbound_cond, &spu_outbound_mutex);
		}
		//printf(WHERESTR "Processing outbound\n", WHEREARG);
	}

	pthread_mutex_unlock(&spu_outbound_mutex);
	
	return dummy;
}


/* This thread function reads mailbox messages and forwards them as packages to the request coordinator */
void* SPU_InboundThread(void* dummy)
{
	int threadNo;
	
	//printf(WHERESTR "Starting inbound\n", WHEREARG);

	while(!spu_terminate)
	{
		if (pthread_mutex_lock(&spu_inbound_mutex) != 0) REPORT_ERROR("pthread_mutex_lock");

		threadNo = SPU_GetWaitingMailbox();
		
		if (threadNo == -1 && !spu_terminate)
		{
			//printf(WHERESTR "Waiting inbound\n", WHEREARG);
			if (pthread_cond_wait(&spu_inbound_cond, &spu_inbound_mutex) != 0) REPORT_ERROR("pthread_cond_wait");
			//printf(WHERESTR "Processing inbound\n", WHEREARG);
		}
		
		if (pthread_mutex_unlock(&spu_inbound_mutex) != 0) REPORT_ERROR("pthread_mutex_unlock");
		
		SPU_ProcessIncommingMailbox(threadNo);
	}
	
	return dummy;
}

/* This thread function listens to SPU events and converts these events to pthread condition signals instead */
void* SPU_EventThread(void* dummy)
{
	int event_count;
	spe_event_unit_t event;
	
	//printf(WHERESTR "Registering events\n", WHEREARG);
	
	//Events may already have occured here, so we fake signal it
	pthread_mutex_lock(&spu_inbound_mutex);
	pthread_cond_signal(&spu_inbound_cond);
	pthread_mutex_unlock(&spu_inbound_mutex);

	pthread_mutex_lock(&spu_outbound_mutex);
	pthread_cond_signal(&spu_outbound_cond);
	pthread_mutex_unlock(&spu_outbound_mutex);
	 
	
	while(!spu_terminate)
	{
		//The timeout is set to 5000 so we check the spu_terminate flag every 5 seconds, 
		// in case nothing else happens
	
		//printf(WHERESTR "Awaiting event\n", WHEREARG);
		event_count = spe_event_wait(spu_event_handler, &event, SPE_MAX_EVENT_COUNT, 10000);
		//printf(WHERESTR "Processing event\n", WHEREARG);
		if (event_count == -1)
			REPORT_ERROR("spe_event_wait failed");
		
		if ((event.events & SPE_EVENT_OUT_INTR_MBOX) != 0)
		{
			//printf(WHERESTR "Got OUT_MBOX event\n", WHEREARG);
			if (pthread_mutex_lock(&spu_inbound_mutex) != 0) REPORT_ERROR("pthread_mutex_lock");
			if (pthread_cond_signal(&spu_inbound_cond) != 0) REPORT_ERROR("pthread_cond_signal");
			//printf(WHERESTR "Signalled OUT_MBOX event\n", WHEREARG);
			if (pthread_mutex_unlock(&spu_inbound_mutex) != 0) REPORT_ERROR("pthread_mutex_unlock");
		}

		//This event keeps fireing if the mailbox is empty, this may have worked under SDK 2.1
		/*if ((event.events & SPE_EVENT_IN_MBOX) != 0)
		{
			//printf(WHERESTR "Got IN_MBOX event\n", WHEREARG);
			if (pthread_mutex_lock(&spu_outbound_mutex) != 0) REPORT_ERROR("pthread_mutex_lock");
			if (pthread_cond_signal(&spu_outbound_cond) != 0) REPORT_ERROR("pthread_cond_signal");
			if (pthread_mutex_unlock(&spu_outbound_mutex) != 0) REPORT_ERROR("pthread_mutex_lock");
			//printf(WHERESTR "Signalled IN_MBOX event\n", WHEREARG);
		}*/

		//This code is used to troubleshoot jiggy event handling code
		if (event_count == 0)
		{
			pthread_mutex_lock(&spu_inbound_mutex);
			pthread_cond_signal(&spu_inbound_cond);
			pthread_mutex_unlock(&spu_inbound_mutex);
		
			/*pthread_mutex_lock(&spu_outbound_mutex);
			pthread_cond_signal(&spu_outbound_cond);
			pthread_mutex_unlock(&spu_outbound_mutex);*/
			
			printf(WHERESTR "Jiggy! Jiggy! Jiggy!\n", WHEREARG);
		}
	}
	
	//Returning the unused data removes a warning
	return dummy;
}

#else

void* SPU_MainThread(void* dummy)
{
	void* dataItem;
	int threadNo;
	
	while(!spu_terminate)
	{
		pthread_mutex_lock(&spu_requestCoordinator_mutex);
		dataItem = SPU_GetRequestCoordinatorMessage(&threadNo);
		pthread_mutex_unlock(&spu_requestCoordinator_mutex);
		
		if (dataItem != NULL && threadNo != -1)
			SPU_ProcessRequestCoordinatorMessage(dataItem, threadNo);
		
		threadNo = SPU_GetWaitingMailbox();
		if (threadNo != -1)
			SPU_ProcessIncommingMailbox(threadNo);
		
		SPU_ProcessOutboundMessages();
	}
	
	return dummy;
}


#endif

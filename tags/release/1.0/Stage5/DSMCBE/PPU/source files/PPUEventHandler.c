/*
 *
 * This module contains code that handles requests 
 * from PPU units
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <glib.h>

#include "../../common/datapackages.h"
#include "../header files/PPUEventHandler.h"
#include "../header files/RequestCoordinator.h"

#include "../../common/debug.h"

//This mutex protects the pointer list
pthread_mutex_t ppu_pointer_mutex;
//This mutex protects the old pointer list
pthread_mutex_t ppu_pointerOld_mutex;
//This mutex protects the invalidate queue
pthread_mutex_t ppu_invalidate_mutex;
//This signals when an item has been released
pthread_cond_t ppu_pointerOld_cond;

//These two tables contains the registered pointers, either active or retired
GHashTable* Gppu_pointers;
GHashTable* Gppu_pointersOld;

//This is the queue of pending invalidates
GQueue* Gppu_pendingInvalidate;

volatile unsigned int ppu_terminate;

#define MAX_SEQUENCE_NUMBER 1000000
unsigned int ppu_request_sequence_number = 0;

GHashTable* ppuhandler_pendingRequests;

pthread_mutex_t ppu_queue_mutex;
pthread_cond_t ppu_queue_cond;
GQueue* Gppu_work_queue;
pthread_t ppu_dispatchthread;

GQueue* Gppu_dummy;
GQueue* Gppu_temp;
pthread_mutex_t ppu_dummy_mutex;
pthread_cond_t ppu_dummy_cond;


typedef struct PointerEntryStruct *PointerEntry;
struct PointerEntryStruct
{
	GUID id;
	void* data;
	unsigned long offset;
	unsigned long size;	
	int mode;
	unsigned int count;
};


void* ppu_requestDispatcher(void* data);

//Setup the PPUHandler
void InitializePPUHandler()
{
	
	pthread_attr_t attr;
	
	//TODO Use new_full, so it is easy to destroy!
	Gppu_pointers = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&ppu_pointer_mutex, NULL);
	
	//TODO Use new_full, so it is easy to destroy!
	Gppu_pointersOld = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&ppu_pointerOld_mutex, NULL);
	pthread_cond_init(&ppu_pointerOld_cond, NULL);

	Gppu_pendingInvalidate = g_queue_new();
	pthread_mutex_init(&ppu_invalidate_mutex, NULL);

	ppuhandler_pendingRequests = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&ppu_queue_mutex, NULL);
	pthread_cond_init(&ppu_queue_cond, NULL);	

	Gppu_work_queue = g_queue_new();	

	Gppu_temp = g_queue_new();

	Gppu_dummy = g_queue_new();
	pthread_mutex_init(&ppu_dummy_mutex, NULL);
	pthread_cond_init(&ppu_dummy_cond, NULL);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&ppu_dispatchthread, &attr, ppu_requestDispatcher, NULL);
	pthread_attr_destroy(&attr);
	
	RegisterInvalidateSubscriber(&ppu_queue_mutex, &ppu_queue_cond, &Gppu_work_queue);
}

//ppu_terminate the PPUHandler and release all resources
void ppu_terminatePPUHandler()
{
	ppu_terminate = 1;
	
	pthread_join(ppu_dispatchthread, NULL);
	
	g_hash_table_destroy(Gppu_pointers);
	g_hash_table_destroy(Gppu_pointersOld);
	
/*
	it = ht_iter_create(pointers);
	keys = queue_create();
	while(ht_iter_next(it))
	{
		pe = ht_iter_get_value(it);
		queue_enq(keys, ht_iter_get_key(it)); 
		FREE_ALIGN(pe->data);
		pe->data = NULL;
		FREE(pe);
		pe = NULL;
	}
	ht_iter_destroy(it);
	
	while(!queue_empty(keys))
		ht_delete(pointers, queue_deq(keys));
	
	ht_destroy(pointers);
	queue_destroy(keys);

	it = ht_iter_create(pointersOld);
	keys = queue_create();
	while(ht_iter_next(it))
	{
		pe = ht_iter_get_value(it);
		queue_enq(keys, ht_iter_get_key(it)); 
		FREE_ALIGN(pe->data);
		pe->data = NULL;
		FREE(pe);
		pe = NULL;
	}
	ht_iter_destroy(it);
	
	while(!queue_empty(keys))
		ht_delete(pointersOld, queue_deq(keys));
	
	ht_destroy(pointers);
	queue_destroy(keys);
*/	
	UnregisterInvalidateSubscriber(&Gppu_pendingInvalidate);
	
	pthread_mutex_destroy(&ppu_pointer_mutex);
	pthread_mutex_destroy(&ppu_pointerOld_mutex);
	pthread_mutex_destroy(&ppu_invalidate_mutex);
	pthread_cond_destroy(&ppu_pointerOld_cond);
	g_queue_free(Gppu_pendingInvalidate);

	g_hash_table_destroy(ppuhandler_pendingRequests);
	pthread_mutex_destroy(&ppu_queue_mutex);
	pthread_cond_destroy(&ppu_queue_cond);	
	g_queue_free(Gppu_work_queue);
	
	g_queue_free(Gppu_temp);	
	g_queue_free(Gppu_dummy);
	pthread_mutex_destroy(&ppu_dummy_mutex);
	pthread_cond_destroy(&ppu_dummy_cond);
}

void RelayEnqueItem(QueueableItem q)
{
	
	QueueableItem relay;
	
	if((relay = MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("malloc error");
		
	relay->dataRequest = q->dataRequest;
	relay->event = &ppu_queue_cond;
	relay->mutex= &ppu_queue_mutex;
	relay->Gqueue = &Gppu_work_queue;
	relay->callback = NULL;
	
	q->dataRequest = NULL;
	
	pthread_mutex_lock(relay->mutex);
	((struct createRequest*)relay->dataRequest)->requestID = NEXT_SEQ_NO(ppu_request_sequence_number, MAX_SEQUENCE_NUMBER);
	g_hash_table_insert(ppuhandler_pendingRequests, (void*)(((struct createRequest*)relay->dataRequest)->requestID), q);
	//printf(WHERESTR "Sending request with type %d, and reqId: %d\n", WHEREARG, ((struct createRequest*)relay->dataRequest)->packageCode, ((struct createRequest*)relay->dataRequest)->requestID);	
	pthread_mutex_unlock(relay->mutex);
	
	EnqueItem(relay);
}

//Sends a request into the coordinator, and awaits the response (blocking)
void* forwardRequest(void* data)
{	
	//TODO: Bad for performance to declare GQueue inside function!!	
	//GQueue* dummy;

	QueueableItem q;
	
	//printf(WHERESTR "creating item\n", WHEREARG);
	//Create the entry, this will be released by the coordinator
	if ((q = (QueueableItem)MALLOC(sizeof(struct QueueableItemStruct))) == NULL)
		REPORT_ERROR("PPUEventHandler.c: malloc error");
	
	//Gppu_dummy = g_queue_new();
	q->dataRequest = data;
	q->event = &ppu_dummy_cond;
	q->mutex = &ppu_dummy_mutex;
	q->Gqueue = &Gppu_dummy;
	q->callback = NULL;
	
	//printf(WHERESTR "Event: %i, Mutex: %i, Queue: %i\n", WHEREARG, (int)q->event, (int)q->mutex, (int)q->queue);	
	
	//printf(WHERESTR "adding item to queue\n", WHEREARG);
	RelayEnqueItem(q);
	//printf(WHERESTR "item added to queue %i\n", WHEREARG, (int)q);
	
	pthread_mutex_lock(&ppu_dummy_mutex);
	//printf(WHERESTR "locked %i\n", WHEREARG, (int)&m);


	while (g_queue_is_empty(Gppu_dummy)) {
		//printf(WHERESTR "waiting for queue %i\n", WHEREARG, (int)&e);
		pthread_cond_wait(&ppu_dummy_cond, &ppu_dummy_mutex);
		//printf(WHERESTR "queue filled\n", WHEREARG);
	}
	
	data = g_queue_pop_head(Gppu_dummy);
	pthread_mutex_unlock(&ppu_dummy_mutex);
	
	if (((struct acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireResponse*)data)->mode == ACQUIRE_MODE_WRITE) {
	
		//printf(WHERESTR "waiting for writebuffer signal\n", WHEREARG);
		
		pthread_mutex_lock(&ppu_dummy_mutex);
		while (g_queue_is_empty(Gppu_dummy)) {
			//printf(WHERESTR "waiting for writebuffer signal\n", WHEREARG);
			pthread_cond_wait(&ppu_dummy_cond, &ppu_dummy_mutex);
			//printf(WHERESTR "got for writebuffer signal\n", WHEREARG);
		}
		
		struct writebufferReady* data2 = g_queue_pop_head(Gppu_dummy);
		pthread_mutex_unlock(&ppu_dummy_mutex);
		if (data2->packageCode != PACKAGE_WRITEBUFFER_READY)
			REPORT_ERROR("Expected PACKAGE_WRITEBUFFER_READY");
		
		FREE(data2);
		data2 = NULL;
	
		pthread_mutex_unlock(&ppu_dummy_mutex);
	}
	
	if (((struct acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireResponse*)data)->mode == ACQUIRE_MODE_WRITE_OK)
			((struct acquireResponse*)data)->mode = ACQUIRE_MODE_WRITE;

	//printf(WHERESTR "returning response (%d)\n", WHEREARG, (int)data);
	
	//g_queue_free(dummy);
	if (g_queue_get_length(Gppu_dummy) > 0)
		g_queue_clear(Gppu_dummy);	
	
	return data;
}

//Record information about the returned pointer
void recordPointer(void* retval, GUID id, unsigned long size, unsigned long offset, int type)
{
	
	PointerEntry ent;
	
	if (type != ACQUIRE_MODE_READ && type != ACQUIRE_MODE_WRITE)
		REPORT_ERROR("pointer was neither READ nor WRITE");
	
	//If the response was valid, record the item data
	if (retval != NULL)
	{		
		pthread_mutex_lock(&ppu_pointer_mutex);
		
		if ((ent = g_hash_table_lookup(Gppu_pointers, retval)) != NULL)
		{
			ent->count++;
		}
		else
		{
			//printf(WHERESTR "recording entry for %d, EA: %d\n", WHEREARG, id, (int)retval);
			if ((ent = (PointerEntry)MALLOC(sizeof(struct PointerEntryStruct))) == NULL)
				REPORT_ERROR("malloc error");
			ent->data = retval;
			ent->id = id;
			ent->offset = offset;
			ent->size = size;
			ent->mode = type;
			ent->count = 1;
		}
		
		g_hash_table_insert(Gppu_pointers, retval, ent);
		pthread_mutex_unlock(&ppu_pointer_mutex);
	}	
}

//Perform a create in the current thread
void* threadCreate(GUID id, unsigned long size)
{
	
	struct createRequest* cr;
	struct acquireResponse* ar;
	void* retval;
	
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return NULL;
	}
	
	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exeeds PAGE_TABLE_SIZE");
		return NULL;
	}
	
	//printf(WHERESTR "creating structure\n", WHEREARG);
	//Create the request, this will be released by the coordinator
	if ((cr = (struct createRequest*)MALLOC(sizeof(struct createRequest))) == NULL)
		REPORT_ERROR("PPUEventHandler.c: malloc error");
	cr->packageCode = PACKAGE_CREATE_REQUEST;
	cr->requestID = 0;
	cr->dataItem = id;
	cr->dataSize = size == 0 ? 1 : size;
	
	retval = NULL;
	
	//Perform the request and await the response
	ar = (struct acquireResponse*)forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		//printf(WHERESTR "response was negative\n", WHEREARG);
		if (ar->packageCode != PACKAGE_NACK)
			REPORT_ERROR("Unexcepted response for a Create request");
	}
	else
	{
		//printf(WHERESTR "response was positive\n", WHEREARG);
		//The response was positive
		retval = ar->data;
		#if DEBUG
		if (ar->dataSize != size)
			REPORT_ERROR("Bad size returned in create");
		if (ar->requestID != 0)
			REPORT_ERROR("Bad request ID returned in create");
		#endif
	}

	//recordPointer(retval, id, size, 0, ACQUIRE_MODE_WRITE);	
	
	FREE(ar);
	ar = NULL;
	return retval;	
}

void processInvalidates(struct invalidateRequest* incoming)
{
	
	struct invalidateRequest* req;
	PointerEntry pe;
	
	//printf(WHERESTR "In processInvalidates\n", WHEREARG);
	pthread_mutex_lock(&ppu_invalidate_mutex);
	
	if (incoming != NULL)
	{
		//printf(WHERESTR "Inserted item in queue: %d, reqId: %d\n", WHEREARG, incoming->dataItem, incoming->requestID);
		g_queue_push_tail(Gppu_pendingInvalidate, incoming);
	}

	//printf(WHERESTR "Testing queue\n", WHEREARG);
	
	if (!g_queue_is_empty(Gppu_pendingInvalidate))
	{
		//printf(WHERESTR "Queue is not empty\n", WHEREARG);

		//Gppu_temp = g_queue_new();
		while(!g_queue_is_empty(Gppu_pendingInvalidate))
		{
			req = g_queue_pop_head(Gppu_pendingInvalidate);
			//printf(WHERESTR "Processing request for %d with reqId: %d\n", WHEREARG, req->dataItem, req->requestID);

			pthread_mutex_lock(&ppu_pointerOld_mutex);

			if ((pe = g_hash_table_lookup(Gppu_pointersOld, (void*)req->dataItem)) != NULL)
			{
				if (pe->count == 0 && pe->mode != ACQUIRE_MODE_BLOCKED )
				{
					g_hash_table_remove(Gppu_pointersOld, (void*)req->dataItem);
					
					FREE(pe);
					pe = NULL;
			
					//printf(WHERESTR "Item was correctly freed: %d\n", WHEREARG, req->dataItem);
					
				}
				else
				{
					//printf(WHERESTR "Item is still in use: %d\n", WHEREARG, req->dataItem);
					g_queue_push_tail(Gppu_temp, req);
					req = NULL;
				}
				
				pthread_mutex_unlock(&ppu_pointerOld_mutex);
			}
			else
			{
				pthread_mutex_unlock(&ppu_pointerOld_mutex);
				
				pthread_mutex_lock(&ppu_pointer_mutex);
				
				GHashTableIter iter;
				gpointer key, value;
				g_hash_table_iter_init (&iter, Gppu_pointers);

				while (g_hash_table_iter_next (&iter, &key, &value)) 
				{
					pe = value;
					if (pe->id == req->dataItem && pe->mode != ACQUIRE_MODE_WRITE)
					{
						//printf(WHERESTR "Item is still in use: %d\n", WHEREARG, req->dataItem);
						g_queue_push_tail(Gppu_temp, req);
						req = NULL;
						break;
					}
				}
				pthread_mutex_unlock(&ppu_pointer_mutex);
			}
			
			if (req != NULL) {
				//printf(WHERESTR "Responding to invalidate for %d, with reqId: %d\n", WHEREARG, req->dataItem, req->requestID);
				
				EnqueInvalidateResponse(req->requestID);
				
				FREE(req);
				req = NULL;
			}
		}

		//Re-insert items
		while(!g_queue_is_empty(Gppu_temp))
			g_queue_push_tail(Gppu_pendingInvalidate, g_queue_pop_head(Gppu_temp));
		
		if (g_queue_get_length(Gppu_temp) > 0)
			g_queue_clear(Gppu_temp);	
			
	}
	pthread_mutex_unlock(&ppu_invalidate_mutex);
}

int isPendingInvalidate(GUID id)
{
	GList* l;
	
	if (g_queue_is_empty(Gppu_pendingInvalidate))
		return 0;
	
	l = Gppu_pendingInvalidate->head;
	
	//TODO: Is this right compared to
	
	/*
	while (l != NULL)
		if ((*l)->element == (void*)id)
			return 1;
		else
			l = &(*l)->next;			
	 */
	while (l != NULL)
		if (l->data == (void*)id)
			return 1;
		else
			l = l->next;
			
	return 0;
	
}

void threadAcquireBarrier(GUID id)
{
	
	struct acquireBarrierRequest* cr;
	struct acquireBarrierResponse* ar;
	
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return;
	}
	
	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exeeds PAGE_TABLE_SIZE");
		return;
	}
	
	//Create the request, this will be released by the coordinator	
	if ((cr = (struct acquireBarrierRequest*)MALLOC(sizeof(struct acquireBarrierRequest))) == NULL)
		REPORT_ERROR("malloc error");

	cr->requestID = 0;
	cr->dataItem = id;

	//Perform the request and await the response
	ar = (struct acquireBarrierResponse*)forwardRequest(cr);
	FREE(ar);
	ar = NULL;
	return;
}

//Perform an acquire in the current thread
void* threadAcquire(GUID id, unsigned long* size, int type)
{
	
	void* retval;
	struct acquireRequest* cr;
	struct acquireResponse* ar;
	PointerEntry pe;
	
	if (id == PAGE_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return NULL;
	}
	
	if (id >= PAGE_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exeeds PAGE_TABLE_SIZE");
		return NULL;
	}
	
	// If acquire is of type read and id is in pointerOld, then we
	// reacquire, without notifying system.
	
	processInvalidates(NULL);
	
	pthread_mutex_lock(&ppu_pointerOld_mutex);

	if ((pe = g_hash_table_lookup(Gppu_pointersOld, (void*)id)) != NULL) {
		//printf(WHERESTR "Starting reacquire on id: %i\n", WHEREARG, id);
		if (type == ACQUIRE_MODE_READ && (pe->count == 0 || pe->mode == ACQUIRE_MODE_READ) && !isPendingInvalidate(id))
		{
			pe->mode = type;
			pe->count++;
			pthread_mutex_unlock(&ppu_pointerOld_mutex);	

			pthread_mutex_lock(&ppu_pointer_mutex);
			if (g_hash_table_lookup(Gppu_pointers, pe->data) == NULL)
				g_hash_table_insert(Gppu_pointers, pe->data, pe);
			pthread_mutex_unlock(&ppu_pointer_mutex);
			
			return pe->data;
		}
	}

	pthread_mutex_unlock(&ppu_pointerOld_mutex);
		
	//Create the request, this will be released by the coordinator	
	if ((cr = (struct acquireRequest*)MALLOC(sizeof(struct acquireRequest))) == NULL)
		REPORT_ERROR("malloc error");

	if (type == ACQUIRE_MODE_WRITE) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: WRITE\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_WRITE;
	}
	else if (type == ACQUIRE_MODE_READ) {
		//printf(WHERESTR "Starting acquiring id: %i in mode: READ\n", WHEREARG, id);
		cr->packageCode = PACKAGE_ACQUIRE_REQUEST_READ;
	}
	else
		REPORT_ERROR("Starting acquiring in unknown mode");
		
	cr->requestID = 0;
	cr->dataItem = id;

	retval = NULL;
		
	//Perform the request and await the response
	ar = (struct acquireResponse*)forwardRequest(cr);
	
	//printf(WHERESTR "Recieved response %i\n", WHEREARG, ar->packageCode);
	
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		REPORT_ERROR("Unexcepted response for an Acquire request");
		retval = NULL;
	}
	else
	{
		//printf(WHERESTR "Done acquiring id: %i\n", WHEREARG, id);
		//The request was positive
		retval = ar->data;
		(*size) = ar->dataSize;
		
		if (type == ACQUIRE_MODE_WRITE)
		{
			pthread_mutex_lock(&ppu_pointerOld_mutex);
			if ((pe = g_hash_table_lookup(Gppu_pointersOld, (void*)id)) != NULL)
			{
				pe->mode = ACQUIRE_MODE_BLOCKED;
				
				while(pe->count != 0)
					pthread_cond_wait(&ppu_pointerOld_cond, &ppu_pointerOld_mutex);
			}
			pthread_mutex_unlock(&ppu_pointerOld_mutex);
		}	
	
		//recordPointer(retval, id, *size, 0, type);
	}
	
	FREE(ar);
	ar = NULL;
	return retval;	
}

//Perform a release on the current thread
void threadRelease(void* data)
{
	
	PointerEntry pe;
	struct releaseRequest* re;
	struct releaseResponse* rr;
	
	//Verify that the pointer is registered
	pthread_mutex_lock(&ppu_pointer_mutex);
	if ((pe = g_hash_table_lookup(Gppu_pointers, data)) != NULL)
	{
		//Extract the pointer, and release the mutex fast
		pthread_mutex_unlock(&ppu_pointer_mutex);
		
		if (pe->id == PAGE_TABLE_ID)
		{
			REPORT_ERROR("cannot request pagetable");
			return;
		}
		
		pthread_mutex_lock(&ppu_pointerOld_mutex);
		pe->count--;
					
		if (pe->count == 0)
		{
			pthread_mutex_unlock(&ppu_pointerOld_mutex);
			
			pthread_mutex_lock(&ppu_pointer_mutex);			
			g_hash_table_remove(Gppu_pointers, data);
			pthread_mutex_unlock(&ppu_pointer_mutex);
		}
		else
			pthread_mutex_unlock(&ppu_pointerOld_mutex);
		
		if (pe->mode == ACQUIRE_MODE_WRITE)
		{
			//Create a request, this will be released by the coordinator
			if ((re = (struct releaseRequest*)MALLOC(sizeof(struct releaseRequest))) == NULL)
				REPORT_ERROR("malloc error");
			re->packageCode = PACKAGE_RELEASE_REQUEST;
			re->requestID = 0;
			re->dataItem = pe->id;
			re->dataSize = pe->size;
			re->offset = pe->offset;
			re->data = data;
			re->mode = pe->mode;

			//Perform the request and await the response
			rr = (struct releaseResponse*)forwardRequest(re);
			if(rr->packageCode != PACKAGE_RELEASE_RESPONSE)
				REPORT_ERROR("Reponse to release had unexpected type");
			
			FREE(rr);
			rr = NULL;
		}

		pthread_mutex_lock(&ppu_pointerOld_mutex);
		if (pe->count == 0 && g_hash_table_lookup(Gppu_pointersOld, data) == NULL)
			FREE(pe);
		pthread_cond_broadcast(&ppu_pointerOld_cond);
		pthread_mutex_unlock(&ppu_pointerOld_mutex);

		processInvalidates(NULL);
		
	}
	else
	{
		pthread_mutex_unlock(&ppu_pointer_mutex);
		REPORT_ERROR("Pointer given to release was not registered");
	}
}


//This is a very simple thread that exists to remove race conditions
//It ensures that no two requests are overlapping, and processes invalidate requests
void* ppu_requestDispatcher(void* dummy)
{
	
	void* data;
	struct acquireResponse* resp;
	unsigned int reqId;
	QueueableItem ui;
	
	while(!ppu_terminate)
	{
		pthread_mutex_lock(&ppu_queue_mutex);
		data = NULL;
		while(!ppu_terminate && g_queue_is_empty(Gppu_work_queue))
			pthread_cond_wait(&ppu_queue_cond, &ppu_queue_mutex);
		
		if (ppu_terminate)
		{
			pthread_mutex_unlock(&ppu_queue_mutex);
			return dummy;
		}	
		
		data = g_queue_pop_head(Gppu_work_queue);
		
		pthread_mutex_unlock(&ppu_queue_mutex);
		
		if (data != NULL)
		{
			//printf(WHERESTR "Processing package with type: %d, reqId: %d\n", WHEREARG, ((struct createRequest*)data)->packageCode, ((struct createRequest*)data)->requestID);

			switch (((struct createRequest*)data)->packageCode)
			{
				case PACKAGE_INVALIDATE_REQUEST:
					//printf(WHERESTR "Processing invalidate\n", WHEREARG);
					processInvalidates((struct invalidateRequest*)data);
					data = NULL;
					break;
				case PACKAGE_ACQUIRE_RESPONSE:
					//printf(WHERESTR "Processing acquire response\n", WHEREARG);
					resp = (struct acquireResponse*)data;
					recordPointer(resp->data, resp->dataItem, resp->dataSize, 0, resp->mode != ACQUIRE_MODE_READ ? ACQUIRE_MODE_WRITE : ACQUIRE_MODE_READ);
					break;
			}
		}
		
		if (data != NULL)
		{
			reqId = ((struct createRequest*)data)->requestID;
		
			pthread_mutex_lock(&ppu_queue_mutex);
			
			if ((ui = g_hash_table_lookup(ppuhandler_pendingRequests, (void*)reqId)) == NULL) {
				//printf(WHERESTR "* ERROR * Response was for ID: %d, package type: %d\n", WHEREARG, reqId, ((struct createRequest*)data)->packageCode);
				
				REPORT_ERROR("Recieved unexpected request");				
			} else {		
				//printf(WHERESTR "Event: %i, Mutex: %i, Queue: %i \n", WHEREARG, (int)ui->event, (int)ui->mutex, (int)ui->queue);
				
				int freeReq = (((struct acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireResponse*)data)->mode != ACQUIRE_MODE_WRITE) || ((struct acquireResponse*)data)->packageCode != PACKAGE_ACQUIRE_RESPONSE;
				
				if (ui->mutex != NULL)
					pthread_mutex_lock(ui->mutex);
				if (ui->Gqueue != NULL) {
					g_queue_push_tail(*ui->Gqueue, data);
				} else {
					REPORT_ERROR("queue was NULL");
				}
				if (ui->event != NULL)
					pthread_cond_signal(ui->event);
				if (ui->mutex != NULL)
					pthread_mutex_unlock(ui->mutex);
				
				if (freeReq)
				{
					g_hash_table_remove(ppuhandler_pendingRequests, (void*)reqId);					
					FREE(ui);
					ui = NULL;
				}
			}

			pthread_mutex_unlock(&ppu_queue_mutex);			
		}
	}	
	return dummy;
}

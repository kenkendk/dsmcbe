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
pthread_mutex_t pointer_mutex;
//This mutex protects the old pointer list
//pthread_mutex_t pointerOld_mutex;
//This mutex protects the invalidate queue
pthread_mutex_t ppu_invalidate_mutex;
//This signals when an item has been released
//pthread_cond_t pointerOld_cond;

//These two tables contains the registered pointers, either active or retired
GHashTable* Gpointers;
//GHashTable* GpointersOld;

//This is the queue of pending invalidates
GQueue* GpendingInvalidate;

volatile unsigned int terminate;

#define MAX_SEQUENCE_NUMBER 1000000
unsigned int request_sequence_number = 0;

GHashTable* GpendingRequests;

pthread_mutex_t ppu_queue_mutex;
pthread_cond_t ppu_queue_cond;
GQueue* Gppu_work_queue;
pthread_t dispatchthread;

GQueue* Gdummy;
GQueue* Gtemp;
pthread_mutex_t dummy_mutex;
pthread_cond_t dummy_cond;


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


void* requestDispatcher(void* data);

//Setup the PPUHandler
void InitializePPUHandler()
{
	
	pthread_attr_t attr;
	
	//TODO Use new_full, so it is easy to destroy!
	Gpointers = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&pointer_mutex, NULL);
	
	//TODO Use new_full, so it is easy to destroy!
/*
	GpointersOld = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&pointerOld_mutex, NULL);
	pthread_cond_init(&pointerOld_cond, NULL);
*/
	GpendingInvalidate = g_queue_new();
	pthread_mutex_init(&ppu_invalidate_mutex, NULL);

	GpendingRequests = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&ppu_queue_mutex, NULL);
	pthread_cond_init(&ppu_queue_cond, NULL);	

	Gppu_work_queue = g_queue_new();	

	Gtemp = g_queue_new();

	Gdummy = g_queue_new();
	pthread_mutex_init(&dummy_mutex, NULL);
	pthread_cond_init(&dummy_cond, NULL);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&dispatchthread, &attr, requestDispatcher, NULL);
	pthread_attr_destroy(&attr);
	
	RegisterInvalidateSubscriber(&ppu_queue_mutex, &ppu_queue_cond, &Gppu_work_queue);
}

//Terminate the PPUHandler and release all resources
void TerminatePPUHandler()
{
	terminate = 1;
	
	pthread_join(dispatchthread, NULL);
	
	g_hash_table_destroy(Gpointers);
	//g_hash_table_destroy(GpointersOld);
	
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
	UnregisterInvalidateSubscriber(&GpendingInvalidate);
	
	pthread_mutex_destroy(&pointer_mutex);
	//pthread_mutex_destroy(&pointerOld_mutex);
	pthread_mutex_destroy(&ppu_invalidate_mutex);
	//pthread_cond_destroy(&pointerOld_cond);
	g_queue_free(GpendingInvalidate);

	g_hash_table_destroy(GpendingRequests);
	pthread_mutex_destroy(&ppu_queue_mutex);
	pthread_cond_destroy(&ppu_queue_cond);	
	g_queue_free(Gppu_work_queue);
	
	g_queue_free(Gtemp);	
	g_queue_free(Gdummy);
	pthread_mutex_destroy(&dummy_mutex);
	pthread_cond_destroy(&dummy_cond);
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
	
	q->dataRequest = NULL;
	
	pthread_mutex_lock(relay->mutex);
	((struct createRequest*)relay->dataRequest)->requestID = NEXT_SEQ_NO(request_sequence_number, MAX_SEQUENCE_NUMBER);
	g_hash_table_insert(GpendingRequests, (void*)(((struct createRequest*)relay->dataRequest)->requestID), q);
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
	
	//Gdummy = g_queue_new();
	q->dataRequest = data;
	q->event = &dummy_cond;
	q->mutex = &dummy_mutex;
	q->Gqueue = &Gdummy;
	
	//printf(WHERESTR "Event: %i, Mutex: %i, Queue: %i\n", WHEREARG, (int)q->event, (int)q->mutex, (int)q->queue);	
	
	//printf(WHERESTR "adding item to queue\n", WHEREARG);
	RelayEnqueItem(q);
	//printf(WHERESTR "item added to queue %i\n", WHEREARG, (int)q);
	
	pthread_mutex_lock(&dummy_mutex);
	//printf(WHERESTR "locked %i\n", WHEREARG, (int)&m);


	while (g_queue_is_empty(Gdummy)) {
		//printf(WHERESTR "waiting for queue %i\n", WHEREARG, (int)&e);
		pthread_cond_wait(&dummy_cond, &dummy_mutex);
		//printf(WHERESTR "queue filled\n", WHEREARG);
	}
	
	data = g_queue_pop_head(Gdummy);
	pthread_mutex_unlock(&dummy_mutex);
	
	if (((struct acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireResponse*)data)->mode == ACQUIRE_MODE_WRITE) {
	
		//printf(WHERESTR "waiting for writebuffer signal\n", WHEREARG);
		
		pthread_mutex_lock(&dummy_mutex);
		while (g_queue_is_empty(Gdummy)) {
			//printf(WHERESTR "waiting for writebuffer signal\n", WHEREARG);
			pthread_cond_wait(&dummy_cond, &dummy_mutex);
			//printf(WHERESTR "got for writebuffer signal\n", WHEREARG);
		}
		
		struct writebufferReady* data2 = g_queue_pop_head(Gdummy);
		pthread_mutex_unlock(&dummy_mutex);
		if (data2->packageCode != PACKAGE_WRITEBUFFER_READY)
			REPORT_ERROR("Expected PACKAGE_WRITEBUFFER_READY");
		
		FREE(data2);
		data2 = NULL;
	
		pthread_mutex_unlock(&dummy_mutex);
	}
	
	if (((struct acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct acquireResponse*)data)->mode == ACQUIRE_MODE_WRITE_OK)
			((struct acquireResponse*)data)->mode = ACQUIRE_MODE_WRITE;

	//printf(WHERESTR "returning response (%d)\n", WHEREARG, (int)data);
	
	//g_queue_free(dummy);
	if (g_queue_get_length(Gdummy) > 0)
		g_queue_clear(Gdummy);	
	
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
		pthread_mutex_lock(&pointer_mutex);
		
		if ((ent = g_hash_table_lookup(Gpointers, retval)) != NULL)
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
		
		g_hash_table_insert(Gpointers, retval, ent);
		pthread_mutex_unlock(&pointer_mutex);
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
	
	free(ar);
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
		g_queue_push_tail(GpendingInvalidate, incoming);
	}

	//printf(WHERESTR "Testing queue\n", WHEREARG);
	
	if (!g_queue_is_empty(GpendingInvalidate))
	{
		//printf(WHERESTR "Queue is not empty\n", WHEREARG);

		//Gtemp = g_queue_new();
		while(!g_queue_is_empty(GpendingInvalidate))
		{
			req = g_queue_pop_head(GpendingInvalidate);
			//printf(WHERESTR "Processing request for %d with reqId: %d\n", WHEREARG, req->dataItem, req->requestID);
/*
			pthread_mutex_lock(&pointerOld_mutex);

			if ((pe = g_hash_table_lookup(GpointersOld, (void*)req->dataItem)) != NULL)
			{
				if (pe->count == 0 && pe->mode != ACQUIRE_MODE_BLOCKED )
				{
					g_hash_table_remove(GpointersOld, (void*)req->dataItem);
					
					FREE(pe);
					pe = NULL;
			
					//printf(WHERESTR "Item was correctly freed: %d\n", WHEREARG, req->dataItem);
					
				}
				else
				{
					//printf(WHERESTR "Item is still in use: %d\n", WHEREARG, req->dataItem);
					g_queue_push_tail(Gtemp, req);
					req = NULL;
				}
				
				pthread_mutex_unlock(&pointerOld_mutex);
			}
			else

			{
			
			pthread_mutex_unlock(&pointerOld_mutex);
*/			
			pthread_mutex_lock(&pointer_mutex);
			
			GHashTableIter iter;
			gpointer key, value;
			g_hash_table_iter_init (&iter, Gpointers);

			while (g_hash_table_iter_next (&iter, &key, &value)) 
			{
				pe = value;
				if (pe->id == req->dataItem && pe->mode != ACQUIRE_MODE_WRITE)
				{
					//printf(WHERESTR "Item is still in use: %d\n", WHEREARG, req->dataItem);
					g_queue_push_tail(Gtemp, req);
					req = NULL;
					break;
				}
			}
			pthread_mutex_unlock(&pointer_mutex);
			//}
			
			if (req != NULL) {
				//printf(WHERESTR "Responding to invalidate for %d, with reqId: %d\n", WHEREARG, req->dataItem, req->requestID);
				
				EnqueInvalidateResponse(req->requestID);
				
				free(req);
				req = NULL;
			}
		}

		//Re-insert items
		while(!g_queue_is_empty(Gtemp))
			g_queue_push_tail(GpendingInvalidate, g_queue_pop_head(Gtemp));
		
		if (g_queue_get_length(Gtemp) > 0)
			g_queue_clear(Gtemp);	
			
	}
	pthread_mutex_unlock(&ppu_invalidate_mutex);
}

int isPendingInvalidate(GUID id)
{
	GList* l;
	
	if (g_queue_is_empty(GpendingInvalidate))
		return 0;
	
	l = GpendingInvalidate->head;
	
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

//Perform an acquire in the current thread
void* threadAcquire(GUID id, unsigned long* size, int type)
{
	
	void* retval;
	struct acquireRequest* cr;
	struct acquireResponse* ar;
	//PointerEntry pe;
	
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

/*	
	pthread_mutex_lock(&pointerOld_mutex);

	if ((pe = g_hash_table_lookup(GpointersOld, (void*)id)) != NULL) {
		printf(WHERESTR "Starting reacquire on id: %i\n", WHEREARG, id);
		if (type == ACQUIRE_MODE_READ && (pe->count == 0 || pe->mode == ACQUIRE_MODE_READ) && !isPendingInvalidate(id))
		{
			pe->mode = type;
			pe->count++;
			pthread_mutex_unlock(&pointerOld_mutex);	

			pthread_mutex_lock(&pointer_mutex);
			if (g_hash_table_lookup(Gpointers, pe->data) == NULL)
				g_hash_table_insert(Gpointers, pe->data, pe);
			pthread_mutex_unlock(&pointer_mutex);
			
			return pe->data;
		}
	}

	pthread_mutex_unlock(&pointerOld_mutex);
*/	
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
/*		
		if (type == ACQUIRE_MODE_WRITE)
		{
			pthread_mutex_lock(&pointerOld_mutex);
			if ((pe = g_hash_table_lookup(GpointersOld, (void*)id)) != NULL)
			{
				pe->mode = ACQUIRE_MODE_BLOCKED;
				
				while(pe->count != 0)
					pthread_cond_wait(&pointerOld_cond, &pointerOld_mutex);
			}
			pthread_mutex_unlock(&pointerOld_mutex);
		}	
*/	
		//recordPointer(retval, id, *size, 0, type);
	}
	
	free(ar);
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
	pthread_mutex_lock(&pointer_mutex);
	if ((pe = g_hash_table_lookup(Gpointers, data)) != NULL)
	{
		//Extract the pointer, and release the mutex fast
		pthread_mutex_unlock(&pointer_mutex);
		
		if (pe->id == PAGE_TABLE_ID)
		{
			REPORT_ERROR("cannot request pagetable");
			return;
		}
		
		//pthread_mutex_lock(&pointerOld_mutex);
		pe->count--;
					
		if (pe->count == 0)
		{
			//pthread_mutex_unlock(&pointerOld_mutex);
			
			pthread_mutex_lock(&pointer_mutex);			
			g_hash_table_remove(Gpointers, data);
			pthread_mutex_unlock(&pointer_mutex);
		}
		//else
			//pthread_mutex_unlock(&pointerOld_mutex);
		
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
			
			free(rr);
			rr = NULL;
		}

		//pthread_mutex_lock(&pointerOld_mutex);
		if (pe->count == 0)// && g_hash_table_lookup(GpointersOld, data) == NULL)
		{
			//printf(WHERESTR "Freeing pointer\n", WHEREARG);
			FREE(pe);
		}
		//pthread_cond_broadcast(&pointerOld_cond);
		//pthread_mutex_unlock(&pointerOld_mutex);

		processInvalidates(NULL);
		
	}
	else
	{
		pthread_mutex_unlock(&pointer_mutex);
		REPORT_ERROR("Pointer given to release was not registered");
	}
}


//This is a very simple thread that exists to remove race conditions
//It ensures that no two requests are overlapping, and processes invalidate requests
void* requestDispatcher(void* dummy)
{
	
	void* data;
	struct acquireResponse* resp;
	unsigned int reqId;
	QueueableItem ui;
	
	while(!terminate)
	{
		pthread_mutex_lock(&ppu_queue_mutex);
		data = NULL;
		while(!terminate && g_queue_is_empty(Gppu_work_queue))
			pthread_cond_wait(&ppu_queue_cond, &ppu_queue_mutex);
		
		if (terminate)
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
			
			if ((ui = g_hash_table_lookup(GpendingRequests, (void*)reqId)) == NULL) {
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
					g_hash_table_remove(GpendingRequests, (void*)reqId);					
					FREE(ui);
					ui = NULL;
				}
			}

			pthread_mutex_unlock(&ppu_queue_mutex);			
		}
	}	
	return dummy;
}

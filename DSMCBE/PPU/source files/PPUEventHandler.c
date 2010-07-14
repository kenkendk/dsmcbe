/*
 *
 * This module contains code that handles requests 
 * from PPU units
 *
 */

#include <pthread.h>
#include <stdio.h>

#include <PPUEventHandler.h>
#include <RequestCoordinator.h>
#include <NetworkHandler.h>
#include <debug.h>
#include <PPUEventHandler_CSP.h>
#include <dsmcbe_initializers.h>
#include <stdlib.h>

//This mutex protects the pointer list
pthread_mutex_t dsmcbe_ppu_pointer_mutex;
//This mutex protects the old pointer list
pthread_mutex_t dsmcbe_ppu_pointerOld_mutex;
//This mutex protects the invalidate queue
pthread_mutex_t dsmcbe_ppu_invalidate_mutex;
//This signals when an item has been released
pthread_cond_t dsmcbe_ppu_pointerOld_cond;

//These two tables contains the registered pointers, either active or retired
GHashTable* dsmcbe_ppu_Gpointers;
GHashTable* dsmcbe_ppu_GpointersOld;

//This is the queue of pending invalidates
GQueue* dsmcbe_ppu_GpendingInvalidate;

volatile unsigned int dsmcbe_ppu_terminate;

#define MAX_SEQUENCE_NUMBER 1000000
unsigned int dsmcbe_ppu_request_sequence_number = 0;

GHashTable* dsmcbe_ppu_GpendingRequests;

pthread_mutex_t dsmcbe_ppu_queue_mutex;
pthread_cond_t dsmcbe_ppu_queue_cond;
GQueue* dsmcbe_ppu_Gwork_queue;
pthread_t dsmcbe_ppu_dispatchthread;

GQueue* dsmcbe_ppu_Gtemp;


typedef struct dsmcbe_ppu_PointerEntryStruct* PointerEntry;
struct dsmcbe_ppu_PointerEntryStruct
{
	GUID id;
	void* data;
	unsigned long offset;
	unsigned long size;	
	int mode;
	unsigned int count;
};


void* dsmcbe_ppu_requestDispatcher(void* data);

PointerEntry dsmcbe_ppu_new_PointerEntry(GUID id, void* data, unsigned long offset, unsigned long size, int mode, unsigned int count)
{
	PointerEntry res = (PointerEntry)MALLOC(sizeof(struct dsmcbe_ppu_PointerEntryStruct));

	res->id = id;
	res->data = data;
	res->offset = offset;
	res->size = size;
	res->mode = mode;
	res->count = count;

	return res;
}

//Setup the PPUHandler
void dsmcbe_ppu_initialize()
{
	pthread_attr_t attr;
	
	dsmcbe_ppu_Gpointers = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&dsmcbe_ppu_pointer_mutex, NULL);
	
	dsmcbe_ppu_GpointersOld = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&dsmcbe_ppu_pointerOld_mutex, NULL);
	pthread_cond_init(&dsmcbe_ppu_pointerOld_cond, NULL);

	dsmcbe_ppu_GpendingInvalidate = g_queue_new();
	pthread_mutex_init(&dsmcbe_ppu_invalidate_mutex, NULL);

	dsmcbe_ppu_GpendingRequests = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&dsmcbe_ppu_queue_mutex, NULL);
	pthread_cond_init(&dsmcbe_ppu_queue_cond, NULL);

	dsmcbe_ppu_Gwork_queue = g_queue_new();

	dsmcbe_ppu_Gtemp = g_queue_new();

	csp_ppu_allocatedPointers = g_hash_table_new(NULL, NULL);
	pthread_mutex_init(&csp_ppu_allocatedPointersMutex, NULL);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&dsmcbe_ppu_dispatchthread, &attr, dsmcbe_ppu_requestDispatcher, NULL);
	pthread_attr_destroy(&attr);
	
	dsmcbe_rc_RegisterInvalidateSubscriber(&dsmcbe_ppu_queue_mutex, &dsmcbe_ppu_queue_cond, &dsmcbe_ppu_Gwork_queue, -1);
}

//dsmcbe_ppu_terminate the PPUHandler and release all resources
void dsmcbe_ppu_terminatePPUHandler()
{
	dsmcbe_ppu_terminate = 1;
	
	pthread_join(dsmcbe_ppu_dispatchthread, NULL);
	
	g_hash_table_destroy(dsmcbe_ppu_Gpointers);
	g_hash_table_destroy(dsmcbe_ppu_GpointersOld);
	
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
	dsmcbe_rc_UnregisterInvalidateSubscriber(&dsmcbe_ppu_GpendingInvalidate);
	
	pthread_mutex_destroy(&dsmcbe_ppu_pointer_mutex);
	pthread_mutex_destroy(&dsmcbe_ppu_pointerOld_mutex);
	pthread_mutex_destroy(&dsmcbe_ppu_invalidate_mutex);
	pthread_cond_destroy(&dsmcbe_ppu_pointerOld_cond);
	g_queue_free(dsmcbe_ppu_GpendingInvalidate);

	g_hash_table_destroy(dsmcbe_ppu_GpendingRequests);
	pthread_mutex_destroy(&dsmcbe_ppu_queue_mutex);
	pthread_cond_destroy(&dsmcbe_ppu_queue_cond);
	g_queue_free(dsmcbe_ppu_Gwork_queue);
	
	pthread_mutex_destroy(&csp_ppu_allocatedPointersMutex);
	g_hash_table_destroy(csp_ppu_allocatedPointers);

	g_queue_free(dsmcbe_ppu_Gtemp);
}

void dsmcbe_ppu_RelayEnqueItem(QueueableItem q)
{
	QueueableItem relay = dsmcbe_rc_new_QueueableItem(&dsmcbe_ppu_queue_mutex, &dsmcbe_ppu_queue_cond, &dsmcbe_ppu_Gwork_queue, q->dataRequest, NULL);
	
	q->dataRequest = NULL;
	
	pthread_mutex_lock(relay->mutex);
	((struct dsmcbe_createRequest*)relay->dataRequest)->requestID = NEXT_SEQ_NO(dsmcbe_ppu_request_sequence_number, MAX_SEQUENCE_NUMBER);
	g_hash_table_insert(dsmcbe_ppu_GpendingRequests, (void*)(((struct dsmcbe_createRequest*)relay->dataRequest)->requestID), q);
	pthread_mutex_unlock(relay->mutex);
	
	dsmcbe_rc_EnqueItem(relay);
}

//Sends a request into the coordinator, and awaits the response (blocking)
void* dsmcbe_ppu_forwardRequest(void* data)
{	
	//TODO: Bad for performance to create items here, should be created per thread!!
	GQueue* local_queue = g_queue_new();
	pthread_mutex_t local_mutex;
	pthread_cond_t local_cond;

	pthread_mutex_init(&local_mutex, NULL);
	pthread_cond_init(&local_cond, NULL);

	//Create the entry, this will be released by the coordinator
	QueueableItem q = dsmcbe_rc_new_QueueableItem(&local_mutex, &local_cond, &local_queue, data, NULL);
	
	dsmcbe_ppu_RelayEnqueItem(q);
	
	pthread_mutex_lock(&local_mutex);

	while (g_queue_is_empty(local_queue)) {
		pthread_cond_wait(&local_cond, &local_mutex);
	}
	
	data = g_queue_pop_head(local_queue);
	pthread_mutex_unlock(&local_mutex);

	if (((struct dsmcbe_acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct dsmcbe_acquireResponse*)data)->mode == ACQUIRE_MODE_WRITE) {
		
		if (((struct dsmcbe_acquireResponse*)data)->writeBufferReady != TRUE)
		{
			pthread_mutex_lock(&local_mutex);
			while (g_queue_is_empty(local_queue)) {
				pthread_cond_wait(&local_cond, &local_mutex);
			}
			
			struct dsmcbe_writebufferReady* data2 = g_queue_pop_head(local_queue);
			pthread_mutex_unlock(&local_mutex);
			if (data2->packageCode != PACKAGE_WRITEBUFFER_READY)
				REPORT_ERROR("Expected PACKAGE_WRITEBUFFER_READY");
			
			FREE(data2);
			data2 = NULL;
		}
	}
	
	g_queue_free(local_queue);
	pthread_mutex_destroy(&local_mutex);
	pthread_cond_destroy(&local_cond);

	local_queue = NULL;
	
	return data;
}

//Record information about the returned pointer
void dsmcbe_ppu_recordPointer(void* retval, GUID id, unsigned long size, unsigned long offset, int type)
{
	PointerEntry ent;
	
	if (type != ACQUIRE_MODE_READ && type != ACQUIRE_MODE_WRITE)
		REPORT_ERROR("pointer was neither READ nor WRITE");
	
	//If the response was valid, record the item data
	if (retval != NULL)
	{		
		pthread_mutex_lock(&dsmcbe_ppu_pointer_mutex);
		
		if ((ent = g_hash_table_lookup(dsmcbe_ppu_Gpointers, retval)) != NULL)
			ent->count++;
		else
			ent = dsmcbe_ppu_new_PointerEntry(id, retval, offset, size, type, 1);
		
		g_hash_table_insert(dsmcbe_ppu_Gpointers, retval, ent);
		pthread_mutex_unlock(&dsmcbe_ppu_pointer_mutex);
	}	
}

//Perform a create in the current thread
void* dsmcbe_ppu_create(GUID id, unsigned long size)
{
	struct dsmcbe_createRequest* cr;
	struct dsmcbe_acquireResponse* ar;
	void* retval;
	
	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return NULL;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exceeds PAGE_TABLE_SIZE");
		return NULL;
	}
	
	//Create the request, this will be released by the coordinator
	cr = dsmcbe_new_createRequest(id, 0, size == 0 ? 1 : size);
	
	retval = NULL;
	
	//Perform the request and await the response
	ar = (struct dsmcbe_acquireResponse*)dsmcbe_ppu_forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		if (ar->packageCode != PACKAGE_NACK)
			REPORT_ERROR("Unexcepted response for a Create request");
	}
	else
	{
		//The response was positive
		retval = ar->data;
#ifdef DEBUG
		if (ar->dataSize != size)
			REPORT_ERROR("Bad size returned in create");
		if (ar->requestID != 0)
			REPORT_ERROR("Bad request ID returned in create");
#endif
	}

	FREE(ar);
	ar = NULL;
	return retval;	
}

void dsmcbe_ppu_processInvalidates(struct dsmcbe_invalidateRequest* incoming)
{
	struct dsmcbe_invalidateRequest* req;
	PointerEntry pe;
	
	pthread_mutex_lock(&dsmcbe_ppu_invalidate_mutex);
	
	if (incoming != NULL)
		g_queue_push_tail(dsmcbe_ppu_GpendingInvalidate, incoming);

	if (!g_queue_is_empty(dsmcbe_ppu_GpendingInvalidate))
	{
		while(!g_queue_is_empty(dsmcbe_ppu_GpendingInvalidate))
		{
			req = g_queue_pop_head(dsmcbe_ppu_GpendingInvalidate);
			//printf(WHERESTR "Processing request for %d with reqId: %d\n", WHEREARG, req->dataItem, req->requestID);

			pthread_mutex_lock(&dsmcbe_ppu_pointerOld_mutex);

			if ((pe = g_hash_table_lookup(dsmcbe_ppu_GpointersOld, (void*)req->dataItem)) != NULL)
			{
				if (pe->count == 0 && pe->mode != ACQUIRE_MODE_BLOCKED )
				{
					g_hash_table_remove(dsmcbe_ppu_GpointersOld, (void*)req->dataItem);
					
					FREE(pe);
					pe = NULL;
				}
				else
				{
					//Item is still in use :(
					g_queue_push_tail(dsmcbe_ppu_Gtemp, req);
					req = NULL;
				}
				
				pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);
			}
			else
			{
				pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);
				
				pthread_mutex_lock(&dsmcbe_ppu_pointer_mutex);
				
				GHashTableIter iter;
				gpointer key, value;
				g_hash_table_iter_init (&iter, dsmcbe_ppu_Gpointers);

				while (g_hash_table_iter_next (&iter, &key, &value)) 
				{
					pe = value;
					if (pe->id == req->dataItem && pe->mode != ACQUIRE_MODE_WRITE)
					{
						//Item is still in use :(
						g_queue_push_tail(dsmcbe_ppu_Gtemp, req);
						req = NULL;
						break;
					}
				}
				pthread_mutex_unlock(&dsmcbe_ppu_pointer_mutex);
			}
			
			if (req != NULL) {
				dsmcbe_rc_EnqueInvalidateResponse(req->dataItem, req->requestID);
				
				FREE(req);
				req = NULL;
			}
		}

		//Re-insert items
		while(!g_queue_is_empty(dsmcbe_ppu_Gtemp))
			g_queue_push_tail(dsmcbe_ppu_GpendingInvalidate, g_queue_pop_head(dsmcbe_ppu_Gtemp));
		
		if (g_queue_get_length(dsmcbe_ppu_Gtemp) > 0)
			g_queue_clear(dsmcbe_ppu_Gtemp);
			
	}
	pthread_mutex_unlock(&dsmcbe_ppu_invalidate_mutex);
}

int dsmcbe_ppu_isPendingInvalidate(GUID id)
{
	GList* l;
	
	if (g_queue_is_empty(dsmcbe_ppu_GpendingInvalidate))
		return 0;
	
	l = dsmcbe_ppu_GpendingInvalidate->head;
	
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

void dsmcbe_ppu_acquireBarrier(GUID id)
{
	struct dsmcbe_acquireBarrierRequest* cr;
	struct dsmcbe_acquireBarrierResponse* ar;
	
	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("cannot request pagetable");
		return;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exceeds PAGE_TABLE_SIZE");
		return;
	}
	
	//Create the request, this will be released by the coordinator	
	cr = dsmcbe_new_acquireBarrierRequest(id, 0);

	//Perform the request and await the response
	ar = (struct dsmcbe_acquireBarrierResponse*)dsmcbe_ppu_forwardRequest(cr);
	if (ar->packageCode != PACKAGE_ACQUIRE_BARRIER_RESPONSE)
	{
		REPORT_ERROR("Unexcepted response for an Acquire Barrier request");
	}
	
	FREE(ar);
	ar = NULL;
	return;
}

//Perform an acquire in the current thread
void* dsmcbe_ppu_acquire(GUID id, unsigned long* size, int type)
{
	void* retval;
	struct dsmcbe_acquireRequest* cr;
	struct dsmcbe_acquireResponse* ar;
	PointerEntry pe;
	
	if (id == OBJECT_TABLE_ID)
	{
		REPORT_ERROR("cannot request object table");
		return NULL;
	}
	
	if (id >= OBJECT_TABLE_SIZE)
	{
		REPORT_ERROR("requested ID exceeds OBJECT_TABLE_SIZE");
		return NULL;
	}
	
	// If acquire is of type read and id is in pointerOld, then we
	// reacquire, without notifying system.
	
	dsmcbe_ppu_processInvalidates(NULL);
	
	pthread_mutex_lock(&dsmcbe_ppu_pointerOld_mutex);

	if ((pe = g_hash_table_lookup(dsmcbe_ppu_GpointersOld, (void*)id)) != NULL) {
		//Starting local reacquire
		if (type == ACQUIRE_MODE_READ && (pe->count == 0 || pe->mode == ACQUIRE_MODE_READ) && !dsmcbe_ppu_isPendingInvalidate(id))
		{
			pe->mode = type;
			pe->count++;
			pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);

			pthread_mutex_lock(&dsmcbe_ppu_pointer_mutex);
			if (g_hash_table_lookup(dsmcbe_ppu_Gpointers, pe->data) == NULL)
				g_hash_table_insert(dsmcbe_ppu_Gpointers, pe->data, pe);
			pthread_mutex_unlock(&dsmcbe_ppu_pointer_mutex);
			
			return pe->data;
		}
	}

	pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);
		
	//Create the request, this will be released by the coordinator	
	cr = dsmcbe_new_acquireRequest(id, 0, type);

	retval = NULL;
		
	//Perform the request and await the response
	ar = (struct dsmcbe_acquireResponse*)dsmcbe_ppu_forwardRequest(cr);
	
	if (ar->packageCode != PACKAGE_ACQUIRE_RESPONSE)
	{
		REPORT_ERROR("Unexcepted response for an Acquire request");
		retval = NULL;
	}
	else
	{
		//The request was positive
		retval = ar->data;
		if (size != NULL)
			(*size) = ar->dataSize;
		
		if (type == ACQUIRE_MODE_WRITE)
		{
			pthread_mutex_lock(&dsmcbe_ppu_pointerOld_mutex);
			if ((pe = g_hash_table_lookup(dsmcbe_ppu_GpointersOld, (void*)id)) != NULL)
			{
				pe->mode = ACQUIRE_MODE_BLOCKED;
				
				while(pe->count != 0)
					pthread_cond_wait(&dsmcbe_ppu_pointerOld_cond, &dsmcbe_ppu_pointerOld_mutex);
			}
			pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);
		}	
	}
	
	FREE(ar);
	ar = NULL;
	return retval;	
}

//Perform a release on the current thread
void dsmcbe_ppu_release(void* data)
{
	PointerEntry pe;
	struct dsmcbe_releaseRequest* re;
	
	//Verify that the pointer is registered
	pthread_mutex_lock(&dsmcbe_ppu_pointer_mutex);
	if ((pe = g_hash_table_lookup(dsmcbe_ppu_Gpointers, data)) != NULL)
	{
		//Extract the pointer, and release the mutex fast
		pthread_mutex_unlock(&dsmcbe_ppu_pointer_mutex);
		
		if (pe->id == OBJECT_TABLE_ID)
		{
			REPORT_ERROR("cannot request pagetable");
			return;
		}
		
		pthread_mutex_lock(&dsmcbe_ppu_pointerOld_mutex);
		pe->count--;
					
		if (pe->count == 0)
		{
			pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);
			
			pthread_mutex_lock(&dsmcbe_ppu_pointer_mutex);
			g_hash_table_remove(dsmcbe_ppu_Gpointers, data);
			pthread_mutex_unlock(&dsmcbe_ppu_pointer_mutex);
		}
		else
			pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);
		
		if (pe->mode == ACQUIRE_MODE_WRITE)
		{
			//Create a request, this will be released by the coordinator
			re = dsmcbe_new_releaseRequest(pe->id, 0, pe->mode, pe->size, pe->offset, data);

			QueueableItem q = dsmcbe_rc_new_QueueableItem(NULL, NULL, NULL, re, NULL);
	
			dsmcbe_ppu_RelayEnqueItem(q);
		}

		pthread_mutex_lock(&dsmcbe_ppu_pointerOld_mutex);
		if (pe->count == 0 && g_hash_table_lookup(dsmcbe_ppu_GpointersOld, data) == NULL)
			FREE(pe);
		pthread_cond_broadcast(&dsmcbe_ppu_pointerOld_cond);
		pthread_mutex_unlock(&dsmcbe_ppu_pointerOld_mutex);

		dsmcbe_ppu_processInvalidates(NULL);
	}
	else
	{
		pthread_mutex_unlock(&dsmcbe_ppu_pointer_mutex);
		REPORT_ERROR("Pointer given to release was not registered");
	}
}


//This is a very simple thread that exists to remove race conditions
//It ensures that no two requests are overlapping, and processes invalidate requests
void* dsmcbe_ppu_requestDispatcher(void* dummy)
{
	void* data;
	struct dsmcbe_acquireResponse* resp;
	unsigned int reqId;
	QueueableItem ui;
	
	while(!dsmcbe_ppu_terminate)
	{
		pthread_mutex_lock(&dsmcbe_ppu_queue_mutex);
		data = NULL;
		while(!dsmcbe_ppu_terminate && g_queue_is_empty(dsmcbe_ppu_Gwork_queue))
			pthread_cond_wait(&dsmcbe_ppu_queue_cond, &dsmcbe_ppu_queue_mutex);
		
		if (dsmcbe_ppu_terminate)
		{
			pthread_mutex_unlock(&dsmcbe_ppu_queue_mutex);
			return dummy;
		}	
		
		data = g_queue_pop_head(dsmcbe_ppu_Gwork_queue);
		
		pthread_mutex_unlock(&dsmcbe_ppu_queue_mutex);
		
		if (data != NULL)
		{
			switch (((struct dsmcbe_createRequest*)data)->packageCode)
			{
				case PACKAGE_INVALIDATE_REQUEST:
					dsmcbe_ppu_processInvalidates((struct dsmcbe_invalidateRequest*)data);
					data = NULL;
					break;
				case PACKAGE_ACQUIRE_RESPONSE:
					resp = (struct dsmcbe_acquireResponse*)data;
					dsmcbe_ppu_recordPointer(resp->data, resp->dataItem, resp->dataSize, 0, resp->mode != ACQUIRE_MODE_READ ? ACQUIRE_MODE_WRITE : ACQUIRE_MODE_READ);
					break;
			}
		}
		
		if (data != NULL)
		{
			reqId = ((struct dsmcbe_createRequest*)data)->requestID;
		
			pthread_mutex_lock(&dsmcbe_ppu_queue_mutex);
			
			if ((ui = g_hash_table_lookup(dsmcbe_ppu_GpendingRequests, (void*)reqId)) == NULL) {
				REPORT_ERROR("Recieved unexpected request");				
			} else {		
				int freeReq = (((struct dsmcbe_acquireResponse*)data)->packageCode == PACKAGE_ACQUIRE_RESPONSE && ((struct dsmcbe_acquireResponse*)data)->mode != ACQUIRE_MODE_WRITE) || ((struct dsmcbe_acquireResponse*)data)->packageCode != PACKAGE_ACQUIRE_RESPONSE;
				
				if (ui->mutex != NULL)
					pthread_mutex_lock(ui->mutex);
				if (ui->Gqueue != NULL) {
					g_queue_push_tail(*ui->Gqueue, data);
				} else {
					REPORT_ERROR("queue was NULL");
				}
				if (ui->event != NULL)
					pthread_cond_broadcast(ui->event);
				if (ui->mutex != NULL)
					pthread_mutex_unlock(ui->mutex);
				
				if (freeReq)
				{
					g_hash_table_remove(dsmcbe_ppu_GpendingRequests, (void*)reqId);
					FREE(ui);
					ui = NULL;
				}
			}

			pthread_mutex_unlock(&dsmcbe_ppu_queue_mutex);
		}
	}	
	return dummy;
}

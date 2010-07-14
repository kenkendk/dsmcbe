/*
 * Implementation file for CSP functions on the PPU
 */

#include <dsmcbe_csp.h>
#include <dsmcbe_csp_initializers.h>
#include <dsmcbe_initializers.h>
#include <RequestCoordinator.h>
#include <glib.h>
#include <datapackages.h>
#include <stdio.h>
#include <debug.h>
#include <malloc_align.h>
#include <free_align.h>
#include <pthread.h>
#include <dsmcbe_ppu_csp.h>
#include <stdlib.h>

//This module uses this function from PPUEventHandler.c
void* dsmcbe_ppu_forwardRequest(void* data);

//Internal value used to convey a skip response to the read/write operations
#define CSP_CALL_SKIP -20

//This table contains all allocated pointers created by csp_item_create, key is the pointer, value is the size of the item
GHashTable* csp_ppu_allocatedPointers;
//This mutex protects the hashtable
pthread_mutex_t csp_ppu_allocatedPointersMutex;

int dsmcbe_csp_item_create(void** data, size_t size)
{
	if (size == 0)
	{
		REPORT_ERROR("Cannot create an element with size zero");
		return CSP_CALL_ERROR;
	}

	if (data == NULL)
	{
		REPORT_ERROR("Data pointer was NULL");
		return CSP_CALL_ERROR;
	}

	*data = MALLOC_ALIGN(ALIGNED_SIZE(size), 7);

	pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
	g_hash_table_insert(csp_ppu_allocatedPointers, *data, (void*)size);
	pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_csp_channel_create(GUID channelid, unsigned int buffersize, unsigned int type)
{
	struct dsmcbe_cspChannelCreateRequest* req;
	int res = dsmcbe_new_cspChannelCreateRequest(&req, channelid, 0, buffersize, type);
	if (res != CSP_CALL_SUCCESS)
		return res;

	struct dsmcbe_cspChannelCreateResponse* resp = (struct dsmcbe_cspChannelCreateResponse*)dsmcbe_ppu_forwardRequest(req);
	int result = CSP_CALL_SUCCESS;

	if (resp->packageCode == PACKAGE_NACK)
		result = CSP_CALL_ERROR;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		 result = CSP_CALL_POISON;
	else if (resp->packageCode != PACKAGE_CSP_CHANNEL_CREATE_RESPONSE)
	{
		REPORT_ERROR2("Unexpected response to a channel create request: %d", resp->packageCode);
		result = CSP_CALL_ERROR;
	}

	FREE(resp);
	return result;
}

int dsmcbe_csp_channel_poison(GUID channelid)
{
	struct dsmcbe_cspChannelPoisonRequest* req;

	int res = dsmcbe_new_cspChannelPoisonRequest(&req, channelid, 0);
	if (res != CSP_CALL_SUCCESS)
		return res;

	struct dsmcbe_cspChannelPoisonResponse* resp = (struct dsmcbe_cspChannelPoisonResponse*)dsmcbe_ppu_forwardRequest(req);
	int result = CSP_CALL_SUCCESS;

	if (resp->packageCode == PACKAGE_NACK)
		result = CSP_CALL_ERROR;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		 result = CSP_CALL_POISON;
	else if (resp->packageCode != PACKAGE_CSP_CHANNEL_POISON_RESPONSE)
	{
		REPORT_ERROR2("Unexpected response to a channel poison request: %d", resp->packageCode);
		result = CSP_CALL_ERROR;
	}

	FREE(resp);
	return result;
}

int dsmcbe_csp_item_free(void* data)
{
	if (data == NULL)
	{
		REPORT_ERROR("Data pointer was NULL");
		return CSP_CALL_ERROR;
	}

	pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
	if (g_hash_table_lookup(csp_ppu_allocatedPointers, data) == NULL)
	{
		pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
		REPORT_ERROR2("Pointer to free was not known %d", (int)data);
		return CSP_CALL_ERROR;
	}

	g_hash_table_remove(csp_ppu_allocatedPointers, data);
	pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);

	FREE_ALIGN(data);
	return CSP_CALL_SUCCESS;
}

int dsmcbe_csp_channel_write(GUID channelid, void* data)
{
	if (data == NULL)
	{
		REPORT_ERROR("Data pointer was NULL");
		return CSP_CALL_ERROR;
	}

	pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
	size_t size = (size_t)g_hash_table_lookup(csp_ppu_allocatedPointers, data);
	if (size == 0)
	{
		pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
		REPORT_ERROR2("Unable to locate the pointer %d, please use the csp_item_create function to register objects before writing them to the channel", (int)data);
		return CSP_CALL_ERROR;
	}

	g_hash_table_remove(csp_ppu_allocatedPointers, data);
	pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);

	struct dsmcbe_cspChannelWriteRequest* req;
	int res = dsmcbe_new_cspChannelWriteRequest_single(&req, channelid, 0, data, size, FALSE, NULL);
	if (res != CSP_CALL_SUCCESS)
		return res;

	struct dsmcbe_cspChannelWriteResponse* resp = (struct dsmcbe_cspChannelWriteResponse*)dsmcbe_ppu_forwardRequest(req);
	int result = CSP_CALL_SUCCESS;

	if (resp->packageCode == PACKAGE_NACK)
		result = CSP_CALL_ERROR;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		 result = CSP_CALL_POISON;
	else if (resp->packageCode != PACKAGE_CSP_CHANNEL_WRITE_RESPONSE)
	{
		REPORT_ERROR2("Unexpected response to a channel write request: %d", resp->packageCode);
		result = CSP_CALL_ERROR;
	}

	if (result != CSP_CALL_SUCCESS)
		FREE_ALIGN(data);

	FREE(resp);

	return result;
}

void dsmcbe_csp_request_spe_transfer(struct dsmcbe_cspChannelReadResponse* resp)
{
	//We need the SPUEventHandler to transfer the item for us

	//TODO: Bad for performance to keep creating and destroying these
	pthread_mutex_t tmpMutex;
	pthread_cond_t tmpCond;

	pthread_mutex_init(&tmpMutex, NULL);
	pthread_cond_init(&tmpCond, NULL);

	//Prepare the request
	struct dsmcbe_transferRequest* req = dsmcbe_new_transferRequest(&tmpMutex, &tmpCond, resp->data);
	QueueableItem q = resp->transferManager;
	q->dataRequest = NULL;

	dsmcbe_rc_SendMessage(q, req);

	//Wait for response, blocking
	pthread_mutex_lock(&tmpMutex);

	while(req->isTransfered == FALSE)
		pthread_cond_wait(&tmpCond, &tmpMutex);

	resp->data = req->data;
	pthread_mutex_unlock(&tmpMutex);

	//Clean up
	pthread_mutex_destroy(&tmpMutex);
	pthread_cond_destroy(&tmpCond);

	FREE(req);
}

int dsmcbe_csp_channel_read(GUID channelid, size_t* size, void** data)
{
	if (data == NULL)
	{
		REPORT_ERROR("Data pointer was NULL");
		return CSP_CALL_ERROR;
	}

	struct dsmcbe_cspChannelReadRequest* req;
	int res = dsmcbe_new_cspChannelReadRequest_single(&req, channelid, 0);
	if (res != CSP_CALL_SUCCESS)
		return res;

	struct dsmcbe_cspChannelReadResponse* resp = (struct dsmcbe_cspChannelReadResponse*)dsmcbe_ppu_forwardRequest(req);
	int result = CSP_CALL_SUCCESS;

	if (resp->packageCode == PACKAGE_NACK)
		result = CSP_CALL_ERROR;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		 result = CSP_CALL_POISON;
	else if (resp->packageCode != PACKAGE_CSP_CHANNEL_READ_RESPONSE)
	{
		REPORT_ERROR2("Unexpected response to a channel read request: %d", resp->packageCode);
		result = CSP_CALL_ERROR;
	}

	if (result == CSP_CALL_SUCCESS)
	{
		if (resp->onSPE)
			dsmcbe_csp_request_spe_transfer(resp);

		if (size != NULL)
			*size = resp->size;
		*data = resp->data;

		if (*data == NULL)
		{
			REPORT_ERROR2("Response to channel %d read had a NULL pointer", resp->channelId);
			result = CSP_CALL_ERROR;
		}
		else
		{
			pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
			if (g_hash_table_lookup(csp_ppu_allocatedPointers, data) != NULL)
			{
				REPORT_ERROR2("Pointer %d, was already registered?", (int)data);
				result = CSP_CALL_ERROR;
			}
			else
			{
				g_hash_table_insert(csp_ppu_allocatedPointers, resp->data, (void*)resp->size);
			}
			pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
		}
	}

	FREE(resp);

	return result;
}

int dsmcbe_csp_channel_write_alt(unsigned int mode, GUID* channels, size_t channelcount, void* data, GUID* channelid)
{
	if (data == NULL)
	{
		REPORT_ERROR("Data pointer was NULL");
		return CSP_CALL_ERROR;
	}

	if (channels == NULL && channelcount != 0)
	{
		REPORT_ERROR("Channels pointer was NULL");
		return CSP_CALL_ERROR;
	}

	pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
	size_t size = (size_t)g_hash_table_lookup(csp_ppu_allocatedPointers, data);
	if (size == 0)
	{
		pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
		REPORT_ERROR2("Unable to locate the pointer %d, please use the csp_item_create function to register objects before writing them to the channel", (int)data);
		return CSP_CALL_ERROR;
	}

	g_hash_table_remove(csp_ppu_allocatedPointers, data);
	pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);

	struct dsmcbe_cspChannelWriteRequest* req;
	int res = dsmcbe_new_cspChannelWriteRequest_multiple(&req, 0, mode, channels, channelcount, size, data, FALSE, NULL);
	if (res != CSP_CALL_SUCCESS)
		return res;

	struct dsmcbe_cspChannelWriteResponse* resp = (struct dsmcbe_cspChannelWriteResponse*)dsmcbe_ppu_forwardRequest(req);
	int result = CSP_CALL_SUCCESS;

	if (resp->packageCode == PACKAGE_NACK)
		result = CSP_CALL_ERROR;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		 result = CSP_CALL_POISON;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE)
		 result = CSP_CALL_SKIP;
	else if (resp->packageCode != PACKAGE_CSP_CHANNEL_WRITE_RESPONSE)
	{
		REPORT_ERROR2("Unexpected response to a channel write request: %d", resp->packageCode);
		result = CSP_CALL_ERROR;
	}


	if (result != CSP_CALL_SUCCESS)
	{
		//Re-insert the pointer so it is still avalible
		pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
		if (g_hash_table_lookup(csp_ppu_allocatedPointers, data) != NULL)
		{
			pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
			REPORT_ERROR2("Pointer %d was somehow re-used while awating response to an earlier call, this indicates a mis-use of the pointer", (int)data);
			exit(-1);
		}
		g_hash_table_insert(csp_ppu_allocatedPointers, data, (void*)size);
		pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
	}

	if (result == CSP_CALL_SKIP)
	{
		if (channelid != NULL)
			*channelid = CSP_SKIP_GUARD;

		result = CSP_CALL_SUCCESS;
	}
	else if (result == CSP_CALL_SUCCESS)
	{
		if (channelid != NULL)
			*channelid = resp->channelId;
	}

	FREE(resp);

	return result;
}

int dsmcbe_csp_channel_read_alt(unsigned int mode, GUID* channels, size_t channelcount, GUID* channelid, size_t* size, void** data)
{
	if (data == NULL)
	{
		REPORT_ERROR("Data pointer was NULL");
		return CSP_CALL_ERROR;
	}

	if (channels == NULL && channelcount != 0)
	{
		REPORT_ERROR("Channels pointer was NULL");
		return CSP_CALL_ERROR;
	}

	struct dsmcbe_cspChannelReadRequest* req;
	int res = dsmcbe_new_cspChannelReadRequest_multiple(&req, 0, mode, channels, channelcount);
	if (res != CSP_CALL_SUCCESS)
		return res;

	struct dsmcbe_cspChannelReadResponse* resp = (struct dsmcbe_cspChannelReadResponse*)dsmcbe_ppu_forwardRequest(req);
	int result = CSP_CALL_SUCCESS;

	if (resp->packageCode == PACKAGE_NACK)
		result = CSP_CALL_ERROR;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		 result = CSP_CALL_POISON;
	else if (resp->packageCode == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE)
		result = CSP_CALL_SKIP;
	else if (resp->packageCode != PACKAGE_CSP_CHANNEL_READ_RESPONSE)
	{
		REPORT_ERROR2("Unexpected response to a channel read request: %d", resp->packageCode);
		result = CSP_CALL_ERROR;
	}

	if (result == CSP_CALL_SKIP)
	{
		if (size != NULL)
			*size = 0;
		*data = NULL;
		if (channelid != NULL)
			*channelid = CSP_SKIP_GUARD;

		result = CSP_CALL_SUCCESS;
	}
	else if (result == CSP_CALL_SUCCESS)
	{
		if (resp->onSPE)
			dsmcbe_csp_request_spe_transfer(resp);

		if (size != NULL)
			*size = resp->size;
		*data = resp->data;
		if (channelid != NULL)
			*channelid = resp->channelId;

		if (*data == NULL)
		{
			REPORT_ERROR2("Response to channel %d read had a NULL pointer", resp->channelId);
			result = CSP_CALL_ERROR;
		}
		else
		{
			pthread_mutex_lock(&csp_ppu_allocatedPointersMutex);
			if (g_hash_table_lookup(csp_ppu_allocatedPointers, data) != NULL)
			{
				REPORT_ERROR2("Pointer %d, was already registered?", (int)data);
				result = CSP_CALL_ERROR;
			}
			else
			{
				g_hash_table_insert(csp_ppu_allocatedPointers, resp->data, (void*)resp->size);
			}
			pthread_mutex_unlock(&csp_ppu_allocatedPointersMutex);
		}
	}

	FREE(resp);

	return result;
}

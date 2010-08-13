/*
 * Implementation file for CSP functions on the SPU
 */

#include <dsmcbe_csp.h>
#include <datapackages.h>
#include <SPUEventHandler_extrapackages.h>
#include <dsmcbe_spu_internal.h>
#include <limits.h>
#include <debug.h>
#include <stdio.h>

#ifdef SPU_STOP_AND_WAIT
//This is from syscall.h
int __send_to_ppe(unsigned int signalcode, unsigned int opcode, void *data);

#define STOP_AND_WAIT __send_to_ppe(0x2100 | (CSP_STOP_FUNCTION_CODE & 0xff), 1, (void*)2);
#else
#define STOP_AND_WAIT
#endif

int dsmcbe_csp_item_create(void** data, size_t size)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(size);

	*data = spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}


int dsmcbe_csp_item_free(void* data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_ITEM_FREE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX((unsigned int)data);

	spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_write(GUID channelid, void* data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_WRITE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(channelid);
	SPU_WRITE_OUT_MBOX((unsigned int)data);

	STOP_AND_WAIT

	spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_WRITE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_read(GUID channelid, size_t* size, void** data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_READ_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(channelid);

	STOP_AND_WAIT

	*data = spu_dsmcbe_endAsync(nextId, size);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_READ_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_read_alt(unsigned int mode, GUID* channels, size_t channelcount, GUID* channelid, size_t* size, void** data)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(mode);
	SPU_WRITE_OUT_MBOX((unsigned int)channels);
	SPU_WRITE_OUT_MBOX(channelcount);
	SPU_WRITE_OUT_MBOX((unsigned int)channelid);

	STOP_AND_WAIT

	*data = spu_dsmcbe_endAsync(nextId, size);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE)
	{
		if (channelid != NULL)
			*channelid = CSP_SKIP_GUARD;

		return CSP_CALL_SUCCESS;
	}
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_write_alt(unsigned int mode, GUID* channels, size_t channelcount, void* data, GUID* channelid)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(mode);
	SPU_WRITE_OUT_MBOX((unsigned int)channels);
	SPU_WRITE_OUT_MBOX(channelcount);
	SPU_WRITE_OUT_MBOX((unsigned int)data);

	STOP_AND_WAIT

	spu_dsmcbe_endAsync(nextId, (unsigned long*)channelid);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE)
		return CSP_CALL_SUCCESS;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISONED_RESPONSE)
		return CSP_CALL_POISON;
	else if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_SKIP_RESPONSE)
	{
		if (channelid != NULL)
			*channelid = CSP_SKIP_GUARD;

		return CSP_CALL_SUCCESS;
	}
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_poison(GUID channel)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_POISON_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(channel);

	STOP_AND_WAIT

	spu_dsmcbe_endAsync(nextId, NULL);
	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_POISON_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}

int dsmcbe_csp_channel_create(GUID channelid, unsigned int buffersize, unsigned int type)
{
	if (!spu_dsmcbe_initialized)
	{
		REPORT_ERROR("Please call initialize() before calling any DSMCBE functions");
		return CSP_CALL_ERROR;
	}

	unsigned int nextId = spu_dsmcbe_getNextReqNo(PACKAGE_CSP_CHANNEL_CREATE_REQUEST);
	if (nextId == UINT_MAX)
		return CSP_CALL_ERROR;

	SPU_WRITE_OUT_MBOX(spu_dsmcbe_pendingRequests[nextId].requestCode);
	SPU_WRITE_OUT_MBOX(nextId);
	SPU_WRITE_OUT_MBOX(channelid);
	SPU_WRITE_OUT_MBOX(buffersize);
	SPU_WRITE_OUT_MBOX(type);

	spu_dsmcbe_endAsync(nextId, NULL);

	STOP_AND_WAIT

	if (spu_dsmcbe_pendingRequests[nextId].responseCode == PACKAGE_CSP_CHANNEL_CREATE_RESPONSE)
		return CSP_CALL_SUCCESS;
	else
		return CSP_CALL_ERROR;
}

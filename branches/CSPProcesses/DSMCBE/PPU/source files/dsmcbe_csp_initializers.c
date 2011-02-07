/*
 * Internally used functions to initialize various types of CSP communication structures
 */
#include <dsmcbe_csp.h>
#include <datapackages.h>
#include <stdio.h>
#include <debug.h>
#include <memory.h>
#include <NetworkHandler.h>
#include <stdlib.h>

#define SETUP_ORIGINATOR(x) \
	(x)->originator = dsmcbe_host_number; \
	(x)->originalRecipient = UINT_MAX; \
	(x)->originalRequestID = UINT_MAX;

#define COMMON_SETUP(x, code) \
	(x)->packageCode = code;\
	(x)->channelId = channelid;\
	(x)->requestID = requestId;


int dsmcbe_new_cspChannelCreateRequest(struct dsmcbe_cspChannelCreateRequest** result, GUID channelid, unsigned int requestId, unsigned int buffersize, unsigned int type)
{
	switch(type)
	{
		case CSP_CHANNEL_TYPE_ONE2ONE:
		case CSP_CHANNEL_TYPE_ONE2ANY:
		case CSP_CHANNEL_TYPE_ANY2ONE:
		case CSP_CHANNEL_TYPE_ANY2ANY:
		case CSP_CHANNEL_TYPE_ONE2ONE_SIMPLE:
			break;
		default:
			REPORT_ERROR2("Invalid channel type requested: %d", type);
			return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelCreateRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelCreateRequest));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_CREATE_REQUEST);

	(*result)->bufferSize = buffersize;
	(*result)->type = type;

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelPoisonRequest(struct dsmcbe_cspChannelPoisonRequest** result, GUID channelid, unsigned int requestId)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot poison the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelPoisonRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelPoisonRequest));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_POISON_REQUEST);

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelReadRequest_multiple(struct dsmcbe_cspChannelReadRequest** result, unsigned int requestId, unsigned int mode, GUID* channels, size_t channelcount)
{
	*result = (struct dsmcbe_cspChannelReadRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelReadRequest));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	GUID channelid = CSP_SKIP_GUARD;

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_READ_REQUEST);

	(*result)->channelcount = channelcount;
	(*result)->channels = MALLOC(sizeof(GUID) * channelcount);
	memcpy((*result)->channels, channels, sizeof(GUID) * channelcount);
	(*result)->mode = mode;

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelReadRequest_single(struct dsmcbe_cspChannelReadRequest** result, GUID channelid, unsigned int requestId, unsigned int speId)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot read the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelReadRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelReadRequest));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_READ_REQUEST);

	(*result)->channelcount = 0;
	(*result)->channels = NULL;
	(*result)->speId = speId;

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelWriteRequest_multiple(struct dsmcbe_cspChannelWriteRequest** result, unsigned int requestId, unsigned int mode, GUID* channels, size_t channelcount, size_t size, void* data, unsigned int speId, QueueableItem transferManager)
{
	*result = (struct dsmcbe_cspChannelWriteRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelWriteRequest));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	GUID channelid = CSP_SKIP_GUARD;
	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_WRITE_REQUEST);

	(*result)->channelcount = channelcount;
	(*result)->channels = MALLOC(sizeof(GUID) * channelcount);
	memcpy((*result)->channels, channels, sizeof(GUID) * channelcount);

	(*result)->mode = mode;
	(*result)->data = data;
	(*result)->size = size;
	(*result)->speId = speId;
	(*result)->transferManager = transferManager;

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelWriteRequest_single(struct dsmcbe_cspChannelWriteRequest** result, GUID channelid, unsigned int requestId, void* data, size_t size, unsigned int speId, QueueableItem transferManager)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot write the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelWriteRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelWriteRequest));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_WRITE_REQUEST);

	(*result)->channelcount = 0;
	(*result)->channels = NULL;

	(*result)->data = data;
	(*result)->size = size;
	(*result)->speId = speId;
	(*result)->transferManager = transferManager;

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelCreateResponse(struct dsmcbe_cspChannelCreateResponse** result, GUID channelid, unsigned int requestId)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot create the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelCreateResponse*)MALLOC(sizeof(struct dsmcbe_cspChannelCreateResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_CREATE_RESPONSE);
	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelPoisonResponse(struct dsmcbe_cspChannelPoisonResponse** result, GUID channelid, unsigned int requestId)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot poison the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelPoisonResponse*)MALLOC(sizeof(struct dsmcbe_cspChannelPoisonResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_POISON_RESPONSE);

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelPoisonedResponse(struct dsmcbe_cspChannelPoisonedResponse** result, GUID channelid, unsigned int requestId)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot poison the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelPoisonedResponse*)MALLOC(sizeof(struct dsmcbe_cspChannelPoisonedResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_POISONED_RESPONSE);

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelSkipResponse(struct dsmcbe_cspChannelSkipResponse** result, unsigned int requestId)
{
	*result = (struct dsmcbe_cspChannelSkipResponse*)MALLOC(sizeof(struct dsmcbe_cspChannelSkipResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	GUID channelid = CSP_SKIP_GUARD;

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_SKIP_RESPONSE);

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelReadResponse(struct dsmcbe_cspChannelReadResponse** result, GUID channelid, unsigned int requestId, void* data, unsigned int size, unsigned int speId, struct dsmcbe_QueueableItemStruct* transferManager)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot read the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelReadResponse*)MALLOC(sizeof(struct dsmcbe_cspChannelReadResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_READ_RESPONSE);

	(*result)->data = data;
	(*result)->size = size;
	(*result)->speId = speId;
	(*result)->transferManager = transferManager;

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspChannelWriteResponse(struct dsmcbe_cspChannelWriteResponse** result, GUID channelid, unsigned int requestId)
{
	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot write the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspChannelWriteResponse*)MALLOC(sizeof(struct dsmcbe_cspChannelWriteResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_CSP_CHANNEL_WRITE_RESPONSE);

	SETUP_ORIGINATOR(*result);

	return CSP_CALL_SUCCESS;
}

int dsmcbe_new_cspDirectSetupResponse(struct dsmcbe_cspDirectSetupResponse** result, GUID channelid, unsigned int requestId, unsigned int bufferSize, struct dsmcbe_cspChannelWriteRequest* writeRequest, void* pendingWrites)
{

	if (channelid == CSP_SKIP_GUARD || channelid == CSP_TIMEOUT_GUARD)
	{
		REPORT_ERROR2("Cannot write the %s channel", channelid == CSP_SKIP_GUARD ? "skip" : "timeout");
		return CSP_CALL_ERROR;
	}

	*result = (struct dsmcbe_cspDirectSetupResponse*)MALLOC(sizeof(struct dsmcbe_cspDirectSetupResponse));
	if (*result == NULL)
	{
		REPORT_ERROR("Out of memory?");
		return CSP_CALL_ERROR;
	}

	COMMON_SETUP(*result, PACKAGE_DIRECT_SETUP_RESPONSE);

	(*result)->bufferSize = bufferSize;
	(*result)->writeRequest = writeRequest;
	(*result)->pendingWrites = pendingWrites;

	return CSP_CALL_SUCCESS;
}

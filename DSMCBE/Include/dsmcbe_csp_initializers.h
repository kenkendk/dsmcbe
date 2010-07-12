/*
 * Internally used functions to initialize various types of CSP communication structures
 */

#include "datapackages.h"

#ifndef DSMCBE_CSP_INITIALIZERS_H_
#define DSMCBE_CSP_INITIALIZERS_H_

int dsmcbe_new_cspChannelCreateRequest(struct dsmcbe_cspChannelCreateRequest** result, GUID channelid, unsigned int requestId, unsigned int buffersize, unsigned int type);

int dsmcbe_new_cspChannelPoisonRequest(struct dsmcbe_cspChannelPoisonRequest** result, GUID channelid, unsigned int requestId);
int dsmcbe_new_cspChannelReadRequest_multiple(struct dsmcbe_cspChannelReadRequest** result, unsigned int requestId, unsigned int mode, GUID* channels, size_t channelcount);
int dsmcbe_new_cspChannelReadRequest_single(struct dsmcbe_cspChannelReadRequest** result, GUID channelid, unsigned int requestId);

int dsmcbe_new_cspChannelWriteRequest_multiple(struct dsmcbe_cspChannelWriteRequest** result, unsigned int requestId, unsigned int mode, GUID* channels, size_t channelcount, size_t size, void* data, unsigned int onSPE);
int dsmcbe_new_cspChannelWriteRequest_single(struct dsmcbe_cspChannelWriteRequest** result, GUID channelid, unsigned int requestId, void* data, size_t size, unsigned int onSPE);

int dsmcbe_new_cspChannelCreateResponse(struct dsmcbe_cspChannelCreateResponse** result, GUID channelid, unsigned int requestId);
int dsmcbe_new_cspChannelPoisonResponse(struct dsmcbe_cspChannelPoisonResponse** result, GUID channelid, unsigned int requestId);
int dsmcbe_new_cspChannelPoisonedResponse(struct dsmcbe_cspChannelPoisonedResponse** result, GUID channelid, unsigned int requestId);
int dsmcbe_new_cspChannelSkipResponse(struct dsmcbe_cspChannelSkipResponse** result, unsigned int requestId);
int dsmcbe_new_cspChannelReadResponse(struct dsmcbe_cspChannelReadResponse** result, GUID channelid, unsigned int requestId, void* data, unsigned int size, unsigned int onSPE);
int dsmcbe_new_cspChannelWriteResponse(struct dsmcbe_cspChannelWriteResponse** result, GUID channelid, unsigned int requestId);

#endif /* DSMCBE_CSP_INITIALIZERS_H_ */

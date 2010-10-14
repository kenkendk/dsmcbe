#include <dsmcbe_csp.h>
#include <SPUEventHandler.h>
#include <SPUEventHandler_shared.h>

#ifndef SPUEVENTHANDLER_CSP_H_
#define SPUEVENTHANDLER_CSP_H_

//This function handles incoming create channel requests from an SPU
void dsmcbe_spu_csp_HandleChannelCreateRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, unsigned int buffersize, unsigned int type);

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelCreateResponse(struct dsmcbe_spu_state* state, unsigned int packagecode, unsigned int requestId);

//This function handles incoming create channel requests from an SPU
void dsmcbe_spu_csp_HandleChannelPoisonRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id);

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelPoisonResponse(struct dsmcbe_spu_state* state, unsigned int packagecode, unsigned int requestId);

//Attempts to allocate the required amount of memory on the SPU
//If there is not enough memory left, but objects to remove,
// the request is delayed, otherwise a NACK message is sent
//The return value is NULL if the memory could not be allocated right now,
// a pointer otherwise
void* dsmcbe_spu_csp_attempt_get_pointer(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq, unsigned int size);

//This function handles incoming create item requests from an SPU
void dsmcbe_spu_csp_HandleItemCreateRequest(struct dsmcbe_spu_state* state, unsigned int requestId, unsigned int size);

//This function handles incoming free item requests from an SPU
void dsmcbe_spu_csp_HandleItemFreeRequest(struct dsmcbe_spu_state* state, unsigned int requestId, void* data);

//This function handles incoming channel read requests from an SPU
void dsmcbe_spu_csp_HandleChannelReadRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id);

//This function handles incoming channel read requests from an SPU
void dsmcbe_spu_csp_HandleChannelReadRequestAlt(struct dsmcbe_spu_state* state, unsigned int requestId, void* guids, unsigned int count, void* channelId, unsigned int mode);

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelReadResponse(struct dsmcbe_spu_state* state, void* resp);

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequest(struct dsmcbe_spu_state* state, unsigned int requestId, GUID id, void* data);

//This function handles incoming channel write requests from an SPU
void dsmcbe_spu_csp_HandleChannelWriteRequestAlt(struct dsmcbe_spu_state* state, unsigned int requestId, void* data, void* guids, unsigned int count, unsigned int mode);

//This function handles incoming create channel responses from the request coordinator
void dsmcbe_spu_csp_HandleChannelWriteResponse(struct dsmcbe_spu_state* state, void* resp);

//Handles any nack, poison or skip response
void dsmcbe_spu_csp_HandleChannelPoisonNACKorSkipResponse(struct dsmcbe_spu_state* state, void* resp);

//Attempts to flush enough CSP items to get the requested avalible space
unsigned int dsmcbe_spu_csp_FlushItems(struct dsmcbe_spu_state* state, unsigned int requested_size);

//Initiates a transfer request on another SPE
void dsmcbe_spu_csp_RequestTransfer(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq);

//Handles a write request that crossed the directSetup request
void dsmcbe_spu_csp_HandleRoundTripWriteRequest(struct dsmcbe_spu_state* localState, void* _resp);

//Handles a request for setting up a direct transfer
void dsmcbe_spu_csp_HandleDirectSetupResponse(struct dsmcbe_spu_state* state, void* resp);

//Handles a write request on a direct channel
void dsmcbe_spu_csp_HandleDirectWriteRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_directChannelObject* channel, unsigned int requestId, struct dsmcbe_spu_dataObject* obj, unsigned int allowBufferResponse);

//Handles a read request on a direct channel
void dsmcbe_spu_csp_HandleDirectReadRequest(struct dsmcbe_spu_state* state, struct dsmcbe_spu_directChannelObject* channel, unsigned int requestId);

//Handles a completed direct transfer
void dsmcbe_spu_csp_HandleDirectTransferCompleted(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq);

#ifdef SPU_STOP_AND_WAIT
//The callback handler used when stopping the SPE while awaiting data
int dsmcbe_spu_csp_callback(void* ls_base, unsigned int data_ptr);
#endif

#endif /* SPUEVENTHANDLER_CSP_H_ */

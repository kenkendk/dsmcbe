#include <spu_mfcio.h>
#include <dsmcbe.h>
#include "ringbuffer.h"

#ifndef DSMCBE_SPU_INTERNAL_H_
#define DSMCBE_SPU_INTERNAL_H_

//Switch to interrupt mailbox in order to enable the PPU eventhandler
#define SPU_WRITE_OUT_MBOX spu_write_out_intr_mbox
//#define SPU_WRITE_OUT_MBOX spu_write_out_mbox


#define FALSE (0)
#define TRUE (!FALSE)

struct spu_dsmcbe_pendingRequestStruct
{
	//The code the package was created with.
	//Zero means available
	unsigned int requestCode;

	//The response code for the package
	//Zero means available
	unsigned int responseCode;

	//The pointer result
	void* pointer;

	//The resulting size
	unsigned int size;

	//Debugging channelId
	GUID channelId;
};

struct spu_dsmcbe_directChannelStruct
{
	//The channel id
	GUID id;

	//The poison state
	unsigned int poisonState;

	//The read request
	unsigned int readerId;

	//A buffer with pending writes, the size of this buffer is one larger than the buffer size
	// and each entry in the buffer occupies SPE_PENDING_WRITE_SIZE elements
	struct dsmcbe_ringbuffer* pendingWrites;
};

//The total number of direct internal SPE channels available
#define MAX_DIRECT_CHANNELS (64)

//The statically allocated buffer for direct channels
struct spu_dsmcbe_directChannelStruct spu_dsmcbe_directChannels[MAX_DIRECT_CHANNELS];

//The total number of pending requests possible
#define MAX_PENDING_REQUESTS (64)

//The statically allocated buffer for the pending requests
struct spu_dsmcbe_pendingRequestStruct spu_dsmcbe_pendingRequests[MAX_PENDING_REQUESTS];

//The next available request number to use.
//Having this number avoids having to search through the spu_dsmcbe_pendingRequests
unsigned int spu_dsmcbe_nextRequestNo;

//A flag indicating if initialize has been called.
unsigned int spu_dsmcbe_initialized;

//This function gets the next available request number, and sets the response flag to "not ready"
unsigned int spu_dsmcbe_getNextReqNo(unsigned int requestCode);

//Ends an async operation. Blocking if the operation is not complete on entry
void* spu_dsmcbe_endAsync(unsigned int requestNo, unsigned long* size);

//Marks all threads waiting for the given requestId as ready.
//If no thread was activated, the function returns false, and otherwise true
int dsmcbe_thread_set_ready_by_requestId(int requestId);

//Performs a blocking read of the SPU mailbox
void spu_dsmcbe_readMailbox();

//Searches for a direct channel with the given id, returns the direct channel object or NULL
struct spu_dsmcbe_directChannelStruct* dsmcbe_csp_findDirectChannelIndex(GUID channelId);

//Handles a request for read on a local direct channel
void dsmcbe_csp_handleDirectReadRequest(struct spu_dsmcbe_directChannelStruct* channel, unsigned int requestId);

//Handles a request for write on a local direct channel
void dsmcbe_csp_handleDirectWriteRequest(struct spu_dsmcbe_directChannelStruct* channel, unsigned int requestId, void* data, unsigned int size);

//Handles a write request that crosses a direct setup response
void dsmcbe_csp_handleCrossWrite(unsigned int requestID);

//Sets up a direct local channel
void dsmcbe_csp_setupDirectChannel(unsigned int requestId, GUID channelId, void* pendingWrites);

//Poison a channel internally
void dsmcbe_csp_channel_poison_internal(GUID channelId);

#endif /* DSMCBE_SPU_INTERNAL_H_ */

//If this flag is set, the SPE will always transfer data to the EA when
// writing to a channel, this will be faster if there is limited
// spu-spu communication
//#define SPE_CSP_CHANNEL_EAGER_TRANSFER


#ifndef SPUEVENTHANDLER_SHARED_H_
#define SPUEVENTHANDLER_SHARED_H_

#include <glib.h>
#include <SPU_MemoryAllocator.h>
#include "SPUEventHandler_extrapackages.h"
#include <RequestCoordinator.h>

//This is the lowest number a release request can have.
//Make sure that this is larger than the MAX_PENDING_REQUESTS number defined on the SPU
#define RELEASE_NUMBER_BASE (2000)

//The number of pending release requests allowed
#define MAX_PENDING_RELEASE_REQUESTS (500)

//The base id for request'ids for inactive items, must be larger than RELEASE_NUMBER_BASE + MAX_PENDING_RELEASE_REQUESTS
#define CSP_INACTIVE_BASE (4000)

//The max number of inactive items
#define MAX_CSP_INACTIVE_ITEMS (500)

//The number of available DMA group ID's
//NOTE: On the PPU this is 0-15, NOT 0-31 as on the SPU!
#define MAX_DMA_GROUPID (16)

//The number of concurrent pending requests
#define MAX_PENDING_REQUESTS (128)

//This number is the min number of bytes the PPU will transfer over a DMA, smaller requests use an alternate transfer method
//Set to zero to disable alternate transfer methods
#define SPE_DMA_MIN_TRANSFERSIZE (32)

//This structure represents an item on the SPU
struct dsmcbe_spu_dataObject
{
	//The object GUID
	GUID id;
	//The current mode, either READ or WRITE
	unsigned int mode;
	//The number of times this object is acquired (only larger than 1 if mode == READ)
	unsigned int count;
	//The DMAGroupID, or UINT_MAX if there are no active transfers
	unsigned int DMAId;
	//A pointer to the object in main memory
	void* EA;
	//A pointer to the object in the SPU LS
	void* LS;
	//The id of the unanswered invalidate request.
	//If this value is not UINT_MAX, the object is invalid,
	//and will be disposed as soon as it is released
	unsigned int invalidateId;
	//The size of the object
	unsigned long size;

	//This is a flag that determines if the writebuffer is ready for transfer
	unsigned int writebuffer_ready;

	//This is a flag that indicates if the transfer i going from the PPU to the SPU
	unsigned int isDMAToSPU;

	//A flag indicating if the item is a csp item, and thus stored in the csp_items, rather than itemsById
	unsigned int isCSP;

	//The pending request currently assigned to the object
	struct dsmcbe_spu_pendingRequest* preq;

#ifndef SPE_CSP_CHANNEL_EAGER_TRANSFER
	//The current csp sequence number, used to track inactive items
	unsigned int csp_seq_no;
#endif

};

//This structure represents a request that is not yet completed
struct dsmcbe_spu_pendingRequest
{
	//The requestId
	unsigned int requestId;
	//The operation, either:
	// PACKAGE_CREATE_REQUEST,
	// PACKAGE_ACQUIRE_READ_REQUEST,
	// PACKAGE_ACQUIRE_WRITE_REQUEST,
	// PACKAGE_ACQUIRE_RELEASE_REQUEST,
	unsigned int operation;
	//The dataobject involved, may be null if the object is not yet created on the SPU
	GUID objId;
	//This is the number of DMA requests that must complete before the entire transaction is done
	unsigned int DMAcount;

	//A flag indicating if the request is a CSP request
	unsigned int isCSP;

	//A reference to the dataObj that is used when performing DMA transfers
	struct dsmcbe_spu_dataObject* dataObj;

	//A pointer to the result channel, when used with CSP,
	// used to communicate more than the 4 mailbox messages usually allowed
	void* channelPointer;

	//The read response transfer handler
	void* transferHandler;
};

struct dsmcbe_spu_state
{
	//This is a flag indicating that the SPU thread has terminated
	unsigned int terminated;

	//This is a list of all allocated objects on the SPU, key is GUID, value is dataObject*
	GHashTable* itemsById;
	//This is a list of all allocated objects on the SPU, key is LS pointer, value is dataObject*
	GHashTable* itemsByPointer;
	//This is a list of all active DMA transfers, key is DMAGroupID, value is pendingRequest*
	//GHashTable* activeDMATransfers;
	struct dsmcbe_spu_pendingRequest* activeDMATransfers[MAX_DMA_GROUPID];
	//This is an ordered list of object GUID's, ordered so least recently used object is in front
	GQueue* agedObjectMap;
	//This is a queue of all messages sent to the SPU.
	//This simulates a mailbox with infinite capacity
	GQueue* mailboxQueue;
	//This is a map of the SPU LS memory
	SPU_Memory_Map* map;
	//This is the SPU context
	spe_context_ptr_t context;
	//This is the next DMA group ID
	unsigned int dmaSeqNo;
	//This is the next release request id
	unsigned int releaseSeqNo;

	//This is the list of incoming work
	GQueue* inQueue;

	//The incoming queue mutex
	pthread_mutex_t inMutex;

#ifdef USE_EVENTS
	//The incoming queue condition
	pthread_cond_t inCond;
#endif

	//This is a list of acquireRespons packages that cannot be forwarded until a
	//releaseResponse arrives and free's memory
	GQueue* releaseWaiters;

	//This is the stream queue
	GQueue* streamItems;

	//The main thread
	pthread_t main;

#ifdef USE_EVENTS
	//The thread that writes the inbox
	pthread_t inboxWriter;

	//The thread that read DMA events
	pthread_t eventHandler;

	//The mutex that protects the writer queue
	pthread_mutex_t writerMutex;
	//The condition used to signal the writer thread
	pthread_cond_t writerCond;
#endif

	//The list of messages to send to the SPU
	GQueue* writerQueue;
	//A dirty flag used to bypass the SPU writer
	volatile unsigned int writerDirtyReadFlag;
	//Flag indicating if threads should shutdown
	unsigned int shutdown;

	//This is a statically sized list for pending requests,
	// using a static sized list greatly reduces the number of malloc calls
	struct dsmcbe_spu_pendingRequest* pendingRequestsPointer[MAX_PENDING_REQUESTS];
	unsigned int currentPendingRequest;

	//This is a hashtable of active CSP items, key is the pointer in LS, value is a dataObj
	GHashTable* csp_active_items;

#ifndef SPE_CSP_CHANNEL_EAGER_TRANSFER
	//This is a hashtable of inactive CSP items, key is a sequence number, value is a dataObj
	GHashTable* csp_inactive_items;

	//The sequence numbers used to keep track of inactive items
	unsigned int cspSeqNo;
#endif

#ifdef SPU_STOP_AND_WAIT
	pthread_mutex_t csp_sleep_mutex;
	pthread_cond_t csp_sleep_cond;
	unsigned int csp_sleep_flag;
#endif
};

//Warning: Do not change the structure layout as it is used to send data to the SPU's
struct dsmcbe_spu_internalMboxArgs
{
	//The package request code
	unsigned int packageCode;

	//The request id
	unsigned int requestId;

	//The data pointer
	unsigned int data;

	//The data size or channels size
	unsigned int size;

	//The id for the item
	GUID id;

	//The mode, only for CSP ALT
	unsigned int mode;

	//The channel list, only for CSP ALT
	unsigned int channels;

	//The result channelId, only for CSP ALT
	unsigned int channelId;
};


//This is an array of SPU states
struct dsmcbe_spu_state* dsmcbe_spu_states;

//This is the number of SPU's allocated
unsigned int dsmcbe_spu_thread_count;


//Declarations for functions that have interdependencies
void dsmcbe_spu_HandleObjectRelease(struct dsmcbe_spu_state* state, struct dsmcbe_spu_dataObject* obj);
void dsmcbe_spu_HandleDMATransferCompleted(struct dsmcbe_spu_state* state, unsigned int groupID);

void dsmcbe_spu_SendRequestCoordinatorMessage(struct dsmcbe_spu_state* state, void* req);
void dsmcbe_spu_SendMessagesToSPU(struct dsmcbe_spu_state* state, unsigned int packageCode, unsigned int requestId, unsigned int data, unsigned int size);
void* dsmcbe_spu_AllocateSpaceForObject(struct dsmcbe_spu_state* state, unsigned long size);
unsigned int dsmcbe_spu_EstimatePendingReleaseSize(struct dsmcbe_spu_state* state);
void dsmcbe_spu_TransferObject(struct dsmcbe_spu_state* state, struct dsmcbe_spu_pendingRequest* preq, struct dsmcbe_spu_dataObject* obj);

struct dsmcbe_spu_internalMboxArgs* dsmcbe_spu_new_internalMboxArgs(GUID id, unsigned int packageCode, unsigned int requestId, unsigned int size, unsigned int data);
struct dsmcbe_spu_dataObject* dsmcbe_spu_new_dataObject(GUID id, unsigned int isCSP, unsigned int mode, unsigned int count, unsigned int DMAId, void* EA, void* LS, unsigned int invalidateId, unsigned long size, unsigned int writebuffer_ready, unsigned int isDMAToSPU, struct dsmcbe_spu_pendingRequest* preq);
struct dsmcbe_spu_pendingRequest* dsmcbe_spu_new_PendingRequest(struct dsmcbe_spu_state* state, unsigned int requestId, unsigned int operation, GUID objId, struct dsmcbe_spu_dataObject* dataObj, unsigned int DMAcount, unsigned int isCSP);
void dsmcbe_spu_free_PendingRequest(struct dsmcbe_spu_pendingRequest* preq);
struct dsmcbe_spu_pendingRequest* dsmcbe_spu_FindPendingRequest(struct dsmcbe_spu_state* state, unsigned int requestId);

QueueableItem dsmcbe_spu_createTransferManager(struct dsmcbe_spu_state* state);

void dsmcbe_spu_ManageDelayedAllocation(struct dsmcbe_spu_state* state);

void dsmcbe_spu_printMemoryStatus(struct dsmcbe_spu_state* state);

#endif /* SPUEVENTHANDLER_SHARED_H_ */

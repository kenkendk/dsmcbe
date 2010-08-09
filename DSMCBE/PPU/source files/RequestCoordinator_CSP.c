/*
 * Implementation of the CSP coordination
 */

//TODO: When a channel is poisoned, it must be inserted int the queue as a write,
// and not flush until all previous writes are done!

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <debug.h>
#include <RequestCoordinator.h>
#include <dsmcbe_csp_initializers.h>
#include <dsmcbe_csp.h>
#include <stdlib.h>

//This module uses these functions from the RequestCoordinator module
void dsmcbe_rc_RespondAny(QueueableItem item, void* resp);
void dsmcbe_rc_RespondNACK(QueueableItem item);


//This table contains all created channels
GHashTable* dsmcbe_rc_cspChannels;

//This table contains all QueuableItem's that are present in multiple csp channel queues
GHashTable* dsmcbe_rc_cspMultiWaiters;

//Indicates that no poisoning has occured
#define POISON_STATE_NONE 0
//Indicates that poisoning is detected, but is currently buffered
#define POISON_STATE_PENDING 1
//Indicates that the channel is poisoned
#define POISON_STATE_POISONED 2


//This structure defines a single CSP channel
struct dsmcbe_cspChannelStruct {
	//The channel ID
	GUID id;
	//This flag is set to true once the channel has been created
	unsigned int created;
	//The state of poisoning
	unsigned int poisonState;
	//The channel type
	unsigned int type;
	//The size of the built-in write buffer
	unsigned int buffersize;
	//The list of pending read requests
	GQueue* Greaders;
	//The list of pending write requests
	GQueue* Gwriters;
};

typedef struct dsmcbe_cspChannelStruct* cspChannel;

//Initializes a new cspChannelStruct object
cspChannel dsmcbe_rc_csp_createChannelObject(GUID channelId)
{
	cspChannel c = (cspChannel)MALLOC(sizeof(struct dsmcbe_cspChannelStruct));
	c->id = channelId;
	c->poisonState = POISON_STATE_NONE;
	c->type = -1;
	c->buffersize = -1;
	c->created = FALSE;
	c->Greaders = g_queue_new();
	c->Gwriters = g_queue_new();

	g_hash_table_insert(dsmcbe_rc_cspChannels, (void*)channelId, c);

	return c;
}

//Gets (and possibly creates) the channel object
cspChannel dsmcbe_rc_csp_getChannelObject(GUID channelId)
{
	cspChannel c = g_hash_table_lookup(dsmcbe_rc_cspChannels, (void*)channelId);
	if (c == NULL)
		return dsmcbe_rc_csp_createChannelObject(channelId);
	else
		return c;
}

//Sends a response to a channel create request
void dsmcbe_rc_csp_RespondChannelCreate(QueueableItem item)
{
	struct dsmcbe_cspChannelCreateRequest* req = (struct dsmcbe_cspChannelCreateRequest*)item->dataRequest;
	struct dsmcbe_cspChannelCreateResponse* resp;
	if (dsmcbe_new_cspChannelCreateResponse(&resp, req->channelId, req->requestID) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_rc_RespondAny(item, resp);
}

//Sends a response to a channel poison request
void dsmcbe_rc_csp_RespondChannelPoison(QueueableItem item)
{
	struct dsmcbe_cspChannelPoisonRequest* req = (struct dsmcbe_cspChannelPoisonRequest*)item->dataRequest;
	struct dsmcbe_cspChannelPoisonResponse* resp;
	if (dsmcbe_new_cspChannelPoisonResponse(&resp, req->channelId, req->requestID) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_rc_RespondAny(item, resp);
}

//Sends a poisoned notification response
void dsmcbe_rc_csp_RespondChannelPoisoned(QueueableItem item, GUID channelId)
{
	//The used field in both read and write have the same layout
	struct dsmcbe_cspChannelPoisonedResponse* resp;
	if (dsmcbe_new_cspChannelPoisonedResponse(&resp, channelId, ((struct dsmcbe_createRequest*)item->dataRequest)->requestID) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_rc_RespondAny(item, resp);
}

//Sends a skip notification response
void dsmcbe_rc_csp_RespondChannelSkip(QueueableItem item)
{
	//The used field in both read and write have the same layout
	struct dsmcbe_cspChannelSkipResponse* resp;
	if (dsmcbe_new_cspChannelSkipResponse(&resp, ((struct dsmcbe_createRequest*)item->dataRequest)->requestID) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_rc_RespondAny(item, resp);
}

//Sends a write notification
void dsmcbe_rc_csp_RespondChannelWrite(QueueableItem item, GUID channelId)
{
	//The used field in both read and write have the same layout
	struct dsmcbe_cspChannelWriteResponse* resp;
	if (dsmcbe_new_cspChannelWriteResponse(&resp, channelId, ((struct dsmcbe_createRequest*)item->dataRequest)->requestID) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_rc_RespondAny(item, resp);
}

//Sends a read notification
void dsmcbe_rc_csp_RespondChannelRead(QueueableItem item, GUID channelId, void* data, unsigned int size, unsigned int speId, QueueableItem transferManager)
{
	//The used field in both read and write have the same layout
	struct dsmcbe_cspChannelReadResponse* resp;
	if (dsmcbe_new_cspChannelReadResponse(&resp, channelId, ((struct dsmcbe_createRequest*)item->dataRequest)->requestID, data, size, speId, transferManager) != CSP_CALL_SUCCESS)
		exit(-1);

	dsmcbe_rc_RespondAny(item, resp);
}


//Removes the item from all known wait queues
QueueableItem dsmcbe_rc_csp_RemoveMultiPair(QueueableItem item)
{
	GQueue* waiters = (GQueue*)g_hash_table_lookup(dsmcbe_rc_cspMultiWaiters, (void*)item);
	if (waiters != NULL)
	{
		g_hash_table_remove(dsmcbe_rc_cspMultiWaiters, (void*)item);
		while(!g_queue_is_empty(waiters))
		{
			cspChannel chan = (cspChannel)g_queue_pop_head(waiters);
			g_queue_remove(chan->Greaders, item);
			g_queue_remove(chan->Gwriters, item);
		}
		g_queue_free(waiters);
	}

	return item;
}

//Responds all pending reads and writes for the channel with a poison response, and marks the channel as poisoned
void dsmcbe_rc_csp_PoisonAndFlushChannel(cspChannel chan)
{
	chan->poisonState = POISON_STATE_POISONED;

	while(!g_queue_is_empty (chan->Greaders))
		dsmcbe_rc_csp_RespondChannelPoisoned(dsmcbe_rc_csp_RemoveMultiPair((QueueableItem)g_queue_pop_head(chan->Greaders)), chan->id);

	while(!g_queue_is_empty (chan->Gwriters))
		dsmcbe_rc_csp_RespondChannelPoisoned(dsmcbe_rc_csp_RemoveMultiPair((QueueableItem)g_queue_pop_head(chan->Gwriters)), chan->id);

	g_queue_free(chan->Greaders);
	g_queue_free(chan->Gwriters);

	chan->Greaders = NULL;
	chan->Gwriters = NULL;
}

//Handles a match between a read and a write by responding to both
//When this function is called, both requests are removed from their respective queues
void dsmcbe_rc_csp_MatchedReaderAndWriter(cspChannel chan, QueueableItem reader, QueueableItem writer)
{
	dsmcbe_rc_csp_RemoveMultiPair(reader);
	dsmcbe_rc_csp_RemoveMultiPair(writer);

	struct dsmcbe_cspChannelWriteRequest* wrq = (struct dsmcbe_cspChannelWriteRequest*)writer->dataRequest;

	if (wrq->packageCode == PACKAGE_CSP_CHANNEL_POISON_REQUEST)
	{
		dsmcbe_rc_csp_RespondChannelPoison(writer);

		//This request is not in queue, so we respond here
		dsmcbe_rc_csp_RespondChannelPoisoned(reader, chan->id);

		//Update the flag and poison the channel, flushing any remaining items
		dsmcbe_rc_csp_PoisonAndFlushChannel(chan);
	}
	else
	{
		if (writer->Gqueue == NULL)
		{
			dsmcbe_rc_csp_RespondChannelRead(reader, chan->id, wrq->data, wrq->size, wrq->speId, wrq->transferManager);

			//The write request was buffered, so we have already responded, just clean up
			FREE(writer->dataRequest);
			writer->dataRequest = NULL;
			FREE(writer);
			writer = NULL;
		}
		else
		{
			//Respond as normal, we must respond to the write first, then the read,
			// otherwise there is a race with the transferRequest and the writeResponse
			//Unfortunately, when responding to the write request, the request gets
			// free'd, so we need to copy some stuff before we can respond
			void* data = wrq->data;
			size_t size = wrq->size;
			unsigned int speId = wrq->speId;
			QueueableItem transferManager = wrq->transferManager;

			//printf(WHERESTR "Responding to write with pointer @%u\n", WHEREARG, (unsigned int)data);
			dsmcbe_rc_csp_RespondChannelWrite(writer, chan->id);

			//printf(WHERESTR "Responding to read with pointer @%u\n", WHEREARG, (unsigned int)data);
			dsmcbe_rc_csp_RespondChannelRead(reader, chan->id, data, size, speId, transferManager);
		}
	}
}


void dsmcbe_rc_csp_RespondWriteChannelWithCopy(cspChannel chan, QueueableItem item)
{
	//Create a copy of the request, as it is free'd when responding
	struct dsmcbe_cspChannelWriteRequest* req = (struct dsmcbe_cspChannelWriteRequest*)MALLOC(sizeof(struct dsmcbe_cspChannelWriteRequest));
	memcpy(req, item->dataRequest, sizeof(struct dsmcbe_cspChannelWriteRequest));

	//We won't pass these pointers to the response as it may try to free them,
	// and we may need them
	req->channels = NULL;
	req->channelcount = 0;
	req->transferManager = NULL;

	//Respond with the copy
	QueueableItem tmp = dsmcbe_rc_new_QueueableItem(item->mutex, item->event, item->Gqueue, req, item->callback);
	dsmcbe_rc_csp_RespondChannelWrite(tmp, chan->id);

	//Modify the original so we do not respond twice
	item->mutex = NULL;
	item->event = NULL;
	item->Gqueue = NULL;
	item->callback = NULL;
	//item->dataRequest = <original request block>;
}

//Attempts to pair a read request with write requests for the given channel
int dsmcbe_rc_csp_AttemptPairRead(cspChannel chan, QueueableItem item, unsigned int addToQueue)
{
	if ((chan->type == CSP_CHANNEL_TYPE_ONE2ONE || chan->type == CSP_CHANNEL_TYPE_ANY2ONE) && !g_queue_is_empty (chan->Greaders))
	{
		REPORT_ERROR2("Attempted to use multiple readers on single reader channel: %d", chan->id);
		dsmcbe_rc_RespondNACK(item);
		return TRUE;
	}
	else
	{
		if (!chan->created || g_queue_is_empty (chan->Gwriters))
		{
			if (addToQueue)
				g_queue_push_tail(chan->Greaders, item);
			return FALSE;
		}
		else
		{
			QueueableItem writer = g_queue_pop_head(chan->Gwriters);
			dsmcbe_rc_csp_MatchedReaderAndWriter(chan, item, writer);

			//If we are buffered, and have waiting writes flush one
			if (chan->poisonState == POISON_STATE_NONE && chan->buffersize > 0 && g_queue_get_length(chan->Gwriters) >= chan->buffersize)
			{
				QueueableItem n = g_queue_peek_nth(chan->Gwriters, chan->buffersize - 1);
				if (n->Gqueue == NULL)
					REPORT_ERROR("Some odd internal error");

				if (((struct dsmcbe_cspChannelWriteRequest*)n->dataRequest)->packageCode == PACKAGE_CSP_CHANNEL_POISON_REQUEST)
				{
					//Poison requests are blocking
					chan->poisonState = POISON_STATE_PENDING;
				}
				else
				{
					//Respond, don't modify the original
					dsmcbe_rc_csp_RespondWriteChannelWithCopy(chan, n);
				}
			}

			return TRUE;
		}
	}
}

//Attempts to pair a write request with a read requests for the given channel
int dsmcbe_rc_csp_AttemptPairWrite(cspChannel chan, QueueableItem item, unsigned int addToQueue)
{
	//TODO: See if the multiwriter check can be better
	if (chan->created && (chan->type == CSP_CHANNEL_TYPE_ONE2ONE || chan->type == CSP_CHANNEL_TYPE_ONE2ANY) && g_queue_get_length(chan->Gwriters) > chan->buffersize)
	{
		REPORT_ERROR2("Attempted to use multiple writers on single writer channel: %d", chan->id);
		dsmcbe_rc_RespondNACK(item);
		return TRUE;
	}
	else
	{
		if (!chan->created || chan->poisonState != POISON_STATE_NONE)
		{
			if (addToQueue)
				g_queue_push_tail(chan->Gwriters, item);
			return FALSE;
		}
		else if (g_queue_is_empty(chan->Greaders))
		{
			if (g_queue_get_length(chan->Gwriters) < chan->buffersize)
			{
				//Buffer the request, but respond immediately
				dsmcbe_rc_csp_RespondWriteChannelWithCopy(chan, item);

				//Record the copy, so we can pair it later
				g_queue_push_tail(chan->Gwriters, item);
				return TRUE;
			}
			else
			{
				if (addToQueue)
					g_queue_push_tail(chan->Gwriters, item);
				return FALSE;
			}
		}
		else
		{
			dsmcbe_rc_csp_MatchedReaderAndWriter(chan, g_queue_pop_head(chan->Greaders), item);
			return TRUE;
		}
	}
}

//Handles an incoming CSP Channel Create Request
void dsmcbe_rc_csp_ProcessChannelCreateRequest(QueueableItem item)
{
	struct dsmcbe_cspChannelCreateRequest* req = (struct dsmcbe_cspChannelCreateRequest*)item->dataRequest;

	cspChannel chan = dsmcbe_rc_csp_getChannelObject(req->channelId);

	if (chan->created)
	{
		REPORT_ERROR2("Attempted to create an already existing channel: %d", req->channelId);
		dsmcbe_rc_RespondNACK(item);
	}
	else
	{
		//Setup the channel for work
		chan->buffersize = req->bufferSize;
		chan->type = req->type;
		chan->created = TRUE;

		dsmcbe_rc_csp_RespondChannelCreate(item);

		//Empty pending read/write pairs, we check on the writer side, as that may also be buffered
		while(!g_queue_is_empty (chan->Gwriters))
		{
			QueueableItem w = g_queue_pop_head(chan->Gwriters);
			if (!dsmcbe_rc_csp_AttemptPairWrite(chan, w, TRUE))
				break; //It gets re-inserted in the AttemptPairWrite function
		}
	}
}

//Handles an incoming CSP poison request
void dsmcbe_rc_csp_ProcessChannelPoisonRequest(QueueableItem item)
{
	struct dsmcbe_cspChannelPoisonRequest* req = (struct dsmcbe_cspChannelPoisonRequest*)item->dataRequest;

	cspChannel chan = dsmcbe_rc_csp_getChannelObject(req->channelId);

	if (!chan->created)
	{
		REPORT_ERROR2("Attempted to poison a non-existing channel: %d", chan->id);
		dsmcbe_rc_RespondNACK(item);
	}
	else if (chan->poisonState != POISON_STATE_NONE)
	{
		REPORT_ERROR2("Attempted to re-poison a channel: %d", chan->id);
		dsmcbe_rc_RespondNACK(item);
	}
	else
	{
		//If there is no buffer, the pending writes can be seen as having happened
		// at the same logical time as the poison request
		if (chan->buffersize == 0 || g_queue_is_empty(chan->Gwriters))
		{
			//If we have no pending writers, just poison the channel
			dsmcbe_rc_csp_PoisonAndFlushChannel(chan);
			dsmcbe_rc_csp_RespondChannelPoison(item);
		}
		else
		{
			//We have pending writers, so insert the poison request as if it was a write

			//If we are writing within the buffer range, the poison is now pending
			// otherwise, it will be discovered at some later point
			if (chan->buffersize > g_queue_get_length(chan->Gwriters))
				chan->poisonState = POISON_STATE_PENDING;

			g_queue_push_tail(chan->Gwriters, item);
		}
	}
}

//Handles an incoming CSP read or write request
void dsmcbe_rc_csp_ProcessChannelReadOrWriteRequest(QueueableItem item, unsigned int isWrite)
{
	size_t i;

	GUID channelId = (isWrite ? ((struct dsmcbe_cspChannelWriteRequest*)item->dataRequest)->channelId : ((struct dsmcbe_cspChannelReadRequest*)item->dataRequest)->channelId);
	unsigned int channelcount = (isWrite ? ((struct dsmcbe_cspChannelWriteRequest*)item->dataRequest)->channelcount : ((struct dsmcbe_cspChannelReadRequest*)item->dataRequest)->channelcount);
	unsigned int mode = (isWrite ? ((struct dsmcbe_cspChannelWriteRequest*)item->dataRequest)->mode : ((struct dsmcbe_cspChannelReadRequest*)item->dataRequest)->mode);

	int (*pairfunc)(cspChannel chan, QueueableItem item, unsigned int addToQueue) = isWrite ? dsmcbe_rc_csp_AttemptPairWrite : dsmcbe_rc_csp_AttemptPairRead;

	if (channelcount == 0)
	{
		//printf("--------- Got %s request for channel %d\n", isWrite ? "write" : "read", channelId);
		//Single channel mode

		cspChannel chan = dsmcbe_rc_csp_getChannelObject(channelId);

		if (chan->poisonState == POISON_STATE_POISONED)
			dsmcbe_rc_csp_RespondChannelPoisoned(item, chan->id);
		else
			pairfunc(chan, item, TRUE);
	}
	else
	{
		GUID** channelIds = (isWrite ? &((struct dsmcbe_cspChannelWriteRequest*)item->dataRequest)->channels : &((struct dsmcbe_cspChannelReadRequest*)item->dataRequest)->channels);
		cspChannel* channels = MALLOC(sizeof(cspChannel) * channelcount);

		unsigned int hasSkip = FALSE;
		unsigned int hasTimeout = FALSE;

		//First we need to verify that all the channels exist
		for(i = 0; i < channelcount; i++)
		{
			if ((*channelIds)[i] == CSP_SKIP_GUARD || (*channelIds)[i] == CSP_TIMEOUT_GUARD)
			{
				channels[i] = NULL;
				hasSkip = TRUE;
				if (i != channelcount - 1)
					REPORT_ERROR2("The %s guard must be the last channel in the list", (*channelIds)[i] == CSP_SKIP_GUARD ? "SKIP" : "TIMEOUT");

				channelcount = i;
				break;
			}
			else
			{
				channels[i] = dsmcbe_rc_csp_getChannelObject((*channelIds)[i]);

				//If any channel in the set is poisoned, we return poison
				if (channels[i]->poisonState == POISON_STATE_POISONED)
				{
					FREE(*channelIds);
					(*channelIds) = NULL;

					dsmcbe_rc_csp_RespondChannelPoisoned(item, channels[i]->id);

					FREE(channels);
					return;
				}
			}
		}

		if (hasSkip && hasTimeout)
			REPORT_ERROR("Cannot supply both SKIP and TIMEOUT guards");

		if (hasTimeout)
			REPORT_ERROR("Timeout guards are not currently supported!");

		FREE(*channelIds);
		(*channelIds) = NULL;

		//TODO: Re-order the list according to ALT mode
		if (mode != CSP_ALT_MODE_PRIORITY)
			REPORT_ERROR2("DSMCBE CSP only supports CSP_ALT_MODE_PRIORITY, request for channel %d has a different mode", channelId);

		GQueue* insertedWaits = hasSkip ? NULL : g_queue_new();
		int responded = FALSE;

		//Then we must insert the entry into all queues
		for(i = 0; i < channelcount; i++)
			if (!pairfunc(channels[i], item, !hasSkip))
			{
				if (!hasSkip)
					g_queue_push_tail(insertedWaits, channels[i]);
			}
			else
			{
				responded = TRUE;
				break;
			}


		if (responded)
		{
			//The read was handled, so roll back any queue insertions in other channels
			if (insertedWaits != NULL)
			{
				while(!g_queue_is_empty(insertedWaits))
				{
					cspChannel chan = (cspChannel)g_queue_pop_head(insertedWaits);
					g_queue_remove(isWrite ? chan->Gwriters : chan->Greaders, item);
				}

				g_queue_free(insertedWaits);
			}
		}
		else if (hasSkip)
		{
			//There was a skip guard and no action was taken
			dsmcbe_rc_csp_RespondChannelSkip(item);
		}
		else
		{
			//The action was not handled, so record the multiwait
			g_hash_table_insert(dsmcbe_rc_cspMultiWaiters, (void*)item, (void*)insertedWaits);
		}

		FREE(channels);
	}

	#undef ACTUAL_OBJ
}

//Handles an incoming CSP read request
void dsmcbe_rc_csp_ProcessChannelReadRequest(QueueableItem item)
{
	dsmcbe_rc_csp_ProcessChannelReadOrWriteRequest(item, FALSE);
}

//Handles an incoming CSP write request
void dsmcbe_rc_csp_ProcessChannelWriteRequest(QueueableItem item)
{
	dsmcbe_rc_csp_ProcessChannelReadOrWriteRequest(item, TRUE);
}


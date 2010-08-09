#include "dsmcbe_csp.h"
#include "RequestCoordinator.h"

#ifndef REQUESTCOORDINATOR_CSP_H_
#define REQUESTCOORDINATOR_CSP_H_

//This table contains all created channels, key is the channel id, value is a cspChannel
extern GHashTable* dsmcbe_rc_cspChannels;

//This table contains all QueuableItem's that are present in multiple csp channel queues
//Key is the QueueableItem and value is the cspChannel
extern GHashTable* dsmcbe_rc_cspMultiWaiters;

void dsmcbe_rc_csp_ProcessChannelCreateRequest(QueueableItem item);
void dsmcbe_rc_csp_ProcessChannelPoisonRequest(QueueableItem item);
void dsmcbe_rc_csp_ProcessChannelReadRequest(QueueableItem item);
void dsmcbe_rc_csp_ProcessChannelWriteRequest(QueueableItem item);

#endif /* REQUESTCOORDINATOR_CSP_H_ */

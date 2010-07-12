#ifndef NETWORKHANDLER_H_
#define NETWORKHANDLER_H_

#include "RequestCoordinator.h"

void dsmcbe_net_initialize(int* handles, unsigned int host_count);
void dsmcbe_net_terminate(int force);

void dsmcbe_net_Update(GUID id, unsigned int offset, unsigned int dataSize, void* data);
void dsmcbe_net_Request(QueueableItem item, unsigned int machineId);
void dsmcbe_net_Unsubscribe(GUID dataitem, unsigned int machineId);

extern OBJECT_TABLE_ENTRY_TYPE dsmcbe_host_number;

#endif /*NETWORKHANDLER_H_*/

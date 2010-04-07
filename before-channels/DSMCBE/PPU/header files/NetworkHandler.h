#ifndef NETWORKHANDLER_H_
#define NETWORKHANDLER_H_

#include "RequestCoordinator.h"

void InitializeNetworkHandler(int* handles, unsigned int host_count);
void TerminateNetworkHandler(int force);

OBJECT_TABLE_ENTRY_TYPE GetMachineID(GUID itemId);

void NetUpdate(GUID id, unsigned int offset, unsigned int dataSize, void* data);
void NetRequest(QueueableItem item, unsigned int machineId);
void NetUnsubscribe(GUID dataitem, unsigned int machineId);

extern OBJECT_TABLE_ENTRY_TYPE dsmcbe_host_number;

#endif /*NETWORKHANDLER_H_*/

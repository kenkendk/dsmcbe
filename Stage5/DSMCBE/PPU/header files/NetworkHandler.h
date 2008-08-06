#ifndef NETWORKHANDLER_H_
#define NETWORKHANDLER_H_

#include "RequestCoordinator.h"

void InitializeNetworkHandler(int* handles, unsigned int host_count);
void TerminateNetworkHandler(int force);

unsigned int GetMachineID(GUID itemId);
int SetMachineID(GUID itemId, unsigned int machineId, unsigned int expectedId);

void NetInvalidate(GUID id);
void NetRequest(QueueableItem item, unsigned int machineId);
void NetUnsubscribe(GUID dataitem, unsigned int machineId);

extern unsigned int dsmcbe_host_number;

#endif /*NETWORKHANDLER_H_*/

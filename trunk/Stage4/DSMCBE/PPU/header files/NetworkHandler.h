#ifndef NETWORKHANDLER_H_
#define NETWORKHANDLER_H_

#define PAGE_TABLE_ID 0

#include "RequestCoordinator.h"

void InitializeNetworkHandler(int* handles, unsigned int host_count);
void TerminateNetworkHandler(int force);
unsigned int GetMachineID(GUID itemId);
void NetInvalidate(GUID id);
void NetRequest(QueueableItem item);

extern unsigned int dsmcbe_host_number;

#endif /*NETWORKHANDLER_H_*/

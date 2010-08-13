This project is used to measure how fast the internal communications
methods are, and how fast DSMCBE is, compared to that.

You must compile the application with a #DEFINE specified on the commandline
or in the Shared.h file.

The following measure methods are avalible:
#define MBOX_MODE
Runs the itterations with mailbox communication.
Requires no commandline arguments.

#define DMA_MODE
Uses DMA transfers to transfer the itteration counter
Requires no commandline arguments.

#define NET_MODE
Uses the loopback adress to simulate network transfer of the counter
Requires no commandline arguments.

#define DSM_MODE
Uses DSMCBE on multiple machines to transfer the counter value.
Requires the machine id as a commandline argument, and a file named network.txt to be present.

#define DSM_MODE_SINGLE
Uses DSMCBE on to transfer the counter value between two SPU processors located on the same machine.
#include <spu_mfcio.h>

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
};

//The total number of pending requests possible
#define MAX_PENDING_REQUESTS 16

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

#endif /* DSMCBE_SPU_INTERNAL_H_ */

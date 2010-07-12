#include <errno.h>
#include <string.h>
#define WHERESTR "[file %s, line %d]: "
#define WHEREARG __FILE__,__LINE__

#define REPORT_ERROR(x) fprintf(stderr, "* ERROR * " WHERESTR x ": %s, (%i)\n", WHEREARG, strerror(errno), errno);
#define REPORT_ERROR2(x,y) fprintf(stderr, "* ERROR * " WHERESTR x ": %s, (%i)\n", WHEREARG, y, strerror(errno), errno);

#define PACKAGE_NAME(x) (x == 0 ? "Invalid" : \
		( x == 1 ? "CreateRequest" : \
		( x == 2 ? "AcquireRequestRead" : \
		( x == 3 ? "AcquireRequestWrite" : \
		( x == 4 ? "AcquireResponse" : \
		( x == 5  ? "WriteBufferReady" : \
		( x == 6 ? "MigrationResponse" : \
		( x == 7 ? "ReleaseRequest" : \
		( x == 8 ? "ReleaseResponse" : \
		( x == 9 ? "NACK" : \
		( x == 10 ? "InvalidateRequest" : \
		( x == 11 ? "InvalidateResponse" : \
		( x == 16 ? "AcquireBarrierRequest" : \
		( x == 17 ? "AcquireBarrierResponse" : \
		(x == 50 ? "CspChannelCreateRequest": \
		(x == 51 ? "CspChannelCreateResponse" : \
		(x == 52 ? "CspChannelPoisonRequest" : \
		(x == 53 ? "CspChannelPoisonResponse" : \
		(x == 54 ? "CspChannelReadRequest": \
		(x == 55 ? "CspChannelReadResponse" : \
		(x == 56 ? "CspChannelWriteRequest" : \
		(x == 57 ? "CspChannelWriteResponse" : \
		(x == 58 ? "CspChannelPoisonedResponse" : \
		(x == 59 ? "CspChannelSkipResponse" : \
		(x == 60 ? "MallocRequest" : \
		(x == 61 ? "TransferRequest" : \
		(x == 70 ? "DMAComplete" : \
		(x == 20 ? "SPUMallocSetup" : \
		(x == 21 ? "SPUMallocRequest" : \
		(x == 22 ? "SPUMallocResponse" : \
		(x == 23 ? "SPUMallocFree" : \
				"Unknown")))))))))))))))))))))))))))))))

//#define DEBUG_COMMUNICATION
//#define EVENT_BASED
//#define DEBUG_FRAGMENTATION

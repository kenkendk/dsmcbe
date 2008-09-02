#include <errno.h>
#include <string.h>
#define WHERESTR "[file %s, line %d]: "
#define WHEREARG __FILE__,__LINE__

#define REPORT_ERROR(x) fprintf(stderr, "* ERROR * " WHERESTR x ": %s, (%i)\n", WHEREARG, strerror(errno), errno);

#define PACKAGE_NAME(x) (x == 0 ? "Invalid" : (x == 1 ? "CreateRequest" : ( x == 2 ? "AcquireRead" : ( x == 3 ? "AcquireWrite" : ( x == 4 ? "AcquireResponse" : ( x == 5  ? "WriteBufferReady" : ( x == 6 ? "MigrationResponse" : ( x == 7 ? "ReleaseRequest" : ( x == 8 ? "ReleaseResponse" : ( x == 9 ? "NACK" : ( x == 10 ? "InvalidateRequest" : ( x == 11 ? "InvalidateResponse" : "Unknown > 10"))))))))))))

//#define DEBUG_COMMUNICATION
//#define EVENT_BASED
#define DEBUG_FRAGMENTATION

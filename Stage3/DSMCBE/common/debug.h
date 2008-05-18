#include <errno.h>
#include <string.h>
#define WHERESTR "[file %s, line %d]: "
#define WHEREARG __FILE__,__LINE__

#if DSMCBE_SPU
extern unsigned int SPUNO;
extern unsigned int THREADNO;
#define REPORT_ERROR(x) fprintf(stderr, WHERESTR x ": %s, Thread %d:%d\n", WHEREARG, strerror(errno), SPUNO, THREADNO);
#else
#define REPORT_ERROR(x) fprintf(stderr, WHERESTR x ": %s\n", WHEREARG, strerror(errno));
#endif /*DSMCBE_SPU*/

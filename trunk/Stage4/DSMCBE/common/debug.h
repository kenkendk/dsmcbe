#include <errno.h>
#include <string.h>
#define WHERESTR "[file %s, line %d]: "
#define WHEREARG __FILE__,__LINE__

#define REPORT_ERROR(x) fprintf(stderr, "* ERROR * " WHERESTR x ": %s\n", WHEREARG, strerror(errno));

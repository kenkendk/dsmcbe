#include <stdlib.h>
#include <spu_mfcio.h>
#include <math.h>
#include <libmisc.h>
#include <dsmcbe_spu.h>
#include <dsmcbe_csp.h>
#include "../PPU/header_files/guids.h"
#include "../PPU/header_files/Shared.h"
#include <debug.h>
#include <unistd.h>
#include <limits.h>
#include <dsmcbe_spu_internal.h>

//HACK: Inject the code for the solver in the SPU
#include "../PPU/source_files/RowSolver.c"

int dsmcbe_main(unsigned long long speid, unsigned int machineId, unsigned int threadId)
{
	UNUSED(speid);
	UNUSED(machineId);
	UNUSED(threadId);

	runSolver();

	return 0;
}

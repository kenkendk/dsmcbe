#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include <time.h>
#include "csp_commons.h"

int csp_thread_main();

int main(int argc, char** argv) {
	
	dsmcbe_initialize();

	csp_thread_main();
	
	dsmcbe_terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


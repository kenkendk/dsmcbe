#include <pthread.h>
#include <glib.h>

#ifndef DSMCBE_PPU_CSP_H_
#define DSMCBE_PPU_CSP_H_

extern GHashTable* dsmcbe_ppu_csp_allocatedPointers;
extern pthread_mutex_t dsmcbe_ppu_csp_allocatedPointersMutex;

#endif /* DSMCBE_PPU_CSP_H_ */

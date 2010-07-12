#include <pthread.h>
#include <glib.h>

#ifndef DSMCBE_PPU_CSP_H_
#define DSMCBE_PPU_CSP_H_

extern GHashTable* csp_ppu_allocatedPointers;
extern pthread_mutex_t csp_ppu_allocatedPointersMutex;

#endif /* DSMCBE_PPU_CSP_H_ */

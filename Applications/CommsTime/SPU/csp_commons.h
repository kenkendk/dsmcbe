#include <dsmcbe.h>
#include <dsmcbe_csp.h>

#ifndef CSP_COMMONS_H_
#define CSP_COMMONS_H_

int delta2(GUID in, GUID outA, GUID outB);
int delta1(GUID in, GUID out);
int prefix(GUID in, GUID out, void* data);
int tail(GUID in, GUID out);
int combine(GUID inA, GUID inB, GUID out, void* (*combineFunc)(void*, size_t, void*, size_t));

#endif /* CSP_COMMONS_H_ */

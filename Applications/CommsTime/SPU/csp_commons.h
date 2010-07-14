#include <dsmcbe.h>
#include <dsmcbe_csp.h>

#ifndef CSP_COMMONS_H_
#define CSP_COMMONS_H_

int delta2(GUID in, GUID outA, GUID outB);
int delta1(GUID in, GUID out);
int prefix(GUID in, GUID out, void* data);

#endif /* CSP_COMMONS_H_ */

#include "csp_commons.h"
#include <dsmcbe.h>
#include <stdlib.h>
#include <string.h> //Defines memcpy

int delta2(GUID in, GUID outA, GUID outB)
{
	unsigned int* inValue;
	unsigned int* outValue;
	size_t size;

	while(1)
	{
		CSP_SAFE_CALL("delta2 inValue read", dsmcbe_csp_channel_read(in, &size, (void**)&inValue));

		CSP_SAFE_CALL("delta2 allocate memory for output", dsmcbe_csp_item_create((void**)&outValue, size));

		memcpy(outValue, inValue, size);

		CSP_SAFE_CALL("delta2 outA write", dsmcbe_csp_channel_write(outA, inValue));

		CSP_SAFE_CALL("delta2 outB write", dsmcbe_csp_channel_write(outB, outValue));
	}
}

int delta1(GUID in, GUID out)
{
	unsigned int* value;

	while(1)
	{
		CSP_SAFE_CALL("delta1 read", dsmcbe_csp_channel_read(in, NULL, (void**)&value));

		CSP_SAFE_CALL("delta1 write", dsmcbe_csp_channel_write(out, value));
	}

	return CSP_CALL_SUCCESS;
}

int prefix(GUID in, GUID out, void* data)
{
	CSP_SAFE_CALL("prefix write", dsmcbe_csp_channel_write(out, data));

	return delta1(in, out);
}


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

int tail(GUID in, GUID out)
{
	//Consume the first entry
	void* tmp;

	CSP_SAFE_CALL("tail consume", dsmcbe_csp_channel_read(in, NULL, &tmp));

	CSP_SAFE_CALL("tail free", dsmcbe_csp_item_free(tmp));

	return delta1(in, out);
}

int combine(GUID inA, GUID inB, GUID out, void* (*combineFunc)(void*, size_t, void*, size_t))
{
	void *data1, *data2, *outData;
	size_t size1, size2;

	GUID channelList[2];
	channelList[0] = inA;
	channelList[1] = inB;

	GUID selectedChannel;

	while(1)
	{
		CSP_SAFE_CALL("combine read alt", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, channelList, 2, &selectedChannel, &size1, &data1));
		CSP_SAFE_CALL("combine read other", dsmcbe_csp_channel_read(selectedChannel == inA ? inB : inA, &size2, &data2));

		if (selectedChannel == inA)
			outData = combineFunc(data1, size1, data2, size2);
		else
			outData = combineFunc(data2, size2, data1, size1);

		if (outData != data1)
			CSP_SAFE_CALL("release item 1", dsmcbe_csp_item_free(data1));

		if (outData != data2)
			CSP_SAFE_CALL("release item 2", dsmcbe_csp_item_free(data2));

		CSP_SAFE_CALL("combine write", dsmcbe_csp_channel_write(out, outData));
	}
}

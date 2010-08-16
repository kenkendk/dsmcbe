#include "spu.h"
#include <debug.h>
#include <unistd.h>
#include <malloc.h>
#include <malloc_align.h>
#include <free_align.h>
#include <time.h>
#include "../PPU/guids.h"

int copyItem(void* dataItem, size_t size, GUID targetChannel)
{
	void* copy;
	int res;

	CSP_SAFE_CALL("create copy", dsmcbe_csp_item_create(&copy, size));
	memcpy(copy, dataItem, size);
	res = dsmcbe_csp_channel_write(targetChannel, copy);
	if (res != CSP_CALL_SUCCESS)
		dsmcbe_csp_item_free(copy);

	return res;
}

int main(int argc, char** argv) {
	
	unsigned int i;
	float a;

	unsigned int procId;
	unsigned int procCount;
	unsigned int workCount;
	unsigned int dataSize;

	GUID readChannel;
	GUID writeChannel;

	void* data;

	dsmcbe_initialize();

	
	CSP_SAFE_CALL("get setup", dsmcbe_csp_channel_read(SETUP_CHANNEL, NULL, &data));
	procId = ((int*)data)[0];
	procCount = ((int*)data)[1];
	workCount = ((int*)data)[2];
	dataSize = ((int*)data)[3];

	readChannel = (procId % procCount) + RING_CHANNEL_BASE;
	writeChannel = ((procId + 1) % procCount) + RING_CHANNEL_BASE;

	CSP_SAFE_CALL("create work channel", dsmcbe_csp_channel_create(readChannel, procId == procCount - 1 ? 1 : 0, CSP_CHANNEL_TYPE_ONE2ONE));

	CSP_SAFE_CALL("create comm block", dsmcbe_csp_item_create(&data, dataSize));

	while(1)
	{
		if(dsmcbe_csp_channel_write(writeChannel, data) != CSP_CALL_SUCCESS)
		{
			CSP_SAFE_CALL("free copy", dsmcbe_csp_item_free(data));
			CSP_SAFE_CALL("forward poison", dsmcbe_csp_channel_poison(readChannel));
			break;
		}

		CSP_SAFE_CALL("read data", dsmcbe_csp_channel_read(readChannel, NULL, &data));

		if (procId == 0)
			if (copyItem(data, dataSize, DELTA_CHANNEL) != CSP_CALL_SUCCESS)
			{
				//We got poison from PPU
				CSP_SAFE_CALL("free copy", dsmcbe_csp_item_free(data));
				CSP_SAFE_CALL("forward poison", dsmcbe_csp_channel_poison(readChannel));
				break;
			}

		//Simulate some amount of work
		a = 1.1;
		for(i = 0; i < workCount; i++)
			a = (a + i) * 0.1;

		((float*)data)[0] = a;
	}

	dsmcbe_terminate();
	
	//Remove compiler warnings
	argc = 0;
	argv = NULL;
	
	return 0;
}


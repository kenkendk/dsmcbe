//#define DEBUG

#include <dsmcbe_csp.h>
#include "csp_commons.h"
#include "../PPU/guids.h"
#include <stdlib.h>

//This is the CommsTime CSP main function
int csp_thread_main()
{
	unsigned int pid;
	unsigned int processcount;

	unsigned int* tmp;

	CSP_SAFE_CALL("read processid", dsmcbe_csp_channel_read(PROCESS_COUNTER_GUID, NULL, (void**)&tmp));

	pid = (tmp[0])++;
	processcount = tmp[1];

	if (tmp[0] != tmp[1])
	{
		CSP_SAFE_CALL("write processid", dsmcbe_csp_channel_write(PROCESS_COUNTER_GUID, tmp));
	}
	else
	{
		CSP_SAFE_CALL("free processid", dsmcbe_csp_item_free(tmp));
	}

	//Test the skip channel feature
	GUID testchans[] = {BUFFERED_CHANNEL_START + pid, CSP_SKIP_GUARD};
	GUID selected_chan = 777;
	void* test_data;

	CSP_SAFE_CALL("test skip non-existing-channel", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
	if (selected_chan != CSP_SKIP_GUARD)
		printf("**** ERROR: ALT SKIP read is broken :(\n");

	CSP_SAFE_CALL("test create buffered channel", dsmcbe_csp_channel_create(BUFFERED_CHANNEL_START + pid, 1, CSP_CHANNEL_TYPE_ONE2ONE));

	CSP_SAFE_CALL("test skip existing-channel", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
	if (selected_chan != CSP_SKIP_GUARD)
		printf("**** ERROR: ALT SKIP read is broken :(\n");

	CSP_SAFE_CALL("test createitem", dsmcbe_csp_item_create(&test_data, 4));
	CSP_SAFE_CALL("test write buffered", dsmcbe_csp_channel_write(BUFFERED_CHANNEL_START + pid, test_data));

	CSP_SAFE_CALL("test createitem2", dsmcbe_csp_item_create(&test_data, 4));
	CSP_SAFE_CALL("test alt-write buffered", dsmcbe_csp_channel_write_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, test_data, &selected_chan));
	if (selected_chan != CSP_SKIP_GUARD)
		printf("**** ERROR: ALT SKIP write is broken :(\n");
	CSP_SAFE_CALL("test freeitem2", dsmcbe_csp_item_free(test_data));

	CSP_SAFE_CALL("test alt-read buffered", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 2, &selected_chan, NULL, &test_data));
	if (selected_chan != BUFFERED_CHANNEL_START + pid)
		printf("**** ERROR: ALT read is broken :(\n");

	CSP_SAFE_CALL("test alt-write buffered", dsmcbe_csp_channel_write_alt(CSP_ALT_MODE_PRIORITY, testchans, 1, test_data, &selected_chan));
	if (selected_chan != BUFFERED_CHANNEL_START + pid)
		printf("**** ERROR: ALT write is broken :(\n");
	CSP_SAFE_CALL("test alt-read buffered", dsmcbe_csp_channel_read_alt(CSP_ALT_MODE_PRIORITY, testchans, 1, &selected_chan, NULL, &test_data));
	if (selected_chan != BUFFERED_CHANNEL_START + pid)
		printf("**** ERROR: ALT read is broken :(\n");
	CSP_SAFE_CALL("test freeitem", dsmcbe_csp_item_free(test_data));



	GUID readerChannel = CHANNEL_START_GUID + (pid % processcount);
	GUID writerChannel = CHANNEL_START_GUID + ((pid + 1) % processcount);

	CSP_SAFE_CALL("create start channel", dsmcbe_csp_channel_create(readerChannel, 0, CSP_CHANNEL_TYPE_ONE2ONE));

	int call_result;

	if (pid == 0)
		call_result = delta2(readerChannel, writerChannel, DELTA_CHANNEL);
	else if (pid == 1)
		call_result = prefix(readerChannel, writerChannel, 1);
	else
		call_result = delta1(readerChannel, writerChannel);

	if (call_result != CSP_CALL_POISON)
		printf("**** ERROR: poison is broken :(\n");

	CSP_SAFE_CALL("poison start channel", dsmcbe_csp_channel_poison(readerChannel));

	return CSP_CALL_SUCCESS;
}

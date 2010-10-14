//Interface definition for the CSP channel implementation

#include "dsmcbe.h"
#include <stdio.h>

#ifndef DSMCBE_CSP_H_
#define DSMCBE_CSP_H_

//The call succeeded
#define CSP_CALL_SUCCESS (0)
//The call failed
#define CSP_CALL_ERROR (-1)
//The channel was poisoned
#define CSP_CALL_POISON (-2)

//Defines that there can be only one reader and one writer
#define CSP_CHANNEL_TYPE_ONE2ONE (1)
//Defines that there can only be one writer, but multiple readers
#define CSP_CHANNEL_TYPE_ONE2ANY (2)
//Defines that there can be multiple writers, but only one reader
#define CSP_CHANNEL_TYPE_ANY2ONE (3)
//Defines that there can be multiple readers and writers
#define CSP_CHANNEL_TYPE_ANY2ANY (4)
//Defines that there can be only one reader and one writer, and the channel is not used for ALT request
#define CSP_CHANNEL_TYPE_ONE2ONE_SIMPLE (5)

//Defines fair scheduling in external choice
#define CSP_ALT_MODE_FAIR (0x0)
//Defines priority scheduling in external choice
#define CSP_ALT_MODE_PRIORITY (0x1)
//Defines all avalible csp scheduling modes
#define CSP_ALT_MODES { CSP_ALT_MODE_FAIR, CSP_ALT_MODE_PRIORITY, -1 }

//Defines the internal callback code used to temporarily stop the SPU when waiting for data
#define CSP_STOP_FUNCTION_CODE (0x0f)

//The skip guard can only be used with alt's, and returns immediately if the call would otherwise block
#define CSP_SKIP_GUARD (0x0)
//The timeout guard can only be used with alt's, and after the specified time if the call would otherwise block
#define CSP_TIMEOUT_GUARD (0x1)

/*TODO: This macro should free the item if the call is a write call and the result is poison*/
//A convenience macro for ensuring the result of a csp function call
#define CSP_SAFE_CALL_INNER(name, func_call) \
	{ \
		int __csp_func_call_tmp = func_call; \
		if (__csp_func_call_tmp == CSP_CALL_POISON) \
		{ \
			fprintf(stderr, "[file %s, line %d]: %s was poisoned\n", __FILE__,__LINE__, name); \
			return __csp_func_call_tmp; \
		} \
		else if (__csp_func_call_tmp == CSP_CALL_ERROR) \
		{ \
			fprintf(stderr, "[file %s, line %d]: %s failed\n", __FILE__,__LINE__, name); \
			exit(__csp_func_call_tmp); \
		} \
	}

#ifdef DEBUG

//A convinience macro for ensuring the result of a csp function call, will print access information in debug mode
#define CSP_SAFE_CALL(name, func_call) \
	{ \
	printf("Performing %s\n", name); \
	CSP_SAFE_CALL_INNER(name, func_call) \
	printf("Completed %s with succes\n", name); \
	}
#else

//A convinience macro for ensuring the result of a csp function call
#define CSP_SAFE_CALL CSP_SAFE_CALL_INNER

#endif


//Generates a new correctly aligned memory region of the required size
int dsmcbe_csp_item_create(void** data, size_t size);

//Releases all resources allocated by the item
int dsmcbe_csp_item_free(void* data);

//Writes the object to the given channel, using the channel ID, return value should be zero if no errors
int dsmcbe_csp_channel_write(GUID channelid, void* data);

//Writes the object to the given channel, using the channel ID, return value should be zero if no errors
int dsmcbe_csp_channel_read(GUID channelid, size_t* size, void** data);

//Reads a value from any of the channels presented, selection is based on the mode argument.
//If the call succeeds, the channelid value indicates the channel index that was read
int dsmcbe_csp_channel_read_alt(unsigned int mode, GUID* channels, size_t channelcount, GUID* channelid, size_t* size, void** data);

//Reads a value from any of the channels presented, selection is based on the mode argument.
//If the call succeeds, the channelid value indicates the channel index that was written
int dsmcbe_csp_channel_write_alt(unsigned int mode, GUID* channels, size_t channelcount, void* data, GUID* channelid);

//Inserts poison into a channel, effectively destroys the channel
int dsmcbe_csp_channel_poison(GUID channel);

//Creates a new CSP channel of the given type
int dsmcbe_csp_channel_create(GUID channelid, unsigned int buffersize, unsigned int type);

#endif /* DSMCBE_CSP_H_ */

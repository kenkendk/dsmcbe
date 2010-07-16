#ifndef SPUEVENTHANDLER_EXTRAPACKAGES_H_
#define SPUEVENTHANDLER_EXTRAPACKAGES_H_

#define PACKAGE_SPU_MEMORY_SETUP 300
#define PACKAGE_SPU_MEMORY_MALLOC_REQUEST 301
#define PACKAGE_SPU_MEMORY_MALLOC_RESPONSE 302
#define PACKAGE_SPU_MEMORY_FREE 303

#define PACKAGE_SPU_CSP_ITEM_CREATE_REQUEST 304
#define PACKAGE_SPU_CSP_ITEM_CREATE_RESPONSE 305

#define PACKAGE_SPU_CSP_ITEM_FREE_REQUEST 306
#define PACKAGE_SPU_CSP_ITEM_FREE_RESPONSE 307

#define PACKAGE_SPU_CSP_CHANNEL_READ_ALT_REQUEST 308
#define PACKAGE_SPU_CSP_CHANNEL_READ_ALT_RESPONSE 309

#define PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_REQUEST 310
#define PACKAGE_SPU_CSP_CHANNEL_WRITE_ALT_RESPONSE 311

//To make the SPU stop each time it awaits external data, activate this flag
//#define SPU_STOP_AND_WAIT

#endif /* SPUEVENTHANDLER_EXTRAPACKAGES_H_ */

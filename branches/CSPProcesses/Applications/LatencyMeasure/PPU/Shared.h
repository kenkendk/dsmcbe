#ifndef SHARED_H_
#define SHARED_H_

//Keep exactly one mode active
//#define MBOX_MODE
//#define DMA_MODE
//#define NET_MODE
#define DSM_MODE
//#define DSM_MODE_SINGLE

//The size of the data object
#define DATA_SIZE (10 * 1024)
//The number of itterations
#define PROBLEM_SIZE (10000000)

//The DSM data object
#define OBJ_1 1
//The DSM barrier
#define OBJ_BARRIER 8

#endif /*SHARED_H_*/

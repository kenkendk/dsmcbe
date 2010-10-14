#ifndef COMMSTIME_GUIDS_H_
#define COMMSTIME_GUIDS_H_

#define SETUP_CHANNEL 100
#define DELTA_CHANNEL 101

#define RING_CHANNEL_BASE 1000

//The number of rounds performed on the SPEs
#define SPE_REPETITIONS 100

//The number of delta ticks to gather before terminating
#define ROUND_COUNT 10

#define DATA_BLOCK_SIZE (sizeof(unsigned int))

#define PPE_HARDWARE_THREADS 1

//The list of ids that produce packages
//#define SINGLE_PACKAGE {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, UINT_MAX}

#define SMART_ID_ASSIGNMENT

#endif /* COMMSTIME_GUIDS_H_ */

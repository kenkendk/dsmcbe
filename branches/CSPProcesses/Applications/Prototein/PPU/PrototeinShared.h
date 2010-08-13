#ifndef COORDINATEMACROS_H_
#define COORDINATEMACROS_H_

#include <stdio.h>
#include <stdlib.h>

//This is the initial Prototein info
#define PROTOTEIN 2 
//This is the master lock
#define MASTER_COMPLETION_LOCK 4

//If this is set, the communication is more channel-like
#define USE_CHANNEL_COMMUNICATION

#ifdef USE_CHANNEL_COMMUNICATION
	//The work channel ID
	#define WORKITEM_CHANNEL 1000

	//The winner channel ID
	#define WINNER_CHANNEL 500
#else
	//This is the actual workblock ID
	#define WORKITEM_OFFSET 1000

	//This is the SYNC lock
	#define PACKAGE_ITEM 1

	//This is the buffer where each SPU will write data into
	#define WINNER_OFFSET 500
#endif

#define BUFFER_SIZE (8 * 1024)

struct coordinate
{
	int x;
	int y;
};

struct workblock
{
	unsigned int worksize;
	unsigned int worksize_delta;
	unsigned int item_length;
	int winner_score;
};


char get_map_char_raw(char* prototein, int x, int y, struct coordinate* place, int places_length);
	
void printmap(char* prototein, unsigned int prototein_length, struct coordinate* place, unsigned int places_length);

#endif /*COORDINATEMACROS_H_*/

#ifndef COORDINATEMACROS_H_
#define COORDINATEMACROS_H_

#include <stdio.h>

/* These 4 defines are the constants used for data communication */ 
//This is the SYNC lock
#define PACKAGE_ITEM 1
//This is the initial Prototein info
#define PROTOTEIN 2 
//This is the master lock
#define MASTER_COMPLETION_LOCK 4

//This is the buffer where each SPU will write data into
#define WINNER_OFFSET 500

//This is the actual workblock ID
#define WORKITEM_OFFSET 1000

//The number of lightweight threads on the SPU
#define SPU_FIBERS 0

//remove annoying warnings
void exit(int);

#define BUFFER_SIZE (16 * 1024) 

char* prototein;
unsigned int prototein_length;


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


inline char get_map_char_raw(int x, int y, struct coordinate* place, int places_length)
{
	int i;
	for (i = 0; i < places_length; i++)
		if (place[i].x == x && place[i].y == y) 
			return prototein[i];
	return ' ';
}
	
	
void printmap(struct coordinate* place, unsigned int places_length)
{
	size_t i, j;
	
	printf("Map (%i): \n", places_length);
	for(i = 0; i<prototein_length*2; i++)
	{
		printf("'");
		for(j = 0; j<prototein_length*2;j++)
			printf("%c", get_map_char_raw(i,j,place, places_length));
		printf("'\n");
	}
	printf("\n\n");
		
}	


#endif /*COORDINATEMACROS_H_*/

#ifndef COORDINATEMACROS_H_
#define COORDINATEMACROS_H_

#include <stdio.h>

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
	
	
void printmap(struct coordinate* place, int places_length, int org)
{
	int i, j;
	char buf[512];
	memset(buf, 0, 512);
	memcpy(buf, prototein, prototein_length);
	
	/*for(i = 0; i < places_length; i++)
		printf("%d %d, ", place[i].x, place[i].y);
	printf("\n");*/
	
	printf("Map for %s  (%i): \n", buf, places_length);
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

#include "PrototeinShared.h"

inline char get_map_char_raw(char* prototein, int x, int y, struct coordinate* place, int places_length)
{
	int i;
	for (i = 0; i < places_length; i++)
		if (place[i].x == x && place[i].y == y)
			return prototein[i];
	return ' ';
}


void printmap(char* prototein, unsigned int prototein_length, struct coordinate* place, unsigned int places_length)
{
	size_t i, j;

	printf("Map (%i): \n", places_length);
	for(i = 0; i<prototein_length*2; i++)
	{
		printf("'");
		for(j = 0; j<prototein_length*2;j++)
			printf("%c", get_map_char_raw(prototein, i,j,place, places_length));
		printf("'\n");
	}
	printf("\n\n");

}

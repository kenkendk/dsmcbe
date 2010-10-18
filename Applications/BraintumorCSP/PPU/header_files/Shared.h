#define RANDOM(max) ((((float)rand() / (float)RAND_MAX) * (float)(max)))
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

struct ENERGY_POINT
{
	//The X value of the point, UINT_MAX means dead
	unsigned int x;
	//The Y value of the point, UINT_MAX means dead
	unsigned int y;
};

struct MACHINE_SETUP
{
	//The number of shots to simulate for each canon
	unsigned int shotsPrCanon;
	//The number of SPEs
	unsigned int workers;
	//The height of the map in pixels
	unsigned int map_heigth;
	//The width of the map in pixels
	unsigned int map_width;
	//The height of a single map fragment
	unsigned int fragment_heigth;
	//The width of a single map fragment
	unsigned int fragment_width;
	//The max size of each result set
	unsigned int result_set_size;
};

struct WORK_ORDER
{	
	//The package id
	unsigned int id;
	//The height of the map in pixels
	unsigned int map_heigth;
	//The width of the map in pixels
	unsigned int map_width;
	//The height of a single map fragment
	unsigned int fragment_height;
	//The width of a single map fragment
	unsigned int fragment_width;
	//The number of fragments in the X direction
	unsigned int fragments_x;
	//The number of fragments in the Y direction
	unsigned int fragments_y;
	//The number of shots to perform
	unsigned int shots;
	//The cannon X position
	int canonX; 
	//The cannon Y position
	int canonY; 
	//The cannon particle initial acceleration on the X axis
	float canonAX; 
	//The cannon particle initial acceleration on the Y axis
	float canonAY;
};

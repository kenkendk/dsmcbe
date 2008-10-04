#define GRIDWIDTH 256 // Number of pixels
#define GRIDHEIGTH 256 // Number of pixels

struct POINTS
{
	unsigned int x;
	unsigned int y;
	unsigned char alive;
};

struct PACKAGE 
{	
	unsigned int id;
	unsigned int maxid;
	unsigned int heigth; 
	unsigned int width; 
	unsigned int shots_spu; 
	int canonX; 
	int canonY; 
	float canonAX; 
	float canonAY;
	unsigned int dummy1; 
	unsigned int dummy2; 
	unsigned int dummy3; 
};

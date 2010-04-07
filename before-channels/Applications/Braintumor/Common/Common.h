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
	unsigned int tot_shots_spu;	 
	int canonX; 
	int canonY; 
	float canonAX; 
	float canonAY;
};

#ifndef SHARED_H_
#define SHARED_H_


//Toggle graphic display on or off
//#define GRAPHICS

//Turning graphics on will result in large memory consumption on machine 0
//The size of the memory grows linearly with the number of machines

//Set the map width
#define FIXED_MAP_WIDTH 128

//The total size of memory to use on each machine
//Unfortunately PS3's do not have a lot of memory...
#define MEMORY_SIZE (1024 * 1024 * 150)

//#define SPU_COUNT 16
//#define MEMORY_SIZE ((((1024 * 100) + (sizeof(PROBLEM_DATA_TYPE) * (FIXED_MAP_WIDTH * 2))) * (SPU_COUNT * 100)) - (sizeof(PROBLEM_DATA_TYPE) * FIXED_MAP_WIDTH))

//The number of itterations to perform
#define ITTERATIONS 1000

//Defines the size of each work block (approximate value)
#define BLOCK_SIZE (100 * 1024)

//How often the graphics should update
#define UPDATE_FREQ 10

//If the blocks should be created on the SPU using them
#define CREATE_LOCALLY

//Offset calculation
#define MAPOFFSET(x,y) (((y) * (map_width)) + (x))
#define MAPVALUE(x,y) (data[MAPOFFSET((x),(y))])

//Simple macros
#ifndef MIN
#define MIN(x,y) ((x) <= (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

#ifndef ABS
//Should use built in sign operator
#define ABS(x) ((x) > 0 ? (x) : ((x) * -1))
#endif 

//The SPU only handles float in hardware, so double has to be simulated
#define PROBLEM_DATA_TYPE float

//Structure for sending data between two units
struct Work_Unit
{
	unsigned int block_no;
    unsigned int line_start;
    unsigned int heigth;
    unsigned int buffer_size;
    //Below here is a chunk of data, size = buffer_size * sizeof(double)
    PROBLEM_DATA_TYPE problem;
};

struct Assignment_Unit
{
	unsigned int map_width;
	unsigned int map_height;
	unsigned int spu_no;
	unsigned int spu_count;
	unsigned int sharedCount;
	unsigned int next_job_no;
	unsigned int maxjobs;
	unsigned int job_count;
	float epsilon;
};

struct Barrier_Unit
{	
	unsigned int lock_count;
	double delta;
	unsigned int print_count;
};

struct Results
{
	double deltaSum;
	unsigned int rc;
};


#define JOB_LOCK 10
#define ASSIGNMENT_LOCK 1
#define BARRIER_LOCK 2
#define BARRIER_LOCK_ALT 3

#define EX_BARRIER_1 4 
#define EX_BARRIER_2 5 
#define BARRIER_LOCK_EXTRA 7
#define EX_BARRIER_X 8 

#define MASTER_START_LOCK 14
#define MASTER_COMPLETION_LOCK 15

#define BARRIER_LOCK_OFFSET 16

#define WORK_OFFSET 100
#define SHARED_ROW_OFFSET 25000
#define RESULT_OFFSET 49900

#endif /*SHARED_H_*/

#ifndef SHARED_H_
#define SHARED_H_

#define GRAPHICS

#define USE_BARRIER

#define SLICE_HEIGHT 128

#define UPDATE_FREQ 10

#define MAPOFFSET(x,y) (((y) * (map_width)) + (x))
#define MAPVALUE(x,y) (data[MAPOFFSET((x),(y))])

#ifndef MIN
#define MIN(x,y) ((x) <= (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

#ifndef ABS
//Should use built in sign operator
#define ABS(x) ((x) > 0 ? (x) : ((x) * -1))
#endif 

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

struct Job_Control
{
	unsigned int count;
	unsigned int nextjob;
	unsigned int red_round;
};

struct Assignment_Unit
{
	unsigned int map_width;
	unsigned int map_height;
	unsigned int spu_no;
	unsigned int spu_count;
	double epsilon;
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

#define EX_BARRIER_1 4 
#define EX_BARRIER_2 5 
#define EX_BARRIER_3 6 
#define BARRIER_LOCK_EXTRA 7
#define EX_BARRIER_X 8 

#define WORK_OFFSET 100
#define FIRST_ROW_OFFSET 500
#define LAST_ROW_OFFSET 1000
#define SHARED_ROW_OFFSET 1500
#define RESULT_OFFSET 2000

#endif /*SHARED_H_*/

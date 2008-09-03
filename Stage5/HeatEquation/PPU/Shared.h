#ifndef SHARED_H_
#define SHARED_H_

//#define GRAPHICS

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

#define PROBLEM_DATA_TYPE double

//Structure for sending data between two units
struct Work_Unit
{
    unsigned int line_start;
    unsigned int width;
    unsigned int heigth;
    double epsilon;
    unsigned int buffer_size;
    //Below here is a chunk of data, size = buffer_size * sizeof(double)
    PROBLEM_DATA_TYPE problem;
};

struct Assignment_Unit
{
	unsigned int spu_no;
	unsigned int spu_count;
};

struct Barrier_Unit
{
	unsigned int lock_count;
	double delta;
	unsigned int print_count;
};

#define ASSIGNMENT_LOCK 1
#define BARRIER_LOCK 2

#define EX_BARRIER_1 4 
#define EX_BARRIER_2 5 
#define EX_BARRIER_3 6 

#define WORK_OFFSET 100
#define FIRST_ROW_OFFSET 500
#define LAST_ROW_OFFSET 1000

#endif /*SHARED_H_*/

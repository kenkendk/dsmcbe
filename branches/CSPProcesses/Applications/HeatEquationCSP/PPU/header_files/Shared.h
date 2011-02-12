#ifndef __SHARED_H__
#define __SHARED_H__

#define DATATYPE float

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(!FALSE)
#endif

#ifndef ABS
//Should use built in sign operator
#define ABS(x) ((x) > 0 ? (x) : ((x) * -1))
#endif

struct MACHINE_SETUP
{
	//The total number of rows to process
	unsigned int numberOfRows;
	//The number of virtual rows
	unsigned int numberOfVRows;
	//The number of rows in a virtual row
	unsigned int numberOfRowsInVRow;
	//The width of a single row
	unsigned int rowWidth;

	//The first row to process
	unsigned int minRow;
	//The last row to process
	unsigned int maxRow;

	//A flag indicating if the worker is the first worker
	unsigned int isLastWorker;
	//A flag indicating if the worker is the last worker
	unsigned int isFirstWorker;
	//The worker number, used to debug
	unsigned int workerNumber;


};

struct VROW
{
	//The VRow number
	unsigned int rowNo;
	//The width of the row
	unsigned int rowWidth;
	//The number of rows in this VRow
	unsigned int rowCount;
	//The number of half iterations performed on the row
	unsigned int halfIterationCount;

	//After this is a block of:
	//DATATYPE[rowWidth * rowCount] rowData;
};

#endif

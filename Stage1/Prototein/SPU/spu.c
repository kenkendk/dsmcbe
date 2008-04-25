#include<stdio.h>
#include<profile.h>
#include<malloc.h>
#include "DMATransfer.h"
#include "SPUThreads.h"

int DMATest(unsigned long long id);
int FoldPrototein(unsigned long long id);

int main(unsigned long long id) {
	
	int threadNo;

	//prof_clear();
	//prof_start();
	
	
	threadNo = CreateThreads(2);

	int res = FoldPrototein(id);
	
	//prof_stop();
	return res;

	
}


#include<stdio.h>
#include<profile.h>
#include<malloc.h>
#include "DMATransfer.h"

int FoldPrototein(unsigned long long id);

int main(unsigned long long id) {
	
	//prof_clear();
	//prof_start();
	
	int res = FoldPrototein(id);
	
	//prof_stop();
	return res;

	
}


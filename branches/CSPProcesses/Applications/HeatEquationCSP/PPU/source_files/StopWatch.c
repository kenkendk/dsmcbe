#include <string.h>
#include <stdio.h>
#include "../header_files/StopWatch.h"

double __sw_start, __sw_stop;


struct timeval __sw_tvstruct;

void sw_init(){
  __sw_start=__sw_stop=0.0;
}

void sw_start(){
  gettimeofday(&__sw_tvstruct,(void *)0);
  __sw_start=1.0*__sw_tvstruct.tv_sec+__sw_tvstruct.tv_usec/1000000.0;
}

void sw_stop(){
  gettimeofday(&__sw_tvstruct,(void *)0);
  __sw_stop=1.0*__sw_tvstruct.tv_sec+__sw_tvstruct.tv_usec/1000000.0;
}

int readDays(){
  return (int)((__sw_stop-__sw_start)/(24*60*60));
}

int readHours(){
  return (int)((__sw_stop-__sw_start)/(60*60))%24;
}

int readMinutes(){
  return (int)((__sw_stop-__sw_start)/(60))%60;
}

int readSeconds(){
  return (int)((__sw_stop-__sw_start))%60;
}

int readmSeconds(){
  return ((int)((__sw_stop-__sw_start)*1000))%1000;
}

double readTotalSeconds() { return (__sw_stop-__sw_start); }

void sw_timeString(char *buf) {
  buf[0]=0;
  
  if(readDays()>0)sprintf(buf,"%d days ",readDays()); 
  buf=strchr(buf,0);
  if(readHours()>0)sprintf(buf,"%d hours ",readHours()); 
  buf=strchr(buf,0);
  if(readMinutes()>0)sprintf(buf,"%d min. ",readMinutes()); 
  buf=strchr(buf,0);
  if(readSeconds()>0)sprintf(buf,"%d seconds ",readSeconds()); 
  buf=strchr(buf,0);
  sprintf(buf,"%d ms ",readmSeconds()); 
}

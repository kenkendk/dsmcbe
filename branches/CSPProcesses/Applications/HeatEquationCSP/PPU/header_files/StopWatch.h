#include <string.h>
#include <stdio.h>
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <time.h>
#else
#include <sys/time.h>
#endif

void sw_init();

void sw_start();

void sw_stop();

int readDays();

int readHours();

int readMinutes();

int readSeconds();

int readmSeconds();

double readTotalSeconds();

void sw_timeString(char *buf);
#if defined(_MSC_VER)
#ifndef __GNUC__
#define EPOCHFILETIME (116444736000000000i64) 
#else 
#define EPOCHFILETIME (116444736000000000LL) 
#endif
  
#if !defined(_WINSOCK2API_) && !defined(_WINSOCKAPI_)
struct timeval {
    long tv_sec; /* seconds */    
    long tv_usec; /* microseconds */    
}; 
#endif
  
struct timezone { 
    int tz_minuteswest; /* minutes W of Greenwich */ 
    int tz_dsttime; /* type of dst correction */   
};
  
__inline int gettimeofday(struct timeval *tv, struct timezone *tz) 
{ { 
    FILETIME ft;  
    LARGE_INTEGER li;     
    __int64 t;    
    static int tzflag;     
  
    if (tv)  
    {    
        GetSystemTimeAsFileTime(&ft);      
        li.LowPart = ft.dwLowDateTime;         
        li.HighPart = ft.dwHighDateTime;        
        t = li.QuadPart; /* In 100-nanosecond intervals */  
        t -= EPOCHFILETIME; /* Offset to the Epoch time */     
        t /= 10; /* In microseconds */      
        tv->tv_sec = (long)(t / 1000000);  
        tv->tv_usec = (long)(t % 1000000);       
    }     
  
    if (tz)     
    {   
        if (!tzflag)       
        {         
            _tzset();          
            tzflag++;
        }       
        tz->tz_minuteswest = _timezone / 60;      
        tz->tz_dsttime = _daylight;       
    }     } 
  
    return 0;    
} 
  
  
  
#else /* _MSC_VER */ 
  
/* unsigned int, 64 bits: u64_t */
#define u64_t u_int64_t
#include <sys/time.h> /* struct timeval */
  
#define DIR_CONTINUE continue;
  
#define DIRCMP strcmp 
#define DIRNCMP strncmp
  
#define DIRNORM(x,l,low) 
  
/** remove trailing / */ 
#define REMOVE_TRAILING_SLASH(str) \
  do { \
    size_t _length = strlen(str); \
    if (_length>1 && (str)[_length-1]=='/') \
      (str)[_length-1] = '\0'; \
  } while(0)

#endif /* _MSC_VER */




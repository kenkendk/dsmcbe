#include <math.h>
#include <memory.h>
#include <malloc.h>
#include <tinyptc.h>

int *__gs_buffer;
int __gs_xsize, __gs_ysize;

void
ptc_cleanup_callback (void)
{
  //fprintf (stderr, "Callback!\n");
}

void gs_update(){
  ptc_update(__gs_buffer);
}

void gs_plot(int x, int y, int color){
  __gs_buffer[y*__gs_xsize+x]=color;
}

void gs_init(int xsize, int ysize) {
  __gs_xsize=xsize;
  __gs_ysize=ysize;
  __gs_buffer = (int *)malloc(sizeof(int)*xsize*ysize);
  
  memset(__gs_buffer, sizeof(int)*xsize*ysize, 0);
  
  if (!ptc_open("GraphicsScreen", xsize, ysize))
  {
	  fprintf(stderr, "Failed to create a graphics screen, exiting\n");
	  exit(-1);
  }
  gs_update();
}


void gs_exit(){
  ptc_close ();
}

void gs_clear(int color){
  memset(__gs_buffer, color, sizeof(int)*__gs_xsize*__gs_xsize);
}

void gs_dot(int x, int y, int z, int color){
  int i,j;

  for(j=y-z/2; j<y+z/2; j++)
    for(i=x-z/2; i<x+z/2; i++)      
      if(pow(((double)x-i)/((double)z/2.0),2.0)+pow(((double)y-j)/((double)z/2.0),2.0)<1)
	gs_plot(i,j, color);
}

void gs_line(int x0, int y0, int x1, int y1, int color) {
  int dy = y1 - y0;
  int dx = x1 - x0;
  float t = (float) 0.5;                      // offset for rounding
  float m;

  gs_plot(x0, y0, color);
  if (fabs(dx) > fabs(dy)) {          // slope < 1
    m = (float) dy / (float) dx;      // compute slope
    t += y0;
    dx = (dx < 0) ? -1 : 1;
    m *= dx;
    while (x0 != x1) {
      x0 += dx;                           // step to next x value
      t += m;                             // add slope to y value
      gs_plot(x0, (int) t, color);
    }
  } else {                                    // slope >= 1
    m = (float) dx / (float) dy;      // compute slope
    t += x0;
    dy = (dy < 0) ? -1 : 1;
    m *= dy;
    while (y0 != y1) {
      y0 += dy;                           // step to next y value
      t += m;                             // add slope to x value
      gs_plot((int) t, y0, color);
    }
  }
}

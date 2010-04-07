#define WHITE (0xffffff)
#define BLUE  (0x0000ff)
#define CYAN  (0x7c8dff)
#define BLACK (0x000000)
#define GREEN (0x18ff08)
#define MAGENTA (0xff00ff)
#define ORANGE (0xff8706)
#define PINK (0xff9a96)
#define YELLOW (0xffff00)
#define RED  (0xff0000)


void gs_update();

void gs_plot(int x, int y, int color);

void gs_init(int xsize, int ysize);

void gs_exit();

void gs_clear(int color);

void gs_dot(int x, int y, int z, int color);

void gs_line(int x0, int y0, int x1, int y1, int color);

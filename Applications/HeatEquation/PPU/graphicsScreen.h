#define WHITE (0x00ffffff)
#define BLUE  (0x000000ff)
#define CYAN  (0x007c8dff)
#define BLACK (0x00000000)
#define GREEN (0x0018ff08)
#define MAGENTA (0x00ff00ff)
#define ORANGE (0x00ff8706)
#define PINK (0x00ff9a96)
#define YELLOW (0x00ffff00)
#define RED  (0x00ff0000)


void gs_update();

void gs_plot(int x, int y, int color);

void gs_init(int xsize, int ysize);

void gs_exit();

void gs_clear(int color);

void gs_dot(int x, int y, int z, int color);

void gs_line(int x0, int y0, int x1, int y1, int color);

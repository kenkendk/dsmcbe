struct RGB
{
		unsigned char r;
		unsigned char g;
		unsigned char b;
};

struct IMAGE_FORMAT
{
	unsigned int width;
	unsigned int height;
	struct RGB* image;
};

struct IMAGE_FORMAT_GREY
{
	unsigned int width;
	unsigned int height;
	unsigned char* image;
};

void readimage_grey_DSMCBE(char* filename, struct IMAGE_FORMAT_GREY* imageinfo, int imageID);
void readimage_grey(char* filename, void* allocator(size_t), struct IMAGE_FORMAT_GREY* imageinfo);
void readimage_rgb(char* filename, void* allocator(size_t), struct IMAGE_FORMAT* imageinfo);
void writeimage_grey(char* filename, struct IMAGE_FORMAT_GREY* imageinfo);
void writeimage_rgb(char* filename, struct IMAGE_FORMAT* imageinfo);

#include "../Common/PPMReaderWriter.h"

#include <stdio.h>
#include <errno.h>
#include <math.h>

#ifdef WIN32
#include <windows.h>
#endif

void readimage_grey(char* filename, void* allocator(size_t), struct IMAGE_FORMAT_GREY* imageinfo)
{
	unsigned char buf[1024];
	FILE* fh;
	size_t r;
	unsigned int x,y;
	unsigned int image_size;
	unsigned int maxval;
	unsigned int memory_width;

    fh = fopen(filename, "rb");
	if (!fh)
	{
		printf("Failed to open file %s\n", filename);
		exit(-1);
	}

	r = fread(buf, 1, 2, fh);
	if (r != 2)
	{
		fclose(fh);
		printf("Failed to read data from file\n");
		exit(-1);
	}
	if (buf[0] != 'P' || buf[1] != '6')
	{
		fclose(fh);
		printf("File format is not P6, which is the only supported format\n");
		exit(-1);
	}

	do
	{
		if (fscanf(fh, "%s", buf) == 0)
		{
			fclose(fh);
			printf("Failed to detect image size\n");
			exit(-1);
		}
	} while(fscanf(fh, "%d %d\n%d", &imageinfo->width, &imageinfo->height, &maxval) != 3);

	//Read whitespace
	fscanf(fh, " ");
	image_size = imageinfo->width * imageinfo->height;
	memory_width = imageinfo->width + (128 - imageinfo->width % 128);

	imageinfo->image = (unsigned char*)allocator(memory_width * imageinfo->height * sizeof(unsigned char));
	
	for(y = 0; y < imageinfo->height; y++)
	{
		for(x = 0; x < imageinfo->width; x++)
		{
			r = fread(buf, 1, 3, fh);
			if (r != 3)
			{
				fclose(fh);
				printf("Failed to read image data at (x,y): (%d,%d)\n", x, y);
				exit(-1);
			}
	
			//Brightness from RGB
			//Perhaps use: (0.299 * buf[0]) + (0.587 * buf[1]) + (0.114 * buf[2])
			//or (buf[0] + buf[1] + buf[2]) / 3;
	
			imageinfo->image[(y*memory_width)+x] = (buf[0] + buf[1] + buf[2]) / 3;
		}
		
	}
	fclose(fh);
}

void readimage_rgb(char* filename, void* allocator(size_t), struct IMAGE_FORMAT* imageinfo)
{
	unsigned char buf[1024];
	FILE* fh;
	size_t r;
	unsigned int x,y;
	unsigned int image_size;
	unsigned int maxval;

    fh = fopen(filename, "rb");
	if (!fh)
	{
		printf("Failed to open file %s\n", filename);
		exit(-1);
	}

	r = fread(buf, 1, 2, fh);
	if (r != 2)
	{
		fclose(fh);
		printf("Failed to read data from file\n");
		exit(-1);
	}
	if (buf[0] != 'P' || buf[1] != '6')
	{
		fclose(fh);
		printf("File format is not P6, which is the only supported format\n");
		exit(-1);
	}

	do
	{
		if (fscanf(fh, "%s", buf) == 0)
		{
			fclose(fh);
			printf("Failed to detect image size\n");
			exit(-1);
		}
	} while(fscanf(fh, "%d %d\n%d", &imageinfo->width, &imageinfo->height, &maxval) != 3);

	//Read whitespace
	fscanf(fh, " ");
	image_size = imageinfo->width * imageinfo->height;

	imageinfo->image = (struct RGB*)allocator(imageinfo->width * imageinfo->height * sizeof(struct RGB));
	
	for(y = 0; y < imageinfo->height; y++)
	{
		for(x = 0; x < imageinfo->width; x++)
		{
			r = fread(buf, 1, 3, fh);
			if (r != 3)
			{
				fclose(fh);
				printf("Failed to read image data at (x,y): (%d,%d)\n", x, y);
				exit(-1);
			}
	
			//Brightness from RGB
			//Perhaps use: (0.299 * buf[0]) + (0.587 * buf[1]) + (0.114 * buf[2])
			//or (buf[0] + buf[1] + buf[2]) / 3;
	
			imageinfo->image[(y*imageinfo->width)+x].r = (buf[0] + buf[1] + buf[2]) / 3;
			imageinfo->image[(y*imageinfo->width)+x].g = (buf[0] + buf[1] + buf[2]) / 3;
			imageinfo->image[(y*imageinfo->width)+x].b = (buf[0] + buf[1] + buf[2]) / 3;
		}
		
	}
	fclose(fh);
}

void writeimage_rgb(char* filename, struct IMAGE_FORMAT* imageinfo)
{
	FILE* fh;
	unsigned int x;
	unsigned int y;
	unsigned char buf[3];

    fh = fopen(filename, "wb");
	if (!fh)
	{
		printf("Failed to open file %s (%d)\n", filename, errno);
		exit(-1);
	}

	fprintf(fh, "P6\n# CREATOR: The GIMP's PNM Filter Version 1.0\n%d %d\n%d", imageinfo->width, imageinfo->height, 255);
	
	//Windoze writes 0x0a 0x0d for newlines...
	buf[0] = 0x0a;
	fwrite(buf, 1, 1, fh);
	for(y = 0; y < imageinfo->height; y++)
		for(x = 0; x < imageinfo->width; x++)
		{
			buf[0] = imageinfo->image[x+(y*imageinfo->width)].r;
			buf[1] = imageinfo->image[x+(y*imageinfo->width)].g;
			buf[2] = imageinfo->image[x+(y*imageinfo->width)].b;

			fwrite(buf, 1, 3, fh);
		}

	fclose(fh);

	printf("Save image done\n");
}

void writeimage_grey(char* filename, struct IMAGE_FORMAT_GREY* imageinfo)
{
	FILE* fh;
	unsigned int x;
	unsigned int y;
	unsigned char buf[3];
	unsigned int memory_width;

    fh = fopen(filename, "wb");
	if (!fh)
	{
		printf("Failed to open file %s (%d)\n", filename, errno);
		exit(-1);
	}

	fprintf(fh, "P6\n# CREATOR: The GIMP's PNM Filter Version 1.0\n%d %d\n%d", imageinfo->width, imageinfo->height, 255);
	
	//Windoze writes 0x0a 0x0d for newlines...
	buf[0] = 0x0a;
	fwrite(buf, 1, 1, fh);
	
	memory_width = imageinfo->width + (128 - imageinfo->width % 128);
	
	for(y = 0; y < imageinfo->height; y++)
		for(x = 0; x < imageinfo->width; x++)
		{
			buf[2] = buf[1] = buf[0] = imageinfo->image[x+(y*memory_width)];
			fwrite(buf, 1, 3, fh);
		}

	fclose(fh);

	printf("Save image done\n");
}


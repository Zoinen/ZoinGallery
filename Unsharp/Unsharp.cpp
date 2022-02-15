/*
* Copyright (C) 1999 Winston Chang
*                    <winstonc@cs.wisc.edu>
*                    <winston@stdout.org>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

// This algorithm was taken from the GIMP v2.4.5 sourcecodes
// and adopted for the FreeImage library
//
//	Copyright (C) 2007-2008:
//	monday2000	monday2000@yandex.ru

//#include "FreeImage.h"
#include "Utilities.h"

#define PAD(width)  (((width) + 3) & (~3))

struct FIBITMAP {
    unsigned width;
    unsigned height;
    unsigned pitch;
    unsigned bpp;
    BYTE *bits;
};

unsigned FreeImage_GetWidth(FIBITMAP *dib) {
    return dib->width;
}

unsigned FreeImage_GetHeight(FIBITMAP *dib) {
    return dib->height;
}

unsigned FreeImage_GetPitch(FIBITMAP *dib) {
    return dib->pitch;
}

unsigned FreeImage_GetBPP(FIBITMAP *dib) {
    return dib->bpp;
}

BYTE* FreeImage_GetBits(FIBITMAP *dib) {
    return dib->bits;
}

FIBITMAP *FreeImage_Allocate(unsigned width, unsigned height, unsigned bpp) {
    FIBITMAP *out = new FIBITMAP;
    out->width = width;
    out->height = height;
    out->bpp = bpp;
    out->bits = new uint8_t[width * height * bpp];
    out->pitch = PAD(width * bpp / 8);
    return out;
}
/////////////////////////////////////////////////////////////////////////////////////////////

// clamp and place result in destination pixel
inline BYTE FI_Clamp(double value)
{
	return (BYTE)MIN(MAX((int)0, (int)(value + 0.5)), (int)255);
}

/////////////////////////////////////////////////////////////////////////////////////////////

inline double FI_Sqr(double param)
{
	return param * param;
	
}

/////////////////////////////////////////////////////////////////////////////////////////////

inline int FI_Round(double param)
{
	return (int)floor(param);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// This function is written as if it is blurring a column at a time,
// even though it can operate on rows, too.  There is no difference
// in the processing of the lines, at least to the blur_line function.

static void blur_line (const double *ctable,
					   const double *cmatrix,
					   const int     cmatrix_length,
					   const BYTE  *src,
					   BYTE        *dest,
					   const int     len,
					   const int     bytes)
{
	const double *cmatrix_p;
	const double *ctable_p;
	const BYTE  *src_p;
	const BYTE  *src_p1;
	const int     cmatrix_middle = cmatrix_length / 2;
	int           row;
	int           i, j;
	
	// This first block is the same as the optimized version --
	// it is only used for very small pictures, so speed isn't a
	// big concern.
	
	if (cmatrix_length > len)
    {
		for (row = 0; row < len; row++)
        {
			// find the scale factor
			double scale = 0;
			
			for (j = 0; j < len; j++)
            {
				// if the index is in bounds, add it to the scale counter
				if (j + cmatrix_middle - row >= 0 &&
					j + cmatrix_middle - row < cmatrix_length)
					scale += cmatrix[j];
            }
			
			src_p = src;
			
			for (i = 0; i < bytes; i++)
            {
				double sum = 0;
				
				src_p1 = src_p++;
				
				for (j = 0; j < len; j++)
                {
					if (j + cmatrix_middle - row >= 0 &&
						j + cmatrix_middle - row < cmatrix_length)
						sum += *src_p1 * cmatrix[j];
					
					src_p1 += bytes;
                }
				
				*dest++ = (BYTE) FI_Round (sum / scale);
            }
        }
    }
	else
    {
		// for the edge condition, we only use available info and scale to one
		for (row = 0; row < cmatrix_middle; row++)
        {
			// find scale factor
			double scale = 0;
			
			for (j = cmatrix_middle - row; j < cmatrix_length; j++)
				scale += cmatrix[j];
			
			src_p = src;
			
			for (i = 0; i < bytes; i++)
            {
				double sum = 0;
				
				src_p1 = src_p++;
				
				for (j = cmatrix_middle - row; j < cmatrix_length; j++)
                {
					sum += *src_p1 * cmatrix[j];
					src_p1 += bytes;
                }
				
				*dest++ = (BYTE) FI_Round (sum / scale);
            }
        }
		
		// go through each pixel in each col
		for (; row < len - cmatrix_middle; row++)
        {
			src_p = src + (row - cmatrix_middle) * bytes;
			
			for (i = 0; i < bytes; i++)
            {
				double sum = 0;
				
				cmatrix_p = cmatrix;
				src_p1 = src_p;
				ctable_p = ctable;
				
				for (j = 0; j < cmatrix_length; j++)
                {
					sum += cmatrix[j] * *src_p1;
					src_p1 += bytes;
					ctable_p += 256;
                }
				
				src_p++;
				*dest++ = (BYTE) FI_Round (sum);
            }
        }
		
		// for the edge condition, we only use available info and scale to one
		for (; row < len; row++)
        {
			// find scale factor
			double scale = 0;
			
			for (j = 0; j < len - row + cmatrix_middle; j++)
				scale += cmatrix[j];
			
			src_p = src + (row - cmatrix_middle) * bytes;
			
			for (i = 0; i < bytes; i++)
            {
				double sum = 0;
				
				src_p1 = src_p++;
				
				for (j = 0; j < len - row + cmatrix_middle; j++)
                {
					sum += *src_p1 * cmatrix[j];
					src_p1 += bytes;
                }
				
				*dest++ = (BYTE) FI_Round (sum / scale);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////

// generates a 1-D convolution matrix to be used for each pass of
//  a two-pass gaussian blur.  Returns the length of the matrix.
//
static int gen_convolve_matrix (double radius, double **cmatrix_p)
{
	double *cmatrix;
	double  std_dev;
	double  sum;
	int     matrix_length;
	int     i, j;
	
	// we want to generate a matrix that goes out a certain radius
	// from the center, so we have to go out ceil(rad-0.5) pixels,
	// inlcuding the center pixel.  Of course, that's only in one direction,
	// so we have to go the same amount in the other direction, but not count
	// the center pixel again.  So we double the previous result and subtract
	// one.
	// The radius parameter that is passed to this function is used as
	// the standard deviation, and the radius of effect is the
	// standard deviation * 2.  It's a little confusing.
	// 
	radius = fabs (radius) + 1.0;
	
	std_dev = radius;
	radius = std_dev * 2;
	
	// go out 'radius' in each direction
	matrix_length = 2 * ceil (radius - 0.5) + 1;
	if (matrix_length <= 0)
		matrix_length = 1;
	
	*cmatrix_p = (double*) calloc (matrix_length, sizeof(double));
	cmatrix = *cmatrix_p;
	
	//  Now we fill the matrix by doing a numeric integration approximation
	//  from -2*std_dev to 2*std_dev, sampling 50 points per pixel.
	//  We do the bottom half, mirror it to the top half, then compute the
	//  center point.  Otherwise asymmetric quantization errors will occur.
	//  The formula to integrate is e^-(x^2/2s^2).
	// 
	
	// first we do the top (right) half of matrix
	for (i = matrix_length / 2 + 1; i < matrix_length; i++)
    {
		double base_x = i - (matrix_length / 2) - 0.5;
		
		sum = 0;
		for (j = 1; j <= 50; j++)
        {
			if (base_x + 0.02 * j <= radius)
				sum += exp (- FI_Sqr (base_x + 0.02 * j) / (2 * FI_Sqr (std_dev)));
        }
		
		cmatrix[i] = sum / 50;
    }
	
	// mirror the thing to the bottom half
	for (i = 0; i <= matrix_length / 2; i++)
		cmatrix[i] = cmatrix[matrix_length - 1 - i];
	
	// find center val -- calculate an odd number of quanta to make it symmetric,
	// even if the center point is weighted slightly higher than others.
	sum = 0;
	for (j = 0; j <= 50; j++)
		sum += exp (- FI_Sqr (0.5 + 0.02 * j) / (2 * FI_Sqr (std_dev)));
	
	cmatrix[matrix_length / 2] = sum / 51;
	
	// normalize the distribution by scaling the total sum to one
	sum = 0;
	for (i = 0; i < matrix_length; i++)
		sum += cmatrix[i];
	
	for (i = 0; i < matrix_length; i++)
		cmatrix[i] = cmatrix[i] / sum;
	
	return matrix_length;
}

//////////////////////////////////////////////////////////////////////////////////////////////

// ----------------------- gen_lookup_table -----------------------
//  generates a lookup table for every possible product of 0-255 and
//  each value in the convolution matrix.  The returned array is
//  indexed first by matrix position, then by input multiplicand (?)
//  value.
//
static double* gen_lookup_table (const double *cmatrix, int cmatrix_length)
{
	double       *lookup_table   = (double*) calloc (cmatrix_length * 256, sizeof(double));
	double       *lookup_table_p = lookup_table;
	const double *cmatrix_p      = cmatrix;
	int           i, j;
	
	for (i = 0; i < cmatrix_length; i++)
    {
		for (j = 0; j < 256; j++)
			*(lookup_table_p++) = *cmatrix_p * (double) j;
		
		cmatrix_p++;
    }
	
	return lookup_table;
}

//////////////////////////////////////////////////////////////////////////////////////////////

void FI_pixel_rgn_get_row (FIBITMAP* dib, BYTE* buf, unsigned x, unsigned y, unsigned width, int bytes)
{
	// Get several pixels of a region in a row.
	// This function fills the buffer buf with the
	// values of the pixels from (x, y) to (x+width-1, y).
	// buf should be large enough to hold all these values.
	
	//dib :
	// a previously initialized FIBITMAP*.
	//buf :
	// a pointer to an array of BYTE  
	//x :
	// the x coordinate of the first pixel
	//y :
	// the y coordinate of the first pixel
	//width :
	// the number of pixels to get.
	
	unsigned dw  = FreeImage_GetWidth(dib);
	
	unsigned dh = FreeImage_GetHeight(dib);
	
	unsigned pitch  = FreeImage_GetPitch(dib);
	
	unsigned c, i;
	
	BYTE* bits = (BYTE*)FreeImage_GetBits(dib); // The image raster
	
	BYTE* lines = bits + y * pitch;
	
	int index = 0;
	
	for (c = x; c < x+width; c++)

		for (i = 0; i < bytes; i++)
		
		buf[index++] = lines[c * bytes + i];
}

/////////////////////////////////////////////////////////////////////////////////////////////

void FI_pixel_rgn_get_col (FIBITMAP* dib, BYTE* buf, unsigned x, unsigned y, unsigned height, int bytes)
{
	// Get several pixels of a region's column.
	// This function fills the buffer buf with the values
	// of the pixels from (x, y) to (x, y+height-1).
	// buf should be large enough to hold all these values.
	
	//dib :
	// a previously initialized FIBITMAP*.
	//buf :
	// a pointer to an array of BYTE  
	//x :
	// the x coordinate of the first pixel
	//y :
	// the y coordinate of the first pixel
	//height :
	// the number of pixels to get.
	
	unsigned dw  = FreeImage_GetWidth(dib);
	
	unsigned dh = FreeImage_GetHeight(dib);
	
	unsigned pitch  = FreeImage_GetPitch(dib);
	
	unsigned r, i;
	
	BYTE* bits = (BYTE*)FreeImage_GetBits(dib); // The image raster
	
	BYTE* lines;  
	
	int index = 0;
	
	for (r = y; r < y+height; r++)
	{
		lines = bits + r * pitch;

		for (i = 0; i < bytes; i++)
				
		buf[index++] = lines[x * bytes + i];
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

void FI_pixel_rgn_set_row (FIBITMAP* dib, BYTE* buf, unsigned x, unsigned y, unsigned width, int bytes)
{
	// Set several pixels of a region in a row.
	// This function draws the pixels from (x, y)
	// to (x+width-1, y) using the values of the buffer buf.
	// buf should be large enough to hold all these values.
	
	//dib :
	// a previously initialized FIBITMAP*.
	//buf :
	// a pointer to an array of BYTE  
	//x :
	// the x coordinate of the first pixel
	//y :
	// the y coordinate of the first pixel
	//width :
	// the number of pixels to set.
	
	unsigned dw  = FreeImage_GetWidth(dib);
	
	unsigned dh = FreeImage_GetHeight(dib);
	
	unsigned pitch  = FreeImage_GetPitch(dib);
	
	unsigned c, i;
	
	BYTE* bits = (BYTE*)FreeImage_GetBits(dib); // The image raster
	
	BYTE* lines = bits + y * pitch;
	
	int index = 0;
	
	for (c = x; c < x+width; c++)

		for (i = 0; i < bytes; i++)
		
		lines[c * bytes + i] = buf[index++];
}

/////////////////////////////////////////////////////////////////////////////////////////////

void FI_pixel_rgn_set_col (FIBITMAP* dib, BYTE* buf, unsigned x, unsigned y, unsigned height, int bytes)
{
	// Set several pixels of a region's column.
	// This function draws the pixels from (x, y)
	// to (x, y+height-1) using the values from the buffer buf.
	// buf should be large enough to hold all these values.
	
	//dib :
	// a previously initialized FIBITMAP*.
	//buf :
	// a pointer to an array of BYTE  
	//x :
	// the x coordinate of the first pixel
	//y :
	// the y coordinate of the first pixel
	//height :
	// the number of pixels to set.
	
	unsigned dw  = FreeImage_GetWidth(dib);
	
	unsigned dh = FreeImage_GetHeight(dib);
	
	unsigned pitch  = FreeImage_GetPitch(dib);
	
	unsigned r, i;
	
	BYTE* bits = (BYTE*)FreeImage_GetBits(dib); // The image raster
	
	BYTE* lines;  
	
	int index = 0;
	
	for (r = y; r < y+height; r++)
	{
		lines = bits + r * pitch;

		for (i = 0; i < bytes; i++)
		
		lines[x * bytes + i] = buf[index++];
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

static FIBITMAP* unsharp_mask (FIBITMAP* src_dib, double radius, double amount, int threshold, int only_blur)
{  
	double *cmatrix = NULL;
	int     cmatrix_length;
	double *ctable;
	
	BYTE* src;
	
	BYTE* dest;

	unsigned bpp = FreeImage_GetBPP(src_dib);
	
	unsigned bytes = bpp/8;
	
	unsigned width = FreeImage_GetWidth(src_dib);
	
	unsigned height = FreeImage_GetHeight(src_dib);
	
	unsigned pitch = FreeImage_GetPitch(src_dib);
	
	unsigned row, col;
	
	FIBITMAP* dst_dib = FreeImage_Allocate(width, height, bpp);
	
	// generate convolution matrix
	//   and make sure it's smaller than each dimension
	cmatrix_length = gen_convolve_matrix (radius, &cmatrix);
	
	// generate lookup table
	ctable = gen_lookup_table (cmatrix, cmatrix_length);
	
	// allocate buffers
	src  = (BYTE*) calloc (MAX (width, height) * bytes, sizeof(BYTE));
	dest = (BYTE*) calloc (MAX (width, height) * bytes, sizeof(BYTE));
	
	// blur the rows
	for (row = 0; row < height; row++)
    {
		FI_pixel_rgn_get_row (src_dib, src, 0, row, width, bytes);
        blur_line (ctable, cmatrix, cmatrix_length, src, dest, width, bytes);
		FI_pixel_rgn_set_row (dst_dib, dest, 0, row, width, bytes);
    }
	
	// blur the cols
	for (col = 0; col < width; col++)
    {
		FI_pixel_rgn_get_col (dst_dib, src, col, 0, height, bytes);
        blur_line (ctable, cmatrix, cmatrix_length, src, dest, height, bytes);
		FI_pixel_rgn_set_col (dst_dib, dest, col, 0, height, bytes);
    }

	if (!only_blur)
	{
	// merge the source and destination (which currently contains
	// the blurred version) images
	for (row = 0; row < height; row++)
    {
		const BYTE *s = src;
		BYTE       *d = dest;
		int          u, v;
		
		// get source row
		FI_pixel_rgn_get_row (src_dib, src, 0, row, width, bytes);
		
		// get dest row
		FI_pixel_rgn_get_row (dst_dib, dest, 0, row, width, bytes);
		
		// combine the two
		for (u = 0; u < width; u++)
        {
			for (v = 0; v < bytes; v++)
            {
				int value;
				int diff = *s - *d;
				
				// do tresholding
				if (abs (2 * diff) < threshold)
					diff = 0;
				
				value = *s++ + amount * diff;
				*d++ = FI_Clamp (value);
            }
        }
		
		FI_pixel_rgn_set_row (dst_dib, dest, 0, row, width, bytes);
    }
	}
	
	free (dest);
	free (src);
	free (ctable);
	free (cmatrix);

    /*if(bpp == 8)
	{
		// copy the original palette to the destination bitmap
		
		RGBQUAD *src_pal = FreeImage_GetPalette(src_dib);
		RGBQUAD *dst_pal = FreeImage_GetPalette(dst_dib);
		memcpy(&dst_pal[0], &src_pal[0], 256 * sizeof(RGBQUAD));		
	}
	
	// Copying the DPI...
	
	FreeImage_SetDotsPerMeterX(dst_dib, FreeImage_GetDotsPerMeterX(src_dib));
	
    FreeImage_SetDotsPerMeterY(dst_dib, FreeImage_GetDotsPerMeterY(src_dib));*/

	return dst_dib;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------------------------------
/**
FreeImage error handler
@param fif Format / Plugin responsible for the error 
@param message Error message
*/
/*void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message) {
	printf("\n*** "); 
	printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	printf(message);
	printf(" ***\n");
}*/

/////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------------------------------

/** Generic image loader

  @param lpszPathName Pointer to the full file name
  @param flag Optional load flag constant
  @return Returns the loaded dib if successful, returns NULL otherwise
*/

/*FIBITMAP* GenericLoader(const char* lpszPathName, int flag)
{	
	FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;
	// check the file signature and deduce its format
	// (the second argument is currently not used by FreeImage)
	
	fif = FreeImage_GetFileType(lpszPathName, 0);
	
	FIBITMAP* dib;
	
	if(fif == FIF_UNKNOWN)
	{
		// no signature ?
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName);
	}
	
	// check that the plugin has reading capabilities ...
	if((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif))
	{
		// ok, let's load the file
		dib = FreeImage_Load(fif, lpszPathName, flag);
		
		// unless a bad file format, we are done !
		if (!dib)
		{
			printf("%s%s%s\n","File \"", lpszPathName, "\" not found.");
			return NULL;
		}
	}	
	
	return dib;
}*/

/////////////////////////////////////////////////////////////////////////////////////////////
/*
The most widely useful method for sharpening an image,
The unsharp mask is a sharpening filter that works
by comparing using the difference of the image and
a blurred version of the image.  It is commonly
used on photographic images, and is provides a much
more pleasing result than the standard sharpen
filter.
Winston Chang <winstonc@cs.wisc.edu>, 1999

	
double  radius; // Radius of gaussian blur (in pixels > 1.0)
double  amount; // Strength of effect
int     threshold; // Threshold (0-255)
	
5.0, // default radius
0.5, // default amount
0    // default threshold
*/

uint8_t *unsharp(uint8_t *src, int width, int height, int bpp) {
    FIBITMAP *dib = new FIBITMAP;
    dib->bits = src;
    dib->width = width;
    dib->height = height;
    dib->bpp = bpp;
    dib->pitch = PAD(width * bpp / 8);

    double radius = 0.2;

    double amount = 0.5;

    int threshold = 1;

    int only_blur = 0;

    if (FreeImage_GetBPP(dib) == 8 || FreeImage_GetBPP(dib) == 24)
    {
        FIBITMAP* dst_dib = unsharp_mask (dib, radius, amount, threshold, only_blur);

        if (dst_dib)
        {
            delete dib;
            uint8_t *out = dst_dib->bits;
            delete dst_dib;
            return out;
        }
    }
    else
        printf("%s\n", "Unsupported color mode.");

    delete dib;
    return nullptr;
}

/*int main(int argc, char *argv[])
{
	
	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB
	
	// initialize your own FreeImage error handler
	
	FreeImage_SetOutputMessage(FreeImageErrorHandler);
	
	if(argc < 5) {
		printf("Usage : unsharp <input_file> <radius (double)> <amount (double)> <threshold (int)> <only_blur (bool)>\n");
		return 0;
	}
	
	FIBITMAP *dib = GenericLoader(argv[1], 0);
	
	double radius = atof(argv[2]);

	double amount = atof(argv[3]);

	int threshold = atof(argv[4]);

	int only_blur = 0;

	if(argc == 6) only_blur = atof(argv[5]);


	if (dib)
	{		
		// bitmap is successfully loaded!
		
		if (FreeImage_GetImageType(dib) == FIT_BITMAP)
		{
			if (FreeImage_GetBPP(dib) == 8 || FreeImage_GetBPP(dib) == 24)
			{
				FIBITMAP* dst_dib = unsharp_mask (dib, radius, amount, threshold, only_blur);
				
				if (dst_dib)
				{					
					// save the filtered bitmap
					const char *output_filename = "filtered.tif";
					
					// first, check the output format from the file name or file extension
					FREE_IMAGE_FORMAT out_fif = FreeImage_GetFIFFromFilename(output_filename);
					
					if(out_fif != FIF_UNKNOWN)
					{
						// then save the file
						FreeImage_Save(out_fif, dst_dib, output_filename, 0);
					}
					
					// free the loaded FIBITMAP
					FreeImage_Unload(dst_dib);					
				}
			}
			
			else
				
				printf("%s\n", "Unsupported color mode.");
		}
		
		else // non-FIT_BITMAP images are not supported.
			
			printf("%s\n", "Unsupported color mode.");
		
		FreeImage_Unload(dib);
	}	 
	
	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB
	
	return 0;
}*/

/////////////////////////////////////////////////////////////////////////////////////////////

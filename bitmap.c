/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Martino Pilia <martino.pilia@gmail.com> , 2015
 */

/*!
 * \file bitmap.c
 * \brief Operate on a bitmap file.
 * @author Martino Pilia <martino.pilia@gmail.com>
 * @date 2015-07-18
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bitmap.h"

/* Minimum macro. */
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* Indices for nibble mask. */
#define HI_NIBBLE 0
#define LO_NIBBLE 1

/* Read a value with a specific mask, removing trailing zeros. */
#define READ_MASK(val, mask) (((val) & (mask)) >> tr_zeros((mask)))

/* Length in bit for the string length encode in the steganographic 
 * functions. */ 
#define STEG_LEN 32

/* Update indices while reading channels of various pixels sequentially;
 * i is the pixel row, j the pixel column, ch the channel no. and w the width;
 * pixels are read by row, and for each pixel the first three channels are
 * read sequentially. */
#define NEXT(i, j, ch, w)   \
    if ((ch) + 1 == 3)      \
    {                       \
        (ch) = 0;           \
        if ((j) + 1 == (w)) \
        {                   \
            (j) = 0;        \
            ++(i);          \
        }                   \
        else                \
            ++(j);          \
    }                       \
    else                    \
        ++(ch);

/* binary mask for the bits and nibbles in a byte */
const uint8_t mask1[] = {128, 64, 32, 16, 8, 4, 2, 1};
const uint8_t mask4[] = {240, 15};

/*
 * \brief Count trailing zeros in the binary representation of a number.
 * @param val Input value.
 * @return Number of trailing binary zeros.
 */
static __inline__ unsigned int tr_zeros(uint32_t val) 
    __attribute__((always_inline));

/*
 * Count trailing binary zeros.
 */
static unsigned int tr_zeros(uint32_t val) 
{
    unsigned int res = 0;

    if (!val)
        return 0u;

    while (!(val & 0x1))
    {
        ++res;
        val >>= 1;
    }

    return res;
}

/*!
 * Allocate resources for a new image object.
 */
Image new_image(int width, int height, short bpp, int colors)
{
    Image res;
    Bmp_header *h = &res.bmp_header;
    long max_colors = 1;
    size_t pad;
    int i;
    memset(&res, 0, sizeof (Image));

    if (width < 1 || height < 1 || colors < 0)
    {
        fprintf(stderr, "new_image: invalid arguments.\n");
        return res;
    }

    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
    {
        fprintf(stderr, "new_image: invalid bpp value.\n");
        return res;
    }

    /* compute the max color number allowed with the input bpp */
    for (i = 0; i < bpp; ++i)
        max_colors *= 2;

    if (colors > max_colors)
    {
        fprintf(stderr, "new_image: incompatible bpp and colors number.\n");
        return res;
    }

    /* rows have a 4 byte alignment */
    pad = (4 - (bpp * width + 7) / 8 % 4) % 4;

    /* fill bitmap header */
    h->header_size = 40;
    h->bit_per_pixel = bpp;
    h->width = width;
    h->height = height;
    h->color_planes = 1;
    h->compression_type = 0;
    h->h_resolution = 2835;
    h->v_resolution = 2835;
    h->image_size = ((bpp * width + 7) / 8 + pad) * height;
    h->color_no = colors;
    h->important_color_no = colors;

    /* alloc pixel data (jagged array) */
    res.pixel_data = (Pixel**) malloc(height * sizeof (Pixel*));
    if (!res.pixel_data)
    {
        return res;
    }
    for (i = 0; i < height; ++i)
    {
        res.pixel_data[i] = (Pixel*) calloc(width, sizeof (Pixel));
        if (!res.pixel_data[i])
        {
            while (i > 0)
                free(res.pixel_data[--i]);
            free(res.pixel_data);
            return res;
        }
    }

    /* alloc color palette */
    res.palette = (Color*) calloc(colors, sizeof (Color));

    return res;
}

/*!
 * Destroy an image object.
 */
void destroy_image(Image *im)
{
    unsigned long i;

    /* soft check against double free */
    for (i = 0; i < im->bmp_header.height; ++i)
        if (im->pixel_data[i])
            free(im->pixel_data[i]);
    if (im->pixel_data)
        free(im->pixel_data);
    if (im->palette)
        free(im->palette);

    memset(im, 0, sizeof (Image));
}

/*!
 * Copy the content of an Image object into another, of possibly different
 * size.
 */
int copy_image(Image to, Image from)
{
    size_t i;
    size_t min_w = MIN(to.bmp_header.width, from.bmp_header.width);
    size_t min_h = MIN(to.bmp_header.height, from.bmp_header.height);

    for (i = 0; i < min_h; ++i)
        memcpy(to.pixel_data[i], from.pixel_data[i], min_w * sizeof (Pixel));

    return 0;
}

/*!
 * Open a bitmap file.
 */
Image open_bitmap(const char *filename)
{
    FILE *f; 
    File_header file_header; 
    Bmp_header *h;
    Image image;
    short allocated_palette = 0;
    size_t i, j;
    uint8_t *buf;
    uint8_t *bitmap_buffer;
    uint32_t h_size;
    size_t pad;
    short bit;

    memset(&image, 0, sizeof (Image));

    /* open input file */
    f = fopen(filename, "rb");
    if (f == NULL)
        return image;

    /* read the file header */
    fread(&file_header, sizeof (File_header), 1, f);
    if (ferror(f))
    {
        fclose(f);
        return image;
    }

    /* check the magic number to ensure this is a valid bmp file */
    if (file_header.file_type != 0x4D42)
    {
        fprintf(stderr, "Invalid magic number.\n");
        fclose(f);
        return image;
    }

    /* check the header size (4 byte value) */
    fread(&h_size, 4, 1, f);
    if (ferror(f))
    {
        fclose(f);
        return image;
    }
    fseek(f, -4, SEEK_CUR); /* restore pointer to the header start */

    /* read the bmp header */
    fread(&image.bmp_header, h_size, 1, f);
    if (ferror(f))
    {
        fclose(f);
        return image;
    }

    /* alias the header, to have an handy shorthand */
    h = &image.bmp_header;

    /* check wether the bit_per_pixel value is valid */
    if (h->bit_per_pixel != 1
            && h->bit_per_pixel != 4
            && h->bit_per_pixel != 8
            && h->bit_per_pixel != 16
            && h->bit_per_pixel != 24
            && h->bit_per_pixel != 32)
    {
        fclose(f);
        return image;
    }

    /* allocate memory for the palette and read it when present */
    if (h->color_no)
    {
        /* each color is stored as a 4 byte sequence */
        image.palette = (Color*) malloc(h->color_no * 4);
        fread(image.palette, h->color_no * 4, 1, f);
        if (ferror(f))
        {
            free(image.palette);
            fclose(f);
            image.palette = NULL;
            return image;
        }
        else
        {
            allocated_palette = 1;
        }
    }

    /* assert the bitmap data start has been reached */
    assert(ftell(f) == file_header.bmp_offset);

    /* allocate memory for the bitmap data (as a jagged array) */
    image.pixel_data = (Pixel**) malloc(h->height * sizeof (Pixel*));
    if (!image.pixel_data)
    {
        if (allocated_palette)
            free(image.palette);
        image.pixel_data = NULL;
        image.palette = NULL;
        fclose(f);
        return image;
    }
    for (i = 0; i < h->height; ++i)
    {
        image.pixel_data[i] = (Pixel*) malloc(h->width * sizeof (Pixel));
        if (!image.pixel_data[i])
        {
            if (allocated_palette)
                free(image.palette);
            while (i > 0)
                free(image.pixel_data[--i]);
            image.pixel_data = NULL;
            image.palette = NULL;
            fclose(f);
            return image;
        }
    }

    /* allocate buffer for the file content */
    bitmap_buffer = (uint8_t*) calloc(1, h->image_size);
    if (!bitmap_buffer)
    {
        for (i = 0; i < h->height; ++i)
            free(image.pixel_data[i]);
        free(image.pixel_data);
        if (allocated_palette)
            free(image.palette);
        image.pixel_data = NULL;
        image.palette = NULL;
        fclose(f);
        return image;
    }

    /* read bitmap data from the file and put it into the buffer */
    fread(bitmap_buffer, h->image_size, 1, f);
    if (ferror(f))
    {
        for (i = 0; i < h->height; ++i)
            free(image.pixel_data[i]);
        free(image.pixel_data);
        if (allocated_palette)
            free(image.palette);
        image.pixel_data = NULL;
        image.palette = NULL;
        fclose(f);
        return image;
    }

    /* convert bitmap data into high level pixel representation */
    /* +7 is to round up to the ceil value in the division */
    pad = (4 - ((h->width * h->bit_per_pixel + 7) / 8) % 4) % 4;
    buf = bitmap_buffer;
    switch (h->bit_per_pixel)
    {
        /* each byte of data represents 8 pixels, with the most significant 
         * bit mapped into the leftmost pixel */
        case 1:
            for (i = 0; i < h->height; ++i)
            {
                bit = 0;
                for (j = 0; j < h->width; ++j)
                {
                    /* get the right bit from the current byte, 
                     * starting from the most significative one */
                    image.pixel_data[i][j].i = READ_MASK(*buf, mask1[bit]);
                    ++bit;
                    
                    /* when the current byte has been fully read,
                     * advance to the next one */
                    if (bit == 8)
                    {
                        bit = 0;
                        ++buf;
                    }
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each byte represents 2 pixel, with the most significant nibble
         * mapped into the leftmost pixel */
        case 4:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; j += 2)
                {
                    /* read the two pixels in the current byte */
                    image.pixel_data[i][j].i = 
                        READ_MASK(*buf, mask4[HI_NIBBLE]);

                    if (j + 1 < h->width)
                        image.pixel_data[i][j + 1].i = 
                            READ_MASK(*buf, mask4[LO_NIBBLE]);

                    /* advance to the next byte */
                    ++buf;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each byte represents 1 pixel */
        case 8:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                    image.pixel_data[i][j].i = *(buf++);

                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each pixel is represented with 2 bytes */
        case 16:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                {
                    uint16_t *px = (uint16_t*) buf;
                    image.pixel_data[i][j].b = READ_MASK(*px, h->blue_mask);
                    image.pixel_data[i][j].g = READ_MASK(*px, h->green_mask);
                    image.pixel_data[i][j].r = READ_MASK(*px, h->red_mask);

                    /* advance to the next pixel (half-word) */
                    buf += 2;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each pixel is represented with 3 bytes, with 1 byte for each 
         * component */
        case 24:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                {
                    image.pixel_data[i][j].b = *(buf++);
                    image.pixel_data[i][j].g = *(buf++);
                    image.pixel_data[i][j].r = *(buf++);
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        case 32:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                {
                    uint32_t *px = (uint32_t*) buf;
                    image.pixel_data[i][j].b = READ_MASK(*px, h->blue_mask);
                    image.pixel_data[i][j].g = READ_MASK(*px, h->green_mask);
                    image.pixel_data[i][j].r = READ_MASK(*px, h->red_mask);
                    image.pixel_data[i][j].i = READ_MASK(*px, h->alpha_mask);

                    /* advance to the next pixel (word) */
                    buf += 4;;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;
    }

    /* free buffer */
    free(bitmap_buffer);

    fclose(f);
    return image;
}

/*!
 * Save a bitmap image.
 */
int save_bitmap(Image image, const char *filename)
{
    FILE *f;
    size_t i, j;
    Bmp_header *h = &image.bmp_header;
    uint8_t *bitmap_buffer;
    uint8_t *buf;
    size_t pad = (4 - ((h->width * h->bit_per_pixel + 7) / 8) % 4) % 4;
    File_header file_header = 
    {
        /* bmp magic number */
        0x4D42, 
        
        /* file size */
        sizeof (File_header) 
            + h->header_size 
            + h->color_no * 4
            + h->image_size,

        /* reserved */
        0,
        0,

        /* bmp offset */
        sizeof (File_header)
            + h->header_size
            + h->color_no * 4
    };

    /* open output file */
    f = fopen(filename, "wb");
    if (!f)
        return 1;

    /* write file header */
    fwrite(&file_header, sizeof (File_header), 1, f);
    if (ferror(f))
    {
        fclose(f);
        return 1;
    }

    /* write bmp header */
    fwrite(h, h->header_size, 1, f);
    if (ferror(f))
    {
        fclose(f);
        return 1;
    }

    /* write color palette if present */
    if (h->color_no)
    {
        fwrite(image.palette, h->color_no * 4, 1, f);
        if (ferror(f))
        {
            fclose(f);
            return 1;
        }
    }

    /* allocate buffer for bitmap pixel data */
    bitmap_buffer = (uint8_t*) calloc(1, h->image_size);
    buf = bitmap_buffer;
    
    /* convert pixel data into bitmap format */
    switch (h->bit_per_pixel)
    {
        /* each byte of data represents 8 pixels, with the most significant 
         * bit mapped into the leftmost pixel */
        case 1:
            for (i = 0; i < h->height; ++i)
            {
                j = 0;
                while (j < h->width)
                {
                    short bit;
                    uint8_t tmp = 0;
                    for (bit = 7; bit >= 0 && j < h->width; --bit)
                    {
                        tmp |= (image.pixel_data[i][j].i == 0 ? 0u : 1u) << bit;
                        ++j;
                    }
                    *buf++ = tmp;
                }
                /* each row has a padding for 4 byte alignment */
                buf += pad;
            }
            break;

        /* each byte represents 2 pixel, with the most significant nibble
         * mapped into the leftmost pixel */
        case 4:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; j += 2)
                {
                    /* write two pixels in the one byte variable tmp */
                    uint8_t tmp = 0;
                    /* most significant nibble */
                    tmp |= image.pixel_data[i][j].i << 4;
                    if (j + 1 < h->height)
                        /* least significant nibble */
                        tmp |= image.pixel_data[i][j + 1].i & mask4[LO_NIBBLE];

                    /* write the byte in the image buffer */
                    *buf++ = tmp;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each byte represents 1 pixel */
        case 8:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                    *buf++ = image.pixel_data[i][j].i;
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each pixel is represented with 2 bytes */
        case 16:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                {
                    uint16_t *px = (uint16_t*) buf;
                    *px = 
                        (image.pixel_data[i][j].b << tr_zeros(h->blue_mask)) +
                        (image.pixel_data[i][j].g << tr_zeros(h->green_mask)) + 
                        (image.pixel_data[i][j].r << tr_zeros(h->red_mask));

                    /* advance to the next pixel (half-word) */
                    buf += 2;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each pixel is represented with 3 bytes, with 1 byte for each 
         * color component */
        case 24:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                {
                    *buf++ = image.pixel_data[i][j].b;
                    *buf++ = image.pixel_data[i][j].g;
                    *buf++ = image.pixel_data[i][j].r;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;

        /* each pixel is represented with 4 bytes */
        case 32:
            for (i = 0; i < h->height; ++i)
            {
                for (j = 0; j < h->width; ++j)
                {
                    uint32_t *px = (uint32_t*) buf;
                    *px = 
                        (image.pixel_data[i][j].b << tr_zeros(h->blue_mask)) +
                        (image.pixel_data[i][j].g << tr_zeros(h->green_mask)) + 
                        (image.pixel_data[i][j].r << tr_zeros(h->red_mask)) + 
                        (image.pixel_data[i][j].i << tr_zeros(h->alpha_mask));

                    /* advance to the next pixel (word) */
                    buf += 4;
                }
                /* each row has a padding to a 4 byte alignment */
                buf += pad;
            }
            break;
    }
    
    /* write pixel data in the file */
    assert(file_header.bmp_offset == ftell(f));
    fwrite(bitmap_buffer, h->image_size, 1, f);
    if (ferror(f))
    {
        free(bitmap_buffer);
        fclose(f);
        return 1;
    }

    free(bitmap_buffer);
    fclose(f);
    return 0;
}

/*!
 * Return a string containing a human readable dump of the image properties.
 */
char* bmp_dump(Image image)
{
    /* 22 * 26 is an extimation for the header dump, 
     * 21 * color_no is for the palette */
    char *out = (char*) malloc(22 * 26 + 21 * image.bmp_header.color_no);
    sprintf(out,
            "Header size:  %10d\n"
            "Image width:  %10d\n"
            "Image height: %10d\n"
            "Color planes: %10d\n"
            "Bit per px:   %10d\n"
            "Compression:  %10d\n"
            "Bitmap size:  %10d\n"
            "X resolution: %10d\n"
            "Y resolution: %10d\n"
            "Colors:       %10d\n"
            "Important:    %10d\n"
            "red_mask      %#010x\n"
            "green_mask    %#010x\n"
            "blue_mask     %#010x\n"
            "alpha_mask    %#010x\n"
            "cs_type       %10d\n"
            "gamma_red     %10d\n"
            "gamma_green   %10d\n"
            "gamma_blue    %10d\n"
            "intent        %10d\n"
            "profile_data  %10d\n"
            "profile_size  %10d\n",
            image.bmp_header.header_size,
            image.bmp_header.width,
            image.bmp_header.height,
            image.bmp_header.color_planes,
            image.bmp_header.bit_per_pixel,
            image.bmp_header.compression_type,
            image.bmp_header.image_size,
            image.bmp_header.h_resolution,
            image.bmp_header.v_resolution,
            image.bmp_header.color_no,
            image.bmp_header.important_color_no,
            image.bmp_header.red_mask,
            image.bmp_header.green_mask,
            image.bmp_header.blue_mask,
            image.bmp_header.alpha_mask,
            image.bmp_header.cs_type,
            image.bmp_header.gamma_red,
            image.bmp_header.gamma_green,
            image.bmp_header.gamma_blue,
            image.bmp_header.intent,
            image.bmp_header.profile_size,
            image.bmp_header.profile_size
            );
    if (image.bmp_header.color_no)
    {
        strcat(out, "\nPalette:\n");
        for (size_t i = 0; i < image.bmp_header.color_no; ++i)
        {
            char buf[100];
            sprintf(buf, 
                    "%3lu: %3u %3u %3u %3u\n",
                    i,
                    image.palette[i].r,
                    image.palette[i].g,
                    image.palette[i].b,
                    image.palette[i].a
                   );
            strcat(out, buf);
        }
    }
    return out;
}

/*!
 * Return a string containing an ASCII art representation for the 
 * two colors input image.
 */
char* ascii_print(Image image)
{
    char *out;
    long i, j, k;
    Bmp_header *h = &image.bmp_header;

    if (h->color_no != 2)
    {
        fprintf(stderr, "ascii_print can print two colors images only.\n");
        return NULL;
    }

    /* memory for the output string (+1 for row and string terminators) */
    out = (char*) malloc((h->width + 1) * h->height + 1);
    if (!out)
    {
        return NULL;
    }

    /* pixels are stored from bottom to top, left to right */
    k = 0;
    for (i = h->height - 1; i >= 0; --i)
    {
        for (j = 0; j < (long) h->width; ++j)
            out[k++] = (image.pixel_data[i][j].i ? '*' : ' ');
        out[k++] = '\n';
    }
    out[k] = '\0';

    return out;
}

/*!
 * Get the histogram for a channel.
 */
unsigned long* histogram(Image image, const int channel)
{
    size_t i, j;
    unsigned long *hist;

    if (channel < 0 || channel > 3)
    {
        fprintf(stderr, "histogram: invalid channel parameter.\n");
        return NULL;
    }

    hist = (unsigned long*) calloc(256, sizeof (unsigned long));
    if (!hist)
    {
        fprintf(stderr, "histogram: memory error.\n");
        return NULL;
    }

    for (i = 0; i < image.bmp_header.height; ++i)
        for (j = 0; j < image.bmp_header.width; ++j)
            /* convert packed struct pointer into an array pointer
             * to access the channel */
            hist[((uint8_t*) &image.pixel_data[i][j])[channel]] += 1;
    
    return hist; 
}

/*!
 * Apply an histogram equalization algorithm.
 */
int equalize(Image image, const int channel)
{
    size_t i, j;
    const int li = 256; /* levels in the input image */
    const int lo = 256; /* levels in output image */
    unsigned long area = image.bmp_header.width * image.bmp_header.height;
    const float c = (float) lo / (float) area; /* coefficient */
    unsigned long cdf[li];        /* cumulative distribution function */
    unsigned long *h;             /* histogram for the channel */

    if (channel < 0 || channel > 3)
    {
        fprintf(stderr, "equalize: invalid channel.\n");
        return 1;
    }
    
    /* get histogram */
    h = histogram(image, channel);
    if (!h)
    {
        fprintf(stderr, "equalize: unable to create histogram.\n");
        return 1;
    }

    /* compute cdf */
    cdf[0] = h[0];
    for (i = 1; i < li; ++i)
        cdf[i] = cdf[i - 1] + h[i];

    /* equalize */
    for (i = 0; i < image.bmp_header.height; ++i)
    {
        for (j = 0; j < image.bmp_header.width; ++j)
        {
            /* convert packed struct pointer into an array pointer
             * to access the channel */
            uint8_t *px = (uint8_t*) &image.pixel_data[i][j];
            px[channel] = c * cdf[px[channel]];
        }
    }

    free(h);
    return 0;
}

/*!
 * Convert the RGB color space into Y'CbCr (with Y, Cb and Cr in the range
 * 0-255), applying the following transformation:
 * \f[
 *   Y   = 0.299 \cdot R + 0.587 \cdot G + 0.114 * B \\
 *   C_b = 128 + 0.564 \cdot (B - Y) \\
 *   C_r = 128 + 0.713 \cdot (R - Y)
 * \f]
 */
int rgb2ycbcr(Image image)
{
    size_t i, j;

    for (i = 0; i < image.bmp_header.height; ++i)
    {
        for (j = 0; j < image.bmp_header.width; ++j)
        {
            Pixel p = image.pixel_data[i][j];
            uint8_t y;

            /* Y */
            image.pixel_data[i][j].b = y =
                  0.299 * p.r
                + 0.587 * p.g 
                + 0.114 * p.b;

            /* Cb */
            image.pixel_data[i][j].g = 128 + 0.713 * (p.b - y);

            /* Cr */
            image.pixel_data[i][j].r = 128 + 0.564 * (p.r - y);
        }
    }
    return 0;
}

/*!
 * Convert the Y'CbCr color space into RGB, applying the following
 * transformation:
 * \f[
 *   R = Y + 1.403 \cdot (C_r - 128) \\
 *   G = Y - 0.714 \cdot (C_r - 128) - 0.344 \cdot (C_b - 128) \\
 *   B = Y + 1.773 \cdot (C_b - 128)
 * \f]
 */
int ycbcr2rgb(Image image)
{
    size_t i, j;

    for (i = 0; i < image.bmp_header.height; ++i)
    {
        for (j = 0; j < image.bmp_header.width; ++j)
        {
            Pixel p = image.pixel_data[i][j];

            /* R */
            image.pixel_data[i][j].r =
                  p.b                  /* Y  */
                + 0                    /* Cb */
                + 1.402 * (p.r - 128); /* Cr */

            /* G */
            image.pixel_data[i][j].g =
                  p.b                    /* Y  */
                - 0.34414 * (p.g - 128)  /* Cb */
                - 0.71414 * (p.r - 128); /* Cr */

            /* B */
            image.pixel_data[i][j].b =
                  p.b                  /* Y  */
                + 1.772 * (p.g - 128)  /* Cb */
                + 0;                   /* Cr */
        }
    }
    return 0;
}

/*!
 * Write an hidden text message inside a bitmap. Each color channel of each 
 * pixel holds a bit of the message; pixels are read from bottom left to top 
 * right, while channels for each pixel are read from B to R. The bits of
 * the characters or numbers are written in little endian order.
 *
 * The value of each channel is zero if its value is even, one if it is odd.
 * The evenness of the values is manipulated to encode the message while doing
 * only a quasi invisible change to the image aspect.
 *
 * A bitmap of size \f$ width \cdot height \f$ can hold 
 * \f$ 3 \cdot width \cdot height \f$ bits of data. The first 32 bits are used
 * to encode the length of the payload message. Then the message follows, and
 * the eventual exceeding channels are filled with random data.
 */
int steganography_write(Image image, const char *string)
{
    Bmp_header *h = &image.bmp_header;
    size_t len = strlen(string) + 1; /* include termination character */
    size_t allowed_len = (h->width * h->height * 3 - STEG_LEN) / CHAR_BIT;
    unsigned long i, j, k, l, ch;
    uint8_t *px;

    if (len > allowed_len)
    {
        fprintf(stderr,
                "steganography_write: the input string is too long, "
                "the maximum allowed string length for this image is %ld\n",
                allowed_len);
        return 1;
    }

    if (h->bit_per_pixel < 16)
    {
        fprintf(stderr, 
                "steganography_write: only 16 bit or higher bpp images"
                "allowed\n");
        return 1;
    }

    /* write len in the first STEG_LEN pixels */
    /* even = zero, odd = 1 */
    i = j = ch = 0;
    for (k = 0; k < STEG_LEN; ++k)
    {
        px = (uint8_t*) &image.pixel_data[i][j];
        if (px[ch] == 255)
            --px[ch]; /* prevent overflow */
        px[ch] += (px[ch] % 2 + ((len >> k) & 0x1)) % 2;
        NEXT(i, j, ch, h->width);
    }

    /* write the actual string (l-th pixel of k-th character) following */
    for (k = 0; k < len; ++k)
    {
        for (l = 0; l < CHAR_BIT; ++l)
        {
            px = (uint8_t*) &image.pixel_data[i][j];
            if (px[ch] == 255)
                --px[ch]; /* prevent overflow */
            px[ch] += (px[ch] % 2 + ((string[k] >> l) & 0x1)) % 2;
            NEXT(i, j, ch, h->width);
        }
    }

    /* fill the rest of the image with random data */
    srand(time(NULL));
    while (i < h->height)
    {
        px = (uint8_t*) &image.pixel_data[i][j];
        if (px[ch] == 255)
            --px[ch]; /* prevent overflow */
        px[ch] += rand() % 2;
        NEXT(i, j, ch, h->width);
    }

    return 0;
}

/*!
 * Read the hidden message inside an image. Read the length of the encoded
 * message first, and read the message if it is valid. If the bitmap does not
 * actually contain an hidden message, the read can fail on the length check,
 * or maybe the operation may prosecute and return a string filled with
 * garbage. The user must be sure that the image under reading actually  
 * contains a valid message encoded.
 */
char* steganography_read(Image image)
{
    Bmp_header *h = &image.bmp_header;
    size_t allowed_len = (h->width * h->height * 3 - STEG_LEN) / CHAR_BIT;
    unsigned long i, j, k, l, ch;
    size_t len = 0;
    uint8_t *px;
    char *res;

    if (h->bit_per_pixel < 16)
    {
        fprintf(stderr, 
                "steganography_read: only 16 bit or higher bpp images"
                "allowed\n");
        return NULL;
    }
    
    /* read the string length (inclusive of termination character) */
    i = j = ch = 0;
    for (k = 0; k < STEG_LEN; ++k)
    {
        px = (uint8_t*) &image.pixel_data[i][j];
        len += (uint32_t) (px[ch] % 2) << k;
        NEXT(i, j, ch, h->width);
    }

    /* ensure the string length is valid */
    if (len > allowed_len)
    {
        fprintf(stderr, 
                "steganography_read: invalid string length read, probably"
                "the image does not contain a message.\n");
        return NULL;
    }

    /* read the message */
    res = (char*) calloc(len, sizeof (char));
    for (k = 0; k < len; ++k)
    {
        for (l = 0; l < CHAR_BIT; ++l)
        {
            px = (uint8_t*) &image.pixel_data[i][j];
            res[k] += (uint8_t) (px[ch] % 2) << l;
            NEXT(i, j, ch, h->width);
        }
    }

    return res;
}

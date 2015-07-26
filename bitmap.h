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
 * Copyright (C) Martino Pilia <martino.pilia@gmail.com>, 2015
 */

/*!
 * \file bitmap.h
 * \brief Operate on a bitmap file.
 * @author Martino Pilia <martino.pilia@gmail.com>
 * @date 2015-07-18
 */

#ifndef __BITMAP_INCLUDED
#define __BITMAP_INCLUDED 

#include <stdint.h>

/* Indices for RGB channels */
#define B 0 /*!< Blue channel index. */
#define G 1 /*!< Green channel index. */
#define R 2 /*!< Red channel index. */
#define A 3 /*!< Alpha channel index. */

/* Indices for YCbCr channels */
#define Y  0 /*!< Blue channel index. */
#define Cb 1 /*!< Green channel index. */
#define Cr 2 /*!< Red channel index. */

/*!
 * \brief Type for a CIE XYZ color.
 */
typedef struct Cie_xyz
{
    uint32_t x; /*!< X component. */
    uint32_t y; /*!< Y component. */
    uint32_t z; /*!< Z component. */
} __attribute__((packed)) Cie_xyz;

/*!
 * \brief Type for a CIE XYZ color triple.
 */
typedef struct Cie_xyz_triple 
{
    Cie_xyz  r; /*!< Red component. */
    Cie_xyz  g; /*!< Green component. */
    Cie_xyz  b; /*!< Blue component. */
} __attribute__((packed)) Cie_xyz_triple;

/*!
 * \brief Type for the image file header.
 */
typedef struct File_header
{
    uint16_t file_type;  /*!< File type. */
    uint32_t file_size;  /*!< Size (byte) of the bitmap file. */
    uint16_t reserved1;  /*!< Reserved (must be 0). */
    uint16_t reserved2;  /*!< Reserved (must be 0). */
    uint32_t bmp_offset; /*!< Byte offset to the bitmap. */
} __attribute__((packed)) File_header;

/*!
 * \brief Type for the bitmap v5 header.
 */
typedef struct Bmp_header
{
    uint32_t        header_size;        /*!< Size (byte) of this header. */
    uint32_t        width;              /*!< Width (px). */
    uint32_t        height;             /*!< Height (px). */
    uint16_t        color_planes;       /*!< Number of color planes (1). */
    uint16_t        bit_per_pixel;      /*!< Number of bits per pixel. */
    uint32_t        compression_type;   /*!< Compression type. */
    uint32_t        image_size;         /*!< Image size (byte). */
    uint32_t        h_resolution;       /*!< Pixels per meter in x axis. */
    uint32_t        v_resolution;       /*!< Pixels per meter in y axis. */
    uint32_t        color_no;           /*!< Number of image colors. */
    uint32_t        important_color_no; /*!< Important colors number. */
    uint32_t        red_mask;           /*!< Red component color mask. */
    uint32_t        green_mask;         /*!< Green component color mask. */
    uint32_t        blue_mask;          /*!< Blue component color mask. */
    uint32_t        alpha_mask;         /*!< Alpha component color mask. */
    uint32_t        cs_type;            /*!< Color space. */
    Cie_xyz_triple  endpoints;          /*!< Endpoints for the color space. */
    uint32_t        gamma_red;          /*!< Gamma for red. */
    uint32_t        gamma_green;        /*!< Gamma for green. */
    uint32_t        gamma_blue;         /*!< Gamma for blue. */
    uint32_t        intent;             /*!< Rendering intent. */
    uint32_t        profile_data;       /*!< Profile data offset (byte). */
    uint32_t        profile_size;       /*!< Profile data size (byte). */
    uint32_t        reserved;           /*!< Zero. */
} __attribute__((packed)) Bmp_header;

/*!
 * \brief Type for a palette color.
 *
 * The palette color is represented as a 4 values tuple (B, G, R, ZERO).
 */
typedef struct Color
{
    uint8_t b; /*!< Blue component. */
    uint8_t g; /*!< Green component. */
    uint8_t r; /*!< Red component. */
    uint8_t a; /*!< Zero. */
} __attribute__((packed)) Color;

/*!
 * \brief Size agnostic type for high level pixel manipulation.
 */
typedef struct Pixel
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t i;
} __attribute__((packed)) Pixel;

/*!
 * \brief Structured type for an image.
 */
typedef struct Image
{
    Bmp_header bmp_header; /*!< Header of the bitmap. */
    Pixel **pixel_data;    /*!< Pixel matrix (jagged array). */
    Color *palette;        /*!< Color palette (array). */
} Image;

/*!
 * \brief Allocate resources for a new image object.
 * @param width Image width.
 * @param height Image height.
 * @param bpp Bit per pixel.
 * @param colors Number of colors.
 * @return A blank image object.
 * @note The object must be deallocated with `destroy_image(Image*)`.
 */                                  
Image new_image(int width, int height, short bpp, int colors);

/*!
 * \brief Destroy an image object.
 * @param im Pointer to the Image object to destroy.
 */
void destroy_image(Image *im);

/*!
 * \brief Copy the content of an Image object into another. If the two
 *        images have different sizes, only the fitting data will be 
 *        copied.
 * @param to Destination Image object (must be initialized yet).
 * @param from Source Image object.
 * @return Zero on success, nonzero otherwise.
 */
int copy_image(Image to, Image from);

/*!
 * \brief Open a bitmap file.
 * @param filename Filename for the image.
 * @param bmp_header Pointer to store the image header.
 * @return The image palette and pixel data.
 */
Image open_bitmap(const char *filename);

/*!
 * \brief Save a bitmap image.
 * @param image Data for the bitmap.
 * @param filename Name for the output file.
 * @return Zero on success, nonzero on failure.
 */
int save_bitmap(Image image, const char *filename);

/*!
 * \brief Return a human readable dump of the image properties.
 * @param image Bitmap image.
 * @return A string containing the dump.
 */                                                             
char* bmp_dump(Image image);

/*!
 * \brief Return a string containing an ASCII art print of a
 *        two colors image.
 * @param image Image to be printed.
 * @return A string containing the ASCII art print.
 */
char* ascii_print(Image image);

/*!
 * \brief Get the histogram for a channel.
 * @param image Image.
 * @param channel Channel.
 */
unsigned long* histogram(Image image, const int channel);

/*!
 * \brief Apply an histogram equalization algorithm.
 * @param image Target image.
 * @param channel Channel to be equalized.
 * @return Zero on success.
 */
int equalize(Image image, const int channel);

/*!
 * \brief Convert image from RGB to a Y'CbCr color space.
 * @param image Image to be converted.
 * @return Zero on success.
 */
int rgb2ycbcr(Image image);

/*!
 * \brief Convert image from Y'CbCr to RGB color space.
 * @param image Image to be converted.
 * @return Zero on success.
 */
int ycbcr2rgb(Image image);

#endif

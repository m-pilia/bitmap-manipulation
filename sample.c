#include <stdio.h>
#include "bitmap.h"

int main(void)
{
    /* create new image (48x48, 1 bit per pixel, two color) */
    Image image = new_image(48, 48, 1, 2);
    /* define color 0 as white (the color 1 is black yet, as default value) */
    image.palette[0].r = image.palette[0].g = image.palette[0].b = 255;
    /* color the central pixel with black */
    image.pixel_data[24][24].i = 1;
    /* save the bitmap */
    save_bitmap(image, "sample.bmp");
    /* destroy the object */
    destroy_image(&image);



    /* open another image */
    image = open_bitmap("./test_images/24bit.bmp");
    /* print image info */
    printf("%s\n", bmp_dump(image));
    /* reflect image on vertical axis */
    unsigned int col = image.bmp_header.width;
    unsigned int half_col = col / 2;
    unsigned int rows = image.bmp_header.height;
    unsigned int i, j;
    Pixel buf;
    for (i = 0; i < rows; ++i)
    {
        /* for each row i */
        for (j = 0; j < half_col; ++j)
        {
            /* reflect: swap j-th pixel of the row with (col-j-1)-th one */
            buf = image.pixel_data[i][j];
            image.pixel_data[i][j] = image.pixel_data[i][col - j - 1];
            image.pixel_data[i][col - j - 1] = buf;
        }
    }
    /* save reflected image */
    save_bitmap(image, "24bit_reflected.bmp");
    /* destroy object */
    destroy_image(&image);



    /* open another image */
    image = open_bitmap("./test_images/unequalized_bw.bmp");
    /* equalize the index channel (A) */
    equalize(image, A);
    /* save */
    save_bitmap(image, "equalized_bw.bmp");
    /* destroy object */
    destroy_image(&image);
    
    return 0;
}

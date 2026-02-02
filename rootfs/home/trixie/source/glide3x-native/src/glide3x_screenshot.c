/*
 * glide3x_screenshot.c - BMP Screenshot export
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h> /* For CreateDirectoryA */

/*
 * RGB565 to RGB888 conversion
 */
static void convert_565_to_888(const uint16_t *src, uint8_t *dst, int width, int height)
{
    int x, y;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint16_t pixel = src[y * width + x];
            
            /* RGB565: RRRR RGGG GGGB BBBB */
            uint8_t r5 = (pixel >> 11) & 0x1F;
            uint8_t g6 = (pixel >> 5) & 0x3F;
            uint8_t b5 = pixel & 0x1F;
            
            /* Scale to 8-bit */
            uint8_t r8 = (r5 * 527 + 23) >> 6;
            uint8_t g8 = (g6 * 259 + 33) >> 6;
            uint8_t b8 = (b5 * 527 + 23) >> 6;
            
            /* BMP stores BGR */
            int dst_idx = (y * width + x) * 3;
            dst[dst_idx + 0] = b8;
            dst[dst_idx + 1] = g8;
            dst[dst_idx + 2] = r8;
        }
    }
}

/*
 * Save buffer as BMP
 */
void save_screenshot_bmp(uint16_t *buffer, int width, int height, int frame_num)
{
    char filename[256];
    static int dir_created = 0;
    
    if (!dir_created) {
        CreateDirectoryA("output_png", NULL);
        dir_created = 1;
    }
    
    snprintf(filename, sizeof(filename), "output_png/frame_%04d.bmp", frame_num);
    
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    
    /* BMP Header */
    int padded_width = (width * 3 + 3) & (~3);
    int image_size = padded_width * height;
    int file_size = 54 + image_size;
    
    uint8_t header[54] = {0};
    
    /* Bitmap File Header */
    header[0] = 'B';
    header[1] = 'M';
    *(uint32_t*)(header + 2) = file_size;
    *(uint32_t*)(header + 10) = 54; /* Offset to data */
    
    /* Bitmap Info Header */
    *(uint32_t*)(header + 14) = 40; /* Info header size */
    *(int32_t*)(header + 18) = width;
    *(int32_t*)(header + 22) = -height; /* Top-down */
    *(uint16_t*)(header + 26) = 1; /* Planes */
    *(uint16_t*)(header + 28) = 24; /* Bits per pixel */
    
    fwrite(header, 1, 54, f);
    
    /* Convert and write data */
    uint8_t *rgb888 = (uint8_t*)malloc(width * height * 3);
    if (rgb888) {
        convert_565_to_888(buffer, rgb888, width, height);
        
        /* Write row by row for padding if necessary, but if width is aligned we can write all */
        if (width * 3 == padded_width) {
             fwrite(rgb888, 1, width * height * 3, f);
        } else {
             uint8_t pad[3] = {0};
             for (int y = 0; y < height; y++) {
                 fwrite(&rgb888[y * width * 3], 1, width * 3, f);
                 fwrite(pad, 1, padded_width - width * 3, f);
             }
        }
        
        free(rgb888);
    }
    
    fclose(f);
}

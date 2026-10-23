/* bmp.c - 24 位 BMP 输出，输入为自顶向下的 RGBA */
#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put_u32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

int dp_write_bmp(const char *path, const unsigned char *rgba_topdown, int width, int height)
{
    FILE *f;
    int stride = (width * 3 + 3) & ~3;
    int row;
    unsigned long pixel_bytes = (unsigned long)stride * (unsigned long)height;
    unsigned long file_size = 54u + pixel_bytes;
    unsigned char header[54];
    unsigned char *line;

    if (!path || !rgba_topdown || width <= 0 || height <= 0) return 0;

    f = fopen(path, "wb");
    if (!f) return 0;

    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + 2, file_size);
    put_u32(header + 10, 54);
    put_u32(header + 14, 40);
    put_u32(header + 18, (unsigned long)width);
    put_u32(header + 22, (unsigned long)height);
    put_u16(header + 26, 1);
    put_u16(header + 28, 24);
    put_u32(header + 34, pixel_bytes);
    put_u32(header + 38, 2835);
    put_u32(header + 42, 2835);

    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) { fclose(f); return 0; }

    line = (unsigned char *)malloc((size_t)stride);
    if (!line) { fclose(f); return 0; }
    memset(line, 0, (size_t)stride);

    for (row = height - 1; row >= 0; --row) {
        const unsigned char *src = rgba_topdown + (size_t)row * (size_t)width * 4u;
        int x;
        for (x = 0; x < width; ++x) {
            line[x * 3 + 0] = src[x * 4 + 2];
            line[x * 3 + 1] = src[x * 4 + 1];
            line[x * 3 + 2] = src[x * 4 + 0];
        }
        if (fwrite(line, 1, (size_t)stride, f) != (size_t)stride) {
            free(line); fclose(f); return 0;
        }
    }
    free(line);
    fclose(f);
    return 1;
}

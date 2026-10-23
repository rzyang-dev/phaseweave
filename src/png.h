/* png.h - minimal RGBA PNG writer (zlib deflate) */
#ifndef DP_PNG_H
#define DP_PNG_H

#include <stddef.h>

int png_write_rgba(const char *path, const unsigned char *rgba,
                   int width, int height, char *err, size_t errsz);

#endif

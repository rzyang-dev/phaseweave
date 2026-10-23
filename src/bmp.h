/* bmp.h - 24 位 BMP 输出（行序自顶向下） */
#ifndef DP_BMP_H
#define DP_BMP_H

int dp_write_bmp(const char *path, const unsigned char *rgba_topdown, int width, int height);

#endif

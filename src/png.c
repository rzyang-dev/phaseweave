/* png.c - minimal 8-bit RGBA PNG writer using zlib */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static void put_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)((v >> 24) & 0xff);
    p[1] = (unsigned char)((v >> 16) & 0xff);
    p[2] = (unsigned char)((v >> 8) & 0xff);
    p[3] = (unsigned char)(v & 0xff);
}

static int write_chunk(FILE *f, const char type[4],
                       const unsigned char *data, unsigned long len)
{
    unsigned char hdr[8];
    unsigned long crc;
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, hdr + 4, 4);
    if (data && len) crc = crc32(crc, data, len);
    if (fwrite(hdr, 1, 8, f) != 8) return 0;
    if (data && len && fwrite(data, 1, len, f) != len) return 0;
    put_be32(hdr, crc);
    return fwrite(hdr, 1, 4, f) == 4;
}

int png_write_rgba(const char *path, const unsigned char *rgba,
                   int width, int height, char *err, size_t errsz)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned char ihdr[13];
    unsigned char *raw = NULL, *comp = NULL;
    uLong rawlen, complen;
    FILE *f = NULL;
    int y, ok = 0;

    if (err && errsz) err[0] = 0;
    if (!path || !rgba || width <= 0 || height <= 0) {
        if (err) snprintf(err, errsz, "invalid png arguments");
        return 0;
    }
    rawlen = (uLong)height * (1u + (uLong)width * 4u);
    raw = (unsigned char *)malloc((size_t)rawlen);
    if (!raw) { if (err) snprintf(err, errsz, "png: out of memory"); goto done; }
    for (y = 0; y < height; ++y) {
        unsigned char *row = raw + (size_t)y * (1u + (size_t)width * 4u);
        row[0] = 0; /* filter: none */
        memcpy(row + 1, rgba + (size_t)y * (size_t)width * 4u, (size_t)width * 4u);
    }
    complen = compressBound(rawlen);
    comp = (unsigned char *)malloc((size_t)complen);
    if (!comp) { if (err) snprintf(err, errsz, "png: out of memory"); goto done; }
    if (compress2(comp, &complen, raw, rawlen, Z_BEST_SPEED) != Z_OK) {
        if (err) snprintf(err, errsz, "png: zlib compression failed");
        goto done;
    }
    f = fopen(path, "wb");
    if (!f) { if (err) snprintf(err, errsz, "png: cannot create %s", path); goto done; }
    if (fwrite(sig, 1, 8, f) != 8) goto done;
    put_be32(ihdr, (unsigned long)width);
    put_be32(ihdr + 4, (unsigned long)height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    if (!write_chunk(f, "IHDR", ihdr, 13)) goto done;
    if (!write_chunk(f, "IDAT", comp, complen)) goto done;
    if (!write_chunk(f, "IEND", NULL, 0)) goto done;
    ok = 1;

done:
    if (f) fclose(f);
    free(raw);
    free(comp);
    return ok;
}

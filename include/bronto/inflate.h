/*
 * Bronto Engine - decompression (DEFLATE, RFC 1951; zlib, RFC 1950).
 *
 * What PNG images are compressed with, and what zip files and many other
 * formats use too. Both ways: deflate.c compresses, png.c decompresses.
 */
#ifndef BRONTO_INFLATE_H
#define BRONTO_INFLATE_H

#include "types.h"

/* A raw DEFLATE stream decompressed, br_alloc'd (br_free it), its size in
 * *out_size. NULL if the stream is damaged, or would be bigger than `limit`
 * bytes (0: no limit). */
void *br_inflate(const void *data, size_t size, size_t *out_size, size_t limit);
/* The same for a zlib stream (2-byte header, DEFLATE data, Adler-32 checksum, checked). */
void *br_zlib_decompress(const void *data, size_t size, size_t *out_size, size_t limit);

/* Compressed: a raw DEFLATE stream, br_alloc'd, its size in *out_size. `level`
 * 1 (fast) to 9 (smallest); 6 is a good middle. NULL only when out of memory. */
void *br_deflate(const void *data, size_t size, size_t *out_size, int level);
/* ... as a zlib stream (what br_zlib_decompress and PNG files take). */
void *br_zlib_compress(const void *data, size_t size, size_t *out_size, int level);

#endif /* BRONTO_INFLATE_H */

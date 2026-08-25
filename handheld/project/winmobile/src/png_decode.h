/*
 * png_decode.h -- minimal self-contained PNG reader for the Windows CE port.
 *
 * There is no ARM build of libpng or zlib for this toolchain (handheld/lib
 * ships x86 MSVC .libs only, with no sources), and dragging both in would cost
 * more code and more of the 32 MB process budget than the game's assets
 * justify: all 117 shipped PNGs are 8-bit and non-interlaced.
 *
 * Supported: bit depth 8, colour types 0/2/3/4/6, all five filters, tRNS,
 * multi-chunk IDAT.  Everything else is rejected with a log line rather than
 * decoded wrongly.
 */
#ifndef PNG_DECODE_H__
#define PNG_DECODE_H__

#include <cstddef>

/**
    Decodes a PNG in memory to tightly packed 8-bit RGBA.

    @param file        whole .png file contents
    @param fileSize    length of @a file in bytes
    @param outPixels   receives a new[]-allocated w*h*4 buffer; caller
                       delete[]s it.  Untouched on failure.
    @param outW,outH   receive the image dimensions
    @param outHasAlpha receives false when every pixel came out fully opaque,
                       which lets the caller upload a cheaper texture format.
                       May be NULL.
    @return true on success.  On failure nothing is allocated and the reason
            has been logged.
*/
bool pngDecodeRGBA(const unsigned char* file, size_t fileSize,
                   unsigned char** outPixels, int* outW, int* outH,
                   bool* outHasAlpha);

/** Convenience wrapper that reads @a path itself (through wce_fopen, so a
    relative path is resolved against the .exe directory). */
bool pngDecodeFileRGBA(const char* path,
                       unsigned char** outPixels, int* outW, int* outH,
                       bool* outHasAlpha);

#endif /* PNG_DECODE_H__ */

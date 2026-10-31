#pragma once

#include <cstddef>
#include <cstdint>

#include "image_utils.h"

namespace biopass {

// Converts a packed YUYV (YUY2) buffer into RGB8. `stride` is the number of
// bytes per row (may exceed width * 2 due to padding). Returns false if
// `size` is too small for the declared width/height/stride.
bool yuyvToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out);

// Expands a packed single-channel GREY/R8 buffer into RGB8 by replicating
// the channel. `stride` is the number of bytes per row.
bool greyToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out);

// In-memory byte order of a packed 8-bit-per-channel RGB pixel. libcamera
// names these formats after DRM fourccs, which describe a little-endian word,
// so the bytes in memory are the reverse of the name:
//   libcamera RGB888                -> B, G, R      (kBgr24)
//   libcamera BGR888                -> R, G, B      (kRgb24)
//   libcamera XRGB8888 / ARGB8888   -> B, G, R, X/A (kBgrx32)
//   libcamera XBGR8888 / ABGR8888   -> R, G, B, X/A (kRgbx32)
// The X/A byte is ignored.
enum class PackedRgbLayout { kBgr24, kRgb24, kBgrx32, kRgbx32 };

// Repacks a packed RGB buffer (see PackedRgbLayout) into RGB8. `stride` is
// the number of bytes per row. Returns false if `size` is too small.
bool packedRgbToRgb(const uint8_t* src, size_t size, int width, int height, int stride,
                    PackedRgbLayout layout, ImageRGB& out);

// Decodes a JPEG/MJPEG buffer (`bytes_used` valid bytes at `src`) into RGB8
// via libjpeg-turbo. Returns false on decode failure or a non-JPEG buffer.
bool mjpegToRgb(const uint8_t* src, size_t bytes_used, ImageRGB& out);

}  // namespace biopass

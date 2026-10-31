#pragma once

#include <cstddef>
#include <cstdint>

#include "image_utils.h"

namespace biopass {

// Raw converters: `stride` is bytes per row (may exceed the packed width);
// they return false if `size` is too small for width/height/stride.
bool yuyvToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out);

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

bool packedRgbToRgb(const uint8_t* src, size_t size, int width, int height, int stride,
                    PackedRgbLayout layout, ImageRGB& out);

// libjpeg-turbo; false on decode failure or a non-JPEG buffer.
bool mjpegToRgb(const uint8_t* src, size_t bytes_used, ImageRGB& out);

}  // namespace biopass

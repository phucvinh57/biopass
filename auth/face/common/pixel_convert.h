#pragma once

#include <cstddef>
#include <cstdint>

#include "image.h"

namespace biopass {

// Raw converters: `stride` is bytes per row (may exceed the packed width);
// they return false if `size` is too small for width/height/stride.
bool yuyvToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out);

bool greyToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out);

// 4:2:0 converters. Each plane has its own size and stride; chroma is
// subsampled 2x2, so a chroma plane has (width+1)/2 x (height+1)/2 samples.
// NV12: a Y plane, then one plane of interleaved U,V pairs (NV21: V,U, set
// swap_uv).
bool nv12ToRgb(const uint8_t* y, size_t y_size, int y_stride, const uint8_t* uv, size_t uv_size,
               int uv_stride, int width, int height, bool swap_uv, ImageRGB& out);

// YUV420 (I420): a Y plane, then separate U and V planes.
bool yuv420ToRgb(const uint8_t* y, size_t y_size, int y_stride, const uint8_t* u, size_t u_size,
                 const uint8_t* v, size_t v_size, int uv_stride, int width, int height,
                 ImageRGB& out);

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

// libjpeg-turbo; decodes at the JPEG header's size, which may differ from
// the configured frame size (some firmware pads, e.g. 1920x1088 for 1080p).
// False (and an empty `out`) on decode failure, a non-JPEG buffer, or a
// header over ImageRGB::kMaxDim.
bool mjpegToRgb(const uint8_t* src, size_t bytes_used, ImageRGB& out);

}  // namespace biopass

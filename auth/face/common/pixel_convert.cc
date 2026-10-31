#include "pixel_convert.h"

#include <turbojpeg.h>

#include <algorithm>
#include <cstring>

namespace biopass {

namespace {

inline uint8_t clamp_u8(int value) {
  return static_cast<uint8_t>(std::min(255, std::max(0, value)));
}

// BT.601 limited-range YUV -> RGB.
inline void yuvToRgbPixel(int y, int u, int v, uint8_t* dst) {
  const int c = y - 16;
  const int d = u - 128;
  const int e = v - 128;
  dst[0] = clamp_u8((298 * c + 409 * e + 128) >> 8);
  dst[1] = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
  dst[2] = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

}  // namespace

bool yuyvToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out) {
  if (!src || width <= 0 || height <= 0) {
    return false;
  }
  const size_t min_stride = static_cast<size_t>(width) * 2;
  const size_t row_stride = static_cast<size_t>(std::max(stride, static_cast<int>(min_stride)));
  const size_t required = row_stride * static_cast<size_t>(height - 1) + min_stride;
  if (size < required) {
    return false;
  }

  out = ImageRGB(width, height);
  for (int y = 0; y < height; ++y) {
    const uint8_t* src_row = src + static_cast<size_t>(y) * row_stride;
    uint8_t* dst_row = out.ptr() + static_cast<size_t>(y) * width * 3;
    for (int x = 0; x + 1 < width; x += 2) {
      const uint8_t y0 = src_row[x * 2 + 0];
      const uint8_t u = src_row[x * 2 + 1];
      const uint8_t y1 = src_row[x * 2 + 2];
      const uint8_t v = src_row[x * 2 + 3];
      yuvToRgbPixel(y0, u, v, dst_row + x * 3);
      yuvToRgbPixel(y1, u, v, dst_row + (x + 1) * 3);
    }
    // YUYV encodes pixels in pairs; a trailing unpaired column (odd width,
    // which real cameras never report) is left black.
  }
  return true;
}

bool greyToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out) {
  if (!src || width <= 0 || height <= 0) {
    return false;
  }
  const size_t row_stride = static_cast<size_t>(std::max(stride, width));
  const size_t required = row_stride * static_cast<size_t>(height - 1) + static_cast<size_t>(width);
  if (size < required) {
    return false;
  }

  out = ImageRGB(width, height);
  for (int y = 0; y < height; ++y) {
    const uint8_t* src_row = src + static_cast<size_t>(y) * row_stride;
    uint8_t* dst_row = out.ptr() + static_cast<size_t>(y) * width * 3;
    for (int x = 0; x < width; ++x) {
      const uint8_t value = src_row[x];
      dst_row[x * 3 + 0] = value;
      dst_row[x * 3 + 1] = value;
      dst_row[x * 3 + 2] = value;
    }
  }
  return true;
}

bool packedRgbToRgb(const uint8_t* src, size_t size, int width, int height, int stride,
                    PackedRgbLayout layout, ImageRGB& out) {
  if (!src || width <= 0 || height <= 0) {
    return false;
  }
  const bool four_bytes = layout == PackedRgbLayout::kBgrx32 || layout == PackedRgbLayout::kRgbx32;
  const bool red_first = layout == PackedRgbLayout::kRgb24 || layout == PackedRgbLayout::kRgbx32;
  const size_t bpp = four_bytes ? 4 : 3;
  const size_t min_stride = static_cast<size_t>(width) * bpp;
  const size_t row_stride = static_cast<size_t>(std::max(stride, static_cast<int>(min_stride)));
  const size_t required = row_stride * static_cast<size_t>(height - 1) + min_stride;
  if (size < required) {
    return false;
  }

  out = ImageRGB(width, height);
  const size_t r = red_first ? 0 : 2;
  const size_t b = red_first ? 2 : 0;
  for (int y = 0; y < height; ++y) {
    const uint8_t* src_row = src + static_cast<size_t>(y) * row_stride;
    uint8_t* dst_row = out.ptr() + static_cast<size_t>(y) * width * 3;
    if (!four_bytes && red_first) {
      std::memcpy(dst_row, src_row, min_stride);
      continue;
    }
    for (int x = 0; x < width; ++x) {
      const uint8_t* px = src_row + static_cast<size_t>(x) * bpp;
      dst_row[x * 3 + 0] = px[r];
      dst_row[x * 3 + 1] = px[1];
      dst_row[x * 3 + 2] = px[b];
    }
  }
  return true;
}

bool mjpegToRgb(const uint8_t* src, size_t bytes_used, ImageRGB& out) {
  if (!src || bytes_used < 2 || src[0] != 0xFF || src[1] != 0xD8) {
    return false;
  }

  tjhandle handle = tjInitDecompress();
  if (!handle) {
    return false;
  }

  int width = 0, height = 0, subsamp = 0, colorspace = 0;
  if (tjDecompressHeader3(handle, const_cast<unsigned char*>(src),
                          static_cast<unsigned long>(bytes_used), &width, &height, &subsamp,
                          &colorspace) != 0 ||
      width <= 0 || height <= 0) {
    tjDestroy(handle);
    return false;
  }

  out = ImageRGB(width, height);
  const int rc = tjDecompress2(handle, const_cast<unsigned char*>(src),
                               static_cast<unsigned long>(bytes_used), out.ptr(), width,
                               0 /* pitch: tightly packed */, height, TJPF_RGB, TJFLAG_FASTDCT);
  tjDestroy(handle);
  if (rc != 0) {
    out = ImageRGB();
    return false;
  }
  return true;
}

}  // namespace biopass

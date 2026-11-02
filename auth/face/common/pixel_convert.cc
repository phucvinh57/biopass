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

// Row stride to read a plane with (a stride below the packed row means
// packed rows), or 0 if `size` can't hold `rows` rows of `row_bytes`.
size_t planeStride(size_t size, int stride, size_t row_bytes, size_t rows) {
  const size_t row_stride = std::max(static_cast<size_t>(std::max(stride, 0)), row_bytes);
  return size >= row_stride * (rows - 1) + row_bytes ? row_stride : 0;
}

}  // namespace

bool nv12ToRgb(const uint8_t* y, size_t y_size, int y_stride, const uint8_t* uv, size_t uv_size,
               int uv_stride, int width, int height, bool swap_uv, ImageRGB& out) {
  if (!y || !uv || ImageRGB::byteSize(width, height) == 0) {
    return false;
  }
  const size_t chroma_w = (static_cast<size_t>(width) + 1) / 2;
  const size_t chroma_h = (static_cast<size_t>(height) + 1) / 2;
  const size_t y_row = planeStride(y_size, y_stride, static_cast<size_t>(width), height);
  const size_t uv_row = planeStride(uv_size, uv_stride, chroma_w * 2, chroma_h);
  if (y_row == 0 || uv_row == 0) {
    return false;
  }

  const size_t u_off = swap_uv ? 1 : 0;
  const size_t v_off = swap_uv ? 0 : 1;
  out = ImageRGB(width, height);
  for (int row = 0; row < height; ++row) {
    const uint8_t* y_src = y + static_cast<size_t>(row) * y_row;
    const uint8_t* uv_src = uv + static_cast<size_t>(row / 2) * uv_row;
    uint8_t* dst_row = out.ptr() + static_cast<size_t>(row) * width * 3;
    for (int x = 0; x < width; ++x) {
      const uint8_t* pair = uv_src + static_cast<size_t>(x / 2) * 2;
      yuvToRgbPixel(y_src[x], pair[u_off], pair[v_off], dst_row + x * 3);
    }
  }
  return true;
}

bool yuv420ToRgb(const uint8_t* y, size_t y_size, int y_stride, const uint8_t* u, size_t u_size,
                 const uint8_t* v, size_t v_size, int uv_stride, int width, int height,
                 ImageRGB& out) {
  if (!y || !u || !v || ImageRGB::byteSize(width, height) == 0) {
    return false;
  }
  const size_t chroma_w = (static_cast<size_t>(width) + 1) / 2;
  const size_t chroma_h = (static_cast<size_t>(height) + 1) / 2;
  const size_t y_row = planeStride(y_size, y_stride, static_cast<size_t>(width), height);
  const size_t u_row = planeStride(u_size, uv_stride, chroma_w, chroma_h);
  const size_t v_row = planeStride(v_size, uv_stride, chroma_w, chroma_h);
  if (y_row == 0 || u_row == 0 || v_row == 0) {
    return false;
  }

  out = ImageRGB(width, height);
  for (int row = 0; row < height; ++row) {
    const uint8_t* y_src = y + static_cast<size_t>(row) * y_row;
    const uint8_t* u_src = u + static_cast<size_t>(row / 2) * u_row;
    const uint8_t* v_src = v + static_cast<size_t>(row / 2) * v_row;
    uint8_t* dst_row = out.ptr() + static_cast<size_t>(row) * width * 3;
    for (int x = 0; x < width; ++x) {
      yuvToRgbPixel(y_src[x], u_src[x / 2], v_src[x / 2], dst_row + x * 3);
    }
  }
  return true;
}

bool yuyvToRgb(const uint8_t* src, size_t size, int width, int height, int stride, ImageRGB& out) {
  if (!src || ImageRGB::byteSize(width, height) == 0) {
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
  if (!src || ImageRGB::byteSize(width, height) == 0) {
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
  if (!src || ImageRGB::byteSize(width, height) == 0) {
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
  out = ImageRGB();
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

  // The stream's own header sizes the decode; ImageRGB caps it (an empty
  // out means the header is over the limit).
  out = ImageRGB(width, height);
  if (out.empty()) {
    tjDestroy(handle);
    return false;
  }
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

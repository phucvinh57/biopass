#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Row-major 8-bit RGB image (minimal cv::Mat replacement).
struct ImageRGB {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> data;  // size = width * height * 3

  // Larger than any camera frame or enrolled photo. Keeps every
  // width * height * 3 index below INT_MAX and stops absurd allocations.
  static constexpr int kMaxDim = 16384;

  // 0 for a size that is non-positive or above kMaxDim on either side.
  static size_t byteSize(int w, int h) {
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim)
      return 0;
    return static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
  }

  // A size byteSize() rejects gives an empty 0x0 image.
  ImageRGB() = default;
  ImageRGB(int w, int h) {
    const size_t bytes = byteSize(w, h);
    if (bytes == 0)
      return;
    width = w;
    height = h;
    data.assign(bytes, 0);
  }
  ImageRGB(int w, int h, const uint8_t* src) {
    const size_t bytes = byteSize(w, h);
    if (bytes == 0 || !src)
      return;
    width = w;
    height = h;
    data.assign(src, src + bytes);
  }

  bool empty() const { return data.empty(); }
  uint8_t* ptr() { return data.data(); }
  const uint8_t* ptr() const { return data.data(); }

  uint8_t& at(int y, int x, int c) { return data[index(y, x, c)]; }
  const uint8_t& at(int y, int x, int c) const { return data[index(y, x, c)]; }

  ImageRGB crop(int x1, int y1, int x2, int y2) const {
    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::min(width, x2);
    y2 = std::min(height, y2);
    int cw = x2 - x1, ch = y2 - y1;
    if (cw <= 0 || ch <= 0)
      return {};
    ImageRGB out(cw, ch);
    for (int r = 0; r < ch; r++)
      std::memcpy(&out.data[r * cw * 3], &data[((y1 + r) * width + x1) * 3], cw * 3);
    return out;
  }

  ImageRGB clone() const {
    ImageRGB out;
    out.width = width;
    out.height = height;
    out.data = data;
    return out;
  }

 private:
  size_t index(int y, int x, int c) const {
    return (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3 +
           static_cast<size_t>(c);
  }
};

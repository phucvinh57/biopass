#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

/**
 * Minimal RGB image container replacing cv::Mat.
 * Stores 3-channel (RGB) uint8 data in row-major order.
 */
struct ImageRGB {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> data;  // size = width * height * 3

  static size_t byteSize(int w, int h) {
    if (w <= 0 || h <= 0)
      return 0;
    const size_t ws = static_cast<size_t>(w);
    const size_t hs = static_cast<size_t>(h);
    if (ws > std::numeric_limits<size_t>::max() / hs / 3)
      return 0;
    return ws * hs * 3;
  }

  ImageRGB() = default;
  ImageRGB(int w, int h)
      : width(std::max(0, w)), height(std::max(0, h)), data(byteSize(width, height), 0) {}
  ImageRGB(int w, int h, const uint8_t* src) : width(std::max(0, w)), height(std::max(0, h)) {
    const size_t bytes = byteSize(width, height);
    if (bytes == 0 || !src) {
      width = 0;
      height = 0;
      return;
    }
    data.resize(bytes);
    std::memcpy(data.data(), src, bytes);
  }

  bool empty() const { return data.empty(); }
  uint8_t* ptr() { return data.data(); }
  const uint8_t* ptr() const { return data.data(); }

  uint8_t& at(int y, int x, int c) { return data[(y * width + x) * 3 + c]; }
  const uint8_t& at(int y, int x, int c) const { return data[(y * width + x) * 3 + c]; }

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
};

/**
 * Bilinear resize.
 */
inline ImageRGB resizeImage(const ImageRGB& src, int tw, int th) {
  if (src.empty())
    return {};
  ImageRGB dst(tw, th);
  float sx = (float)src.width / tw;
  float sy = (float)src.height / th;
  for (int y = 0; y < th; y++) {
    float fy = (y + 0.5f) * sy - 0.5f;
    int y0 = (int)std::floor(fy);
    int y1 = y0 + 1;
    float wy = fy - y0;
    y0 = std::max(0, std::min(y0, src.height - 1));
    y1 = std::max(0, std::min(y1, src.height - 1));
    for (int x = 0; x < tw; x++) {
      float fx = (x + 0.5f) * sx - 0.5f;
      int x0 = (int)std::floor(fx);
      int x1_c = x0 + 1;
      float wx = fx - x0;
      x0 = std::max(0, std::min(x0, src.width - 1));
      x1_c = std::max(0, std::min(x1_c, src.width - 1));
      for (int c = 0; c < 3; c++) {
        float v = (1 - wy) * ((1 - wx) * src.at(y0, x0, c) + wx * src.at(y0, x1_c, c)) +
                  wy * ((1 - wx) * src.at(y1, x0, c) + wx * src.at(y1, x1_c, c));
        dst.at(y, x, c) = (uint8_t)std::min(255.0f, std::max(0.0f, v + 0.5f));
      }
    }
  }
  return dst;
}

/**
 * Letterbox resize: scale to fit target keeping aspect ratio, pad with pad_val.
 */
inline ImageRGB imageLetterbox(const ImageRGB& src, int tw, int th, uint8_t pad_val = 114) {
  if (src.empty())
    return {};
  float scale = std::min((float)tw / src.width, (float)th / src.height);
  int nw = (int)std::round(src.width * scale);
  int nh = (int)std::round(src.height * scale);
  ImageRGB resized = resizeImage(src, nw, nh);

  ImageRGB out(tw, th);
  std::memset(out.data.data(), pad_val, out.data.size());

  int dx = (tw - nw) / 2;
  int dy = (th - nh) / 2;
  for (int r = 0; r < nh; r++)
    std::memcpy(&out.data[((dy + r) * tw + dx) * 3], &resized.data[r * nw * 3], nw * 3);
  return out;
}

/**
 * Resize with aspect-ratio preserving and zero-padding (for recognition).
 */
inline ImageRGB imageResizePad(const ImageRGB& src, int tw, int th) {
  return imageLetterbox(src, tw, th, 0);
}

/**
 * Area-average resize (downscaling).
 */
inline ImageRGB resizeImageArea(const ImageRGB& src, int tw, int th) {
  if (src.empty() || tw <= 0 || th <= 0)
    return {};
  ImageRGB dst(tw, th);

  double sx = (double)src.width / tw;
  double sy = (double)src.height / th;

  for (int y = 0; y < th; y++) {
    double y0 = y * sy;
    double y1 = (y + 1) * sy;
    int iy0 = (int)std::floor(y0);
    int iy1 = std::min(src.height, (int)std::ceil(y1));

    for (int x = 0; x < tw; x++) {
      double x0 = x * sx;
      double x1 = (x + 1) * sx;
      int ix0 = (int)std::floor(x0);
      int ix1 = std::min(src.width, (int)std::ceil(x1));

      double acc[3] = {0, 0, 0};
      double weight_sum = 0;
      for (int sy_i = iy0; sy_i < iy1; sy_i++) {
        double wy = std::min((double)(sy_i + 1), y1) - std::max((double)sy_i, y0);
        if (wy <= 0)
          continue;
        for (int sx_i = ix0; sx_i < ix1; sx_i++) {
          double wx = std::min((double)(sx_i + 1), x1) - std::max((double)sx_i, x0);
          if (wx <= 0)
            continue;
          double w = wx * wy;
          for (int c = 0; c < 3; c++) acc[c] += src.at(sy_i, sx_i, c) * w;
          weight_sum += w;
        }
      }

      for (int c = 0; c < 3; c++) {
        double v = (weight_sum > 0) ? acc[c] / weight_sum : 0.0;
        dst.at(y, x, c) = (uint8_t)std::min(255.0, std::max(0.0, std::round(v)));
      }
    }
  }
  return dst;
}

namespace image_utils_detail {

// 4-lobe Lanczos kernel.
inline double lanczos4(double x) {
  constexpr double kA = 4.0;
  if (x == 0.0)
    return 1.0;
  if (x <= -kA || x >= kA)
    return 0.0;
  const double px = M_PI * x;
  return kA * std::sin(px) * std::sin(px / kA) / (px * px);
}

// Lanczos4 sample weights/indices for one output coordinate.
inline void lanczos4Weights(double src_pos, int src_size, int* idx, double* w) {
  int center = (int)std::floor(src_pos);
  for (int k = -3; k <= 4; k++) {
    int i = center + k;
    double dist = src_pos - i;
    w[k + 3] = lanczos4(dist);
    idx[k + 3] = std::min(std::max(i, 0), src_size - 1);
  }
}

}  // namespace image_utils_detail

// Lanczos4 resize (upscaling): separable 8-tap windowed-sinc resampling.
inline ImageRGB resizeImageLanczos4(const ImageRGB& src, int tw, int th) {
  if (src.empty() || tw <= 0 || th <= 0)
    return {};
  ImageRGB dst(tw, th);

  double sx = (double)src.width / tw;
  double sy = (double)src.height / th;

  // Precompute horizontal weights/indices per output column.
  std::vector<std::array<int, 8>> col_idx(tw);
  std::vector<std::array<double, 8>> col_w(tw);
  for (int x = 0; x < tw; x++) {
    double src_x = (x + 0.5) * sx - 0.5;
    image_utils_detail::lanczos4Weights(src_x, src.width, col_idx[x].data(), col_w[x].data());
  }

  for (int y = 0; y < th; y++) {
    double src_y = (y + 0.5) * sy - 0.5;
    int row_idx[8];
    double row_w[8];
    image_utils_detail::lanczos4Weights(src_y, src.height, row_idx, row_w);

    for (int x = 0; x < tw; x++) {
      double acc[3] = {0, 0, 0};
      for (int ky = 0; ky < 8; ky++) {
        for (int kx = 0; kx < 8; kx++) {
          double w = row_w[ky] * col_w[x][kx];
          for (int c = 0; c < 3; c++) acc[c] += src.at(row_idx[ky], col_idx[x][kx], c) * w;
        }
      }
      for (int c = 0; c < 3; c++) {
        dst.at(y, x, c) = (uint8_t)std::min(255.0, std::max(0.0, std::round(acc[c])));
      }
    }
  }
  return dst;
}

/**
 * Letterbox resize to imgsz x imgsz: Lanczos4 (upscale) or Area (downscale),
 * then pad with reflect-101.
 */
inline ImageRGB imageLetterboxReflect101(const ImageRGB& src, int imgsz) {
  if (src.empty() || imgsz <= 0)
    return {};

  int old_h = src.height;
  int old_w = src.width;
  double ratio = (double)imgsz / std::max(old_h, old_w);
  int scaled_h = (int)(old_h * ratio);
  int scaled_w = (int)(old_w * ratio);

  ImageRGB resized = (ratio > 1.0) ? resizeImageLanczos4(src, scaled_w, scaled_h)
                                   : resizeImageArea(src, scaled_w, scaled_h);

  int delta_w = imgsz - scaled_w;
  int delta_h = imgsz - scaled_h;
  int top = delta_h / 2;
  int left = delta_w / 2;

  ImageRGB out(imgsz, imgsz);
  for (int y = 0; y < imgsz; y++) {
    int sy = y - top;
    // reflect-101: mirror without repeating the edge pixel.
    if (scaled_h > 1) {
      while (sy < 0 || sy >= scaled_h) {
        if (sy < 0)
          sy = -sy;
        if (sy >= scaled_h)
          sy = 2 * (scaled_h - 1) - sy;
      }
    } else {
      sy = 0;
    }
    for (int x = 0; x < imgsz; x++) {
      int sx = x - left;
      if (scaled_w > 1) {
        while (sx < 0 || sx >= scaled_w) {
          if (sx < 0)
            sx = -sx;
          if (sx >= scaled_w)
            sx = 2 * (scaled_w - 1) - sx;
        }
      } else {
        sx = 0;
      }
      for (int c = 0; c < 3; c++) out.at(y, x, c) = resized.at(sy, sx, c);
    }
  }
  return out;
}

/**
 * HWC RGB uint8 -> CHW float, normalized to [0,1].
 */
inline std::vector<float> imageToChw(const ImageRGB& img) {
  int h = img.height, w = img.width;
  std::vector<float> out(3 * h * w);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) out[c * h * w + y * w + x] = img.at(y, x, c) / 255.0f;
  return out;
}

/**
 * HWC RGB uint8 -> CHW float, with mean/std normalization.
 */
inline std::vector<float> imageToChwNormalized(const ImageRGB& img, const float mean[3],
                                               const float std_val[3]) {
  int h = img.height, w = img.width;
  std::vector<float> out(3 * h * w);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        out[c * h * w + y * w + x] = (img.at(y, x, c) / 255.0f - mean[c]) / std_val[c];
  return out;
}

namespace {
inline std::string lowercasePathExtension(const std::string& path) {
  size_t dot = path.rfind('.');
  if (dot == std::string::npos)
    return "";
  std::string ext = path.substr(dot);
  for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
  return ext;
}
}  // namespace

/**
 * Load image from any supported format (JPEG, PNG, BMP, GIF, TGA, PSD, HDR, PIC, PNM).
 * Automatically detects format from file contents via stb_image.
 */
inline ImageRGB readImage(const std::string& path) {
  int w = 0, h = 0, channels = 0;
  uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &channels, 3);
  if (!pixels)
    return {};

  ImageRGB img(w, h, pixels);
  stbi_image_free(pixels);
  return img;
}

/**
 * Save image to file. Format is determined by file extension:
 *   .jpg / .jpeg  -> JPEG (quality 95)
 *   .png          -> PNG
 *   .bmp          -> BMP
 *   .tga          -> TGA
 * Returns false on unsupported extension or write failure.
 */
inline bool saveImage(const std::string& path, const ImageRGB& img) {
  if (img.empty())
    return false;

  std::string ext = lowercasePathExtension(path);
  int stride = img.width * 3;

  if (ext == ".jpg" || ext == ".jpeg") {
    return stbi_write_jpg(path.c_str(), img.width, img.height, 3, img.ptr(), 95) != 0;
  } else if (ext == ".png") {
    return stbi_write_png(path.c_str(), img.width, img.height, 3, img.ptr(), stride) != 0;
  } else if (ext == ".bmp") {
    return stbi_write_bmp(path.c_str(), img.width, img.height, 3, img.ptr()) != 0;
  } else if (ext == ".tga") {
    return stbi_write_tga(path.c_str(), img.width, img.height, 3, img.ptr()) != 0;
  }

  // Fallback: save as PNG
  return stbi_write_png(path.c_str(), img.width, img.height, 3, img.ptr(), stride) != 0;
}

#endif  // IMAGE_UTILS_H

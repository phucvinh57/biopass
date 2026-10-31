#include "resize.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// 4-lobe Lanczos kernel.
double lanczos4(double x) {
  constexpr double kA = 4.0;
  if (x == 0.0)
    return 1.0;
  if (x <= -kA || x >= kA)
    return 0.0;
  const double px = M_PI * x;
  return kA * std::sin(px) * std::sin(px / kA) / (px * px);
}

// Lanczos4 sample weights/indices for one output coordinate.
void lanczos4Weights(double src_pos, int src_size, int* idx, double* w) {
  int center = (int)std::floor(src_pos);
  for (int k = -3; k <= 4; k++) {
    int i = center + k;
    double dist = src_pos - i;
    w[k + 3] = lanczos4(dist);
    idx[k + 3] = std::min(std::max(i, 0), src_size - 1);
  }
}

}  // namespace

ImageRGB resizeImage(const ImageRGB& src, int tw, int th) {
  ImageRGB dst(tw, th);
  if (src.empty() || dst.empty())
    return {};
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

ImageRGB resizeImageArea(const ImageRGB& src, int tw, int th) {
  if (src.empty() || tw <= 0 || th <= 0)
    return {};
  ImageRGB dst(tw, th);
  if (dst.empty())
    return {};

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

ImageRGB resizeImageLanczos4(const ImageRGB& src, int tw, int th) {
  if (src.empty() || tw <= 0 || th <= 0)
    return {};
  ImageRGB dst(tw, th);
  if (dst.empty())
    return {};

  double sx = (double)src.width / tw;
  double sy = (double)src.height / th;

  std::vector<std::array<int, 8>> col_idx(tw);
  std::vector<std::array<double, 8>> col_w(tw);
  for (int x = 0; x < tw; x++) {
    double src_x = (x + 0.5) * sx - 0.5;
    lanczos4Weights(src_x, src.width, col_idx[x].data(), col_w[x].data());
  }

  for (int y = 0; y < th; y++) {
    double src_y = (y + 0.5) * sy - 0.5;
    int row_idx[8];
    double row_w[8];
    lanczos4Weights(src_y, src.height, row_idx, row_w);

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

ImageRGB imageLetterbox(const ImageRGB& src, int tw, int th, uint8_t pad_val) {
  ImageRGB out(tw, th);
  if (src.empty() || out.empty())
    return {};
  float scale = std::min((float)tw / src.width, (float)th / src.height);
  // A very thin source would round its short side to 0; keep at least 1 px.
  int nw = std::clamp((int)std::round(src.width * scale), 1, tw);
  int nh = std::clamp((int)std::round(src.height * scale), 1, th);
  ImageRGB resized = resizeImage(src, nw, nh);
  if (resized.empty())
    return {};

  std::memset(out.data.data(), pad_val, out.data.size());

  int dx = (tw - nw) / 2;
  int dy = (th - nh) / 2;
  for (int r = 0; r < nh; r++)
    std::memcpy(&out.data[((dy + r) * tw + dx) * 3], &resized.data[r * nw * 3], nw * 3);
  return out;
}

ImageRGB imageResizePad(const ImageRGB& src, int tw, int th) {
  return imageLetterbox(src, tw, th, 0);
}

int reflect101Index(int i, int n) {
  if (n <= 1)
    return 0;
  while (i < 0 || i >= n) {
    if (i < 0)
      i = -i;
    if (i >= n)
      i = 2 * (n - 1) - i;
  }
  return i;
}

ImageRGB imageLetterboxReflect101(const ImageRGB& src, int imgsz) {
  ImageRGB out(imgsz, imgsz);
  if (src.empty() || out.empty())
    return {};

  int old_h = src.height;
  int old_w = src.width;
  double ratio = (double)imgsz / std::max(old_h, old_w);
  // A very thin source would truncate its short side to 0; keep at least 1 px.
  int scaled_h = std::clamp((int)(old_h * ratio), 1, imgsz);
  int scaled_w = std::clamp((int)(old_w * ratio), 1, imgsz);

  ImageRGB resized = (ratio > 1.0) ? resizeImageLanczos4(src, scaled_w, scaled_h)
                                   : resizeImageArea(src, scaled_w, scaled_h);
  if (resized.empty())
    return {};

  int delta_w = imgsz - scaled_w;
  int delta_h = imgsz - scaled_h;
  int top = delta_h / 2;
  int left = delta_w / 2;

  for (int y = 0; y < imgsz; y++) {
    int sy = reflect101Index(y - top, scaled_h);
    for (int x = 0; x < imgsz; x++) {
      int sx = reflect101Index(x - left, scaled_w);
      for (int c = 0; c < 3; c++) out.at(y, x, c) = resized.at(sy, sx, c);
    }
  }
  return out;
}

ImageRGB imageSquareCropReflect101(const ImageRGB& src, int x1, int y1, int x2, int y2,
                                   float scale) {
  // Box maths in double: int corners near INT_MAX must not overflow.
  const double w = (double)x2 - x1;
  const double h = (double)y2 - y1;
  if (src.empty() || !(w > 0) || !(h > 0) || !(scale > 0))
    return {};

  const double max_dim = std::max(w, h);
  const double side = max_dim * (double)scale;
  // The output is side x side; past the image size limit it can't be built.
  if (!(side < ImageRGB::kMaxDim + 1.0))
    return {};
  const double fx = x1 + w / 2.0 - side / 2;
  const double fy = y1 + h / 2.0 - side / 2;
  // A box that far out has no pixels in the frame.
  if (fx <= -side || fy <= -side || fx >= src.width || fy >= src.height)
    return {};
  int x = (int)fx;
  int y = (int)fy;
  int size = (int)side;

  ImageRGB inside = src.crop(x, y, x + size, y + size);
  if (inside.empty() || size <= 0)
    return {};
  int off_x = std::max(0, x);
  int off_y = std::max(0, y);

  ImageRGB out(size, size);
  for (int r = 0; r < size; r++) {
    int sy = reflect101Index(y + r - off_y, inside.height);
    for (int c = 0; c < size; c++) {
      int sx = reflect101Index(x + c - off_x, inside.width);
      for (int ch = 0; ch < 3; ch++) out.at(r, c, ch) = inside.at(sy, sx, ch);
    }
  }
  return out;
}

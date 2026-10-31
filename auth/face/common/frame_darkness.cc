#include "frame_darkness.h"

#include <algorithm>
#include <cmath>

namespace biopass {

LumaStats measureLuma(const ImageRGB& image, int step) {
  LumaStats stats;
  if (image.empty()) {
    return stats;
  }
  step = std::max(1, step);

  double sum = 0.0;
  double sum_sq = 0.0;
  size_t count = 0;
  for (int y = 0; y < image.height; y += step) {
    for (int x = 0; x < image.width; x += step) {
      // Integer BT.601 luma: (77 R + 150 G + 29 B) / 256.
      const int luma =
          (77 * image.at(y, x, 0) + 150 * image.at(y, x, 1) + 29 * image.at(y, x, 2)) >> 8;
      sum += luma;
      sum_sq += static_cast<double>(luma) * luma;
      ++count;
    }
  }

  stats.mean = sum / static_cast<double>(count);
  const double variance = sum_sq / static_cast<double>(count) - stats.mean * stats.mean;
  stats.stddev = std::sqrt(std::max(0.0, variance));
  return stats;
}

bool looksCovered(const LumaStats& stats) {
  return stats.mean < kCoveredMaxMeanLuma && stats.stddev < kCoveredMaxLumaStdDev;
}

bool looksCovered(const ImageRGB& image) {
  if (image.empty()) {
    return false;
  }
  return looksCovered(measureLuma(image));
}

}  // namespace biopass

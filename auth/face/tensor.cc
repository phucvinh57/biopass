#include "tensor.h"

std::vector<float> imageToChw(const ImageRGB& img) {
  int h = img.height, w = img.width;
  std::vector<float> out(3 * h * w);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) out[c * h * w + y * w + x] = img.at(y, x, c) / 255.0f;
  return out;
}

std::vector<float> imageToChwNormalized(const ImageRGB& img, const float mean[3],
                                        const float std_val[3]) {
  int h = img.height, w = img.width;
  std::vector<float> out(3 * h * w);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        out[c * h * w + y * w + x] = (img.at(y, x, c) / 255.0f - mean[c]) / std_val[c];
  return out;
}

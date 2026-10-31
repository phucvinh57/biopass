// Sanity checks for the covered-lens / closed-shutter heuristic
// (face/common/frame_darkness.h). Exits non-zero on the first failure.
//
// Optionally pass image paths to print their luma stats and verdict, e.g. to
// tune thresholds against frames saved from a real camera:
//   frame_darkness_test shutter_closed.jpg dim_room.jpg

#include <cstdio>
#include <random>

#include "frame_darkness.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

// Uniform grey frame with Gaussian sensor noise.
ImageRGB noisyFrame(int base, double sigma, unsigned seed) {
  ImageRGB img(320, 240);
  std::mt19937 rng(seed);
  std::normal_distribution<double> noise(0.0, sigma);
  for (auto& px : img.data) {
    px = static_cast<uint8_t>(std::min(255.0, std::max(0.0, base + noise(rng))));
  }
  return img;
}

// Dark background with a brighter, screen-lit "face" blob in the middle.
ImageRGB dimRoomFrame() {
  ImageRGB img = noisyFrame(10, 3.0, 7);
  for (int y = 60; y < 180; ++y) {
    for (int x = 110; x < 210; ++x) {
      for (int c = 0; c < 3; ++c) {
        img.at(y, x, c) = 70;
      }
    }
  }
  return img;
}

}  // namespace

int main(int argc, char** argv) {
  using biopass::looksCovered;
  using biopass::measureLuma;

  expect(!looksCovered(ImageRGB()), "empty frame is not 'covered'");
  expect(looksCovered(ImageRGB(320, 240)), "pure black frame is covered");
  expect(looksCovered(noisyFrame(6, 3.0, 1)), "black + max-gain noise is covered");
  expect(looksCovered(noisyFrame(14, 5.0, 2)), "near-black, slightly noisy is covered");
  expect(!looksCovered(noisyFrame(45, 4.0, 3)), "uniformly dim (mean 45) is not covered");
  expect(!looksCovered(noisyFrame(10, 20.0, 4)), "dark but high-variance is not covered");
  expect(!looksCovered(dimRoomFrame()), "dark room with a lit face is not covered");
  expect(!looksCovered(noisyFrame(128, 30.0, 5)), "normally lit frame is not covered");

  const auto stats = measureLuma(ImageRGB(16, 16));
  expect(stats.mean == 0.0 && stats.stddev == 0.0, "black frame stats are zero");

  for (int i = 1; i < argc; ++i) {
    ImageRGB img = readImage(argv[i]);
    if (img.empty()) {
      std::printf("%s: could not read\n", argv[i]);
      g_failures++;
      continue;
    }
    const auto s = measureLuma(img);
    std::printf("%s: mean=%.1f stddev=%.1f covered=%s\n", argv[i], s.mean, s.stddev,
                looksCovered(s) ? "yes" : "no");
  }

  return g_failures == 0 ? 0 : 1;
}

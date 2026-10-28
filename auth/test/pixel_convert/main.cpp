// Byte-order checks for the packed RGB converters (face/common/pixel_convert.h)
// used for libcamera SoftISP output. Exits non-zero on any failure.

#include <cstdio>
#include <vector>

#include "pixel_convert.h"

namespace {

using biopass::PackedRgbLayout;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

struct Rgb {
  uint8_t r, g, b;
};

// 2x2 test image: red, green / blue, (10, 20, 30).
const Rgb kPixels[4] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {10, 20, 30}};
constexpr int kWidth = 2;
constexpr int kHeight = 2;

// Packs kPixels into `layout` with `pad` extra bytes per row (filled with
// 0xEE so a stride bug shows up as a wrong colour).
std::vector<uint8_t> pack(PackedRgbLayout layout, int pad, int* stride) {
  const bool four = layout == PackedRgbLayout::kBgrx32 || layout == PackedRgbLayout::kRgbx32;
  const bool red_first = layout == PackedRgbLayout::kRgb24 || layout == PackedRgbLayout::kRgbx32;
  const int bpp = four ? 4 : 3;
  *stride = kWidth * bpp + pad;
  std::vector<uint8_t> buf(static_cast<size_t>(*stride) * kHeight, 0xEE);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const Rgb& p = kPixels[y * kWidth + x];
      uint8_t* dst = buf.data() + y * *stride + x * bpp;
      dst[0] = red_first ? p.r : p.b;
      dst[1] = p.g;
      dst[2] = red_first ? p.b : p.r;
      if (four) {
        dst[3] = 0x77;  // X/A byte, must be ignored
      }
    }
  }
  return buf;
}

bool matches(const ImageRGB& img) {
  if (img.width != kWidth || img.height != kHeight) {
    return false;
  }
  for (int i = 0; i < kWidth * kHeight; ++i) {
    const uint8_t* px = img.ptr() + i * 3;
    if (px[0] != kPixels[i].r || px[1] != kPixels[i].g || px[2] != kPixels[i].b) {
      return false;
    }
  }
  return true;
}

void checkLayout(PackedRgbLayout layout, const char* name) {
  char what[128];
  for (int pad : {0, 8}) {
    int stride = 0;
    const auto buf = pack(layout, pad, &stride);
    ImageRGB out;
    const bool ok =
        biopass::packedRgbToRgb(buf.data(), buf.size(), kWidth, kHeight, stride, layout, out);
    std::snprintf(what, sizeof(what), "%s, row padding %d: R,G,B in order", name, pad);
    expect(ok && matches(out), what);
  }

  int stride = 0;
  const auto buf = pack(layout, 0, &stride);
  ImageRGB out;
  std::snprintf(what, sizeof(what), "%s: rejects a truncated buffer", name);
  expect(!biopass::packedRgbToRgb(buf.data(), buf.size() - 1, kWidth, kHeight, stride, layout, out),
         what);
}

// libcamera follows DRM fourcc naming: RGB888 is B,G,R in memory.
void checkLibcameraRgb888Bytes() {
  const uint8_t bgr_red[3] = {0, 0, 255};  // a red pixel as libcamera RGB888 stores it
  ImageRGB out;
  const bool ok =
      biopass::packedRgbToRgb(bgr_red, sizeof(bgr_red), 1, 1, 3, PackedRgbLayout::kBgr24, out);
  expect(ok && out.ptr()[0] == 255 && out.ptr()[1] == 0 && out.ptr()[2] == 0,
         "libcamera RGB888 bytes {0,0,255} decode to red");
}

}  // namespace

int main() {
  checkLayout(PackedRgbLayout::kBgr24, "RGB888 (B,G,R)");
  checkLayout(PackedRgbLayout::kRgb24, "BGR888 (R,G,B)");
  checkLayout(PackedRgbLayout::kBgrx32, "XRGB8888/ARGB8888 (B,G,R,X)");
  checkLayout(PackedRgbLayout::kRgbx32, "XBGR8888/ABGR8888 (R,G,B,X)");
  checkLibcameraRgb888Bytes();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

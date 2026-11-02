// Byte-order checks for the packed RGB converters (face/common/pixel_convert.h)
// used for libcamera SoftISP output, and the values the YUYV (BT.601 limited
// range), NV12/NV21/YUV420 and GREY converters produce. Exits non-zero on any failure.

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

bool pixelIs(const ImageRGB& img, int x, int y, Rgb want) {
  const uint8_t* px = img.ptr() + (static_cast<size_t>(y) * img.width + x) * 3;
  return px[0] == want.r && px[1] == want.g && px[2] == want.b;
}

// YUYV packs two pixels in four bytes: Y0 U Y1 V, sharing U and V.
void checkYuyv() {
  ImageRGB out;

  // Limited range: Y 16 is black and Y 235 white; one pair shares U and V.
  const uint8_t black_white[4] = {16, 128, 235, 128};
  expect(biopass::yuyvToRgb(black_white, sizeof(black_white), 2, 1, 4, out) && out.width == 2 &&
             out.height == 1 && pixelIs(out, 0, 0, {0, 0, 0}) &&
             pixelIs(out, 1, 0, {255, 255, 255}),
         "YUYV: Y 16 -> black, Y 235 -> white, chroma shared by the pair");

  // BT.601 red (Y 81, U 90, V 240) and green (Y 145, U 54, V 34).
  const uint8_t red[4] = {81, 90, 81, 240};
  expect(biopass::yuyvToRgb(red, sizeof(red), 2, 1, 4, out) && pixelIs(out, 0, 0, {255, 0, 0}) &&
             pixelIs(out, 1, 0, {255, 0, 0}),
         "YUYV: BT.601 red decodes to (255, 0, 0)");
  const uint8_t green[4] = {145, 54, 145, 34};
  expect(biopass::yuyvToRgb(green, sizeof(green), 2, 1, 4, out) && pixelIs(out, 0, 0, {0, 255, 1}),
         "YUYV: BT.601 green decodes to (0, 255, 1)");

  // Out-of-range results clamp to [0, 255].
  const uint8_t hot[4] = {255, 255, 255, 255};
  expect(biopass::yuyvToRgb(hot, sizeof(hot), 2, 1, 4, out) && pixelIs(out, 0, 0, {255, 125, 255}),
         "YUYV: Y/U/V 255 clamps to (255, 125, 255)");

  // Row padding (0xEE) is skipped: row 1 is read from `stride`, not 2*width.
  const uint8_t padded[16] = {16,  128, 16,  128, 0xEE, 0xEE, 0xEE, 0xEE,
                              235, 128, 235, 128, 0xEE, 0xEE, 0xEE, 0xEE};
  expect(biopass::yuyvToRgb(padded, sizeof(padded), 2, 2, 8, out) &&
             pixelIs(out, 1, 0, {0, 0, 0}) && pixelIs(out, 0, 1, {255, 255, 255}),
         "YUYV: row padding is skipped");

  // A stride below 2*width is read as packed rows.
  const uint8_t packed[8] = {235, 128, 235, 128, 16, 128, 16, 128};
  expect(biopass::yuyvToRgb(packed, sizeof(packed), 2, 2, 0, out) &&
             pixelIs(out, 0, 0, {255, 255, 255}) && pixelIs(out, 0, 1, {0, 0, 0}),
         "YUYV: stride 0 means packed rows");

  // Odd width: the trailing unpaired column is left black.
  const uint8_t odd[6] = {235, 128, 235, 128, 235, 128};
  expect(biopass::yuyvToRgb(odd, sizeof(odd), 3, 1, 6, out) && out.width == 3 &&
             pixelIs(out, 1, 0, {255, 255, 255}) && pixelIs(out, 2, 0, {0, 0, 0}),
         "YUYV: odd width leaves the last column black");

  // The last row needs only 2*width bytes, not a full stride.
  expect(biopass::yuyvToRgb(padded, 12, 2, 2, 8, out), "YUYV: last row may omit its padding");
  expect(!biopass::yuyvToRgb(padded, 11, 2, 2, 8, out), "YUYV: rejects a truncated buffer");
  expect(!biopass::yuyvToRgb(nullptr, 4, 2, 1, 4, out), "YUYV: rejects a null buffer");
  expect(!biopass::yuyvToRgb(black_white, sizeof(black_white), 0, 1, 4, out),
         "YUYV: rejects zero width");
}

// GREY copies each byte to R, G and B.
void checkGrey() {
  ImageRGB out;
  const uint8_t grey[6] = {0, 77, 0xEE, 255, 128, 0xEE};
  expect(biopass::greyToRgb(grey, sizeof(grey), 2, 2, 3, out) && out.width == 2 &&
             out.height == 2 && pixelIs(out, 0, 0, {0, 0, 0}) && pixelIs(out, 1, 0, {77, 77, 77}) &&
             pixelIs(out, 0, 1, {255, 255, 255}) && pixelIs(out, 1, 1, {128, 128, 128}),
         "GREY: value copied to R, G, B; row padding skipped");
  expect(biopass::greyToRgb(grey, 4, 2, 2, 0, out) && pixelIs(out, 0, 1, {0xEE, 0xEE, 0xEE}),
         "GREY: stride 0 means packed rows");
  expect(biopass::greyToRgb(grey, 5, 2, 2, 3, out), "GREY: last row may omit its padding");
  expect(!biopass::greyToRgb(grey, 4, 2, 2, 3, out), "GREY: rejects a truncated buffer");
  expect(!biopass::greyToRgb(nullptr, 6, 2, 2, 3, out), "GREY: rejects a null buffer");
  expect(!biopass::greyToRgb(grey, sizeof(grey), 2, 0, 3, out), "GREY: rejects zero height");
}

// 4x2 test frame for the 4:2:0 converters: the left 2x2 block is BT.601 red
// (Y 81, U 90, V 240), the right one green (Y 145, U 54, V 34).
constexpr Rgb kRed = {255, 0, 0};
constexpr Rgb kGreen = {0, 255, 1};

bool isRedGreen(const ImageRGB& img) {
  if (img.width != 4 || img.height != 2) {
    return false;
  }
  for (int y = 0; y < 2; ++y) {
    if (!pixelIs(img, 0, y, kRed) || !pixelIs(img, 1, y, kRed) || !pixelIs(img, 2, y, kGreen) ||
        !pixelIs(img, 3, y, kGreen)) {
      return false;
    }
  }
  return true;
}

// NV12: Y plane, then one row of interleaved U,V pairs per two Y rows.
void checkNv12() {
  ImageRGB out;
  const uint8_t y[8] = {81, 81, 145, 145, 81, 81, 145, 145};
  const uint8_t uv[4] = {90, 240, 54, 34};
  expect(
      biopass::nv12ToRgb(y, sizeof(y), 4, uv, sizeof(uv), 4, 4, 2, false, out) && isRedGreen(out),
      "NV12: known pixels, chroma shared per 2x2 block");

  // NV21 stores V before U.
  const uint8_t vu[4] = {240, 90, 34, 54};
  expect(biopass::nv12ToRgb(y, sizeof(y), 4, vu, sizeof(vu), 4, 4, 2, true, out) && isRedGreen(out),
         "NV21: V,U order decodes to the same pixels");
  expect(biopass::nv12ToRgb(y, sizeof(y), 4, vu, sizeof(vu), 4, 4, 2, false, out) &&
             !pixelIs(out, 0, 0, kRed),
         "NV12 read of NV21 bytes swaps the colours");

  // Strides wider than the row (0xEE padding) are skipped on both planes.
  const uint8_t y_pad[12] = {81, 81, 145, 145, 0xEE, 0xEE, 81, 81, 145, 145, 0xEE, 0xEE};
  const uint8_t uv_pad[6] = {90, 240, 54, 34, 0xEE, 0xEE};
  expect(biopass::nv12ToRgb(y_pad, sizeof(y_pad), 6, uv_pad, sizeof(uv_pad), 6, 4, 2, false, out) &&
             isRedGreen(out),
         "NV12: row padding is skipped");
  expect(biopass::nv12ToRgb(y_pad, 10, 6, uv_pad, 4, 6, 4, 2, false, out),
         "NV12: last rows may omit their padding");
  expect(
      biopass::nv12ToRgb(y, sizeof(y), 0, uv, sizeof(uv), 0, 4, 2, false, out) && isRedGreen(out),
      "NV12: stride 0 means packed rows");

  expect(!biopass::nv12ToRgb(y, sizeof(y) - 1, 4, uv, sizeof(uv), 4, 4, 2, false, out),
         "NV12: rejects a truncated Y plane");
  expect(!biopass::nv12ToRgb(y, sizeof(y), 4, uv, sizeof(uv) - 1, 4, 4, 2, false, out),
         "NV12: rejects a truncated UV plane");
  expect(!biopass::nv12ToRgb(y_pad, 9, 6, uv_pad, sizeof(uv_pad), 6, 4, 2, false, out),
         "NV12: rejects a Y plane too short for its stride");
  expect(!biopass::nv12ToRgb(nullptr, 8, 4, uv, sizeof(uv), 4, 4, 2, false, out),
         "NV12: rejects a null Y plane");
  expect(!biopass::nv12ToRgb(y, sizeof(y), 4, nullptr, 4, 4, 4, 2, false, out),
         "NV12: rejects a null UV plane");
  expect(!biopass::nv12ToRgb(y, sizeof(y), 4, uv, sizeof(uv), 4, 0, 2, false, out),
         "NV12: rejects zero width");

  // Odd sizes round the chroma plane up: 3x3 needs 2x2 chroma pairs.
  const uint8_t y_odd[9] = {235, 235, 235, 235, 235, 235, 235, 235, 235};
  const uint8_t uv_odd[8] = {128, 128, 128, 128, 128, 128, 128, 128};
  expect(biopass::nv12ToRgb(y_odd, sizeof(y_odd), 3, uv_odd, sizeof(uv_odd), 4, 3, 3, false, out) &&
             pixelIs(out, 2, 2, {255, 255, 255}),
         "NV12: odd width/height reads the rounded-up chroma");
  expect(!biopass::nv12ToRgb(y_odd, sizeof(y_odd), 3, uv_odd, 7, 4, 3, 3, false, out),
         "NV12: odd size rejects a chroma plane missing its last pair");
}

// YUV420 (I420): Y plane, then separate half-size U and V planes.
void checkYuv420() {
  ImageRGB out;
  const uint8_t y[8] = {81, 81, 145, 145, 81, 81, 145, 145};
  const uint8_t u[2] = {90, 54};
  const uint8_t v[2] = {240, 34};
  expect(biopass::yuv420ToRgb(y, sizeof(y), 4, u, sizeof(u), v, sizeof(v), 2, 4, 2, out) &&
             isRedGreen(out),
         "YUV420: known pixels, chroma shared per 2x2 block");

  // 4x4 with padded rows (Y stride 6, U/V stride 3); the bottom blocks swap.
  const uint8_t y_pad[24] = {81,  81,  145, 145, 0xEE, 0xEE, 81,  81,  145, 145, 0xEE, 0xEE,
                             145, 145, 81,  81,  0xEE, 0xEE, 145, 145, 81,  81,  0xEE, 0xEE};
  const uint8_t u_pad[6] = {90, 54, 0xEE, 54, 90, 0xEE};
  const uint8_t v_pad[6] = {240, 34, 0xEE, 34, 240, 0xEE};
  expect(biopass::yuv420ToRgb(y_pad, sizeof(y_pad), 6, u_pad, sizeof(u_pad), v_pad, sizeof(v_pad),
                              3, 4, 4, out) &&
             pixelIs(out, 0, 0, kRed) && pixelIs(out, 3, 1, kGreen) && pixelIs(out, 0, 2, kGreen) &&
             pixelIs(out, 3, 3, kRed),
         "YUV420: row padding is skipped on every plane");
  expect(biopass::yuv420ToRgb(y_pad, 22, 6, u_pad, 5, v_pad, 5, 3, 4, 4, out),
         "YUV420: last rows may omit their padding");

  expect(!biopass::yuv420ToRgb(y, sizeof(y) - 1, 4, u, sizeof(u), v, sizeof(v), 2, 4, 2, out),
         "YUV420: rejects a truncated Y plane");
  expect(!biopass::yuv420ToRgb(y, sizeof(y), 4, u, 1, v, sizeof(v), 2, 4, 2, out),
         "YUV420: rejects a truncated U plane");
  expect(!biopass::yuv420ToRgb(y, sizeof(y), 4, u, sizeof(u), v, 1, 2, 4, 2, out),
         "YUV420: rejects a truncated V plane");
  expect(
      !biopass::yuv420ToRgb(y_pad, sizeof(y_pad), 6, u_pad, 4, v_pad, sizeof(v_pad), 3, 4, 4, out),
      "YUV420: rejects a U plane too short for its stride");
  expect(!biopass::yuv420ToRgb(y, sizeof(y), 4, nullptr, 2, v, sizeof(v), 2, 4, 2, out),
         "YUV420: rejects a null U plane");
  expect(!biopass::yuv420ToRgb(y, sizeof(y), 4, u, sizeof(u), v, sizeof(v), 2, 4, 0, out),
         "YUV420: rejects zero height");
}

}  // namespace

int main() {
  checkLayout(PackedRgbLayout::kBgr24, "RGB888 (B,G,R)");
  checkLayout(PackedRgbLayout::kRgb24, "BGR888 (R,G,B)");
  checkLayout(PackedRgbLayout::kBgrx32, "XRGB8888/ARGB8888 (B,G,R,X)");
  checkLayout(PackedRgbLayout::kRgbx32, "XBGR8888/ABGR8888 (R,G,B,X)");
  checkLibcameraRgb888Bytes();
  checkYuyv();
  checkGrey();
  checkNv12();
  checkYuv420();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

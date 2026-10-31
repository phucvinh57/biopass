// Bounds checks for face/image_utils.h, the MJPEG decoder and detector box
// clamping: degenerate, oversized and out-of-frame inputs must give an empty
// result instead of reading or writing out of bounds. Valid images are pinned
// by output hashes. Exits non-zero on any failure.

#include <turbojpeg.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "image_utils.h"
#include "pixel_convert.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

// Deterministic non-uniform test pattern.
ImageRGB pattern(int w, int h) {
  ImageRGB img(w, h);
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      img.at(y, x, 0) = static_cast<uint8_t>(x * 7 + y * 3);
      img.at(y, x, 1) = static_cast<uint8_t>(x * 13 + y * 5 + 40);
      img.at(y, x, 2) = static_cast<uint8_t>((x ^ y) * 11);
    }
  }
  return img;
}

// FNV-1a over dims and pixels.
uint64_t hashImage(const ImageRGB& img) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint8_t b) {
    h ^= b;
    h *= 1099511628211ull;
  };
  for (int v : {img.width, img.height}) {
    for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>(v >> (8 * i)));
  }
  for (uint8_t b : img.data) mix(b);
  return h;
}

bool consistent(const ImageRGB& img) {
  return img.data.size() == static_cast<size_t>(img.width) * img.height * 3;
}

bool isEmptyImage(const ImageRGB& img) { return img.empty() && img.width == 0 && img.height == 0; }

// True if any pixel differs from the pad colour.
bool hasContent(const ImageRGB& img, uint8_t pad) {
  for (uint8_t b : img.data) {
    if (b != pad) {
      return true;
    }
  }
  return false;
}

void checkGolden() {
  // Hashes recorded from the pre-guard implementation: valid inputs must not change.
  const ImageRGB src = pattern(64, 48);
  const ImageRGB face = pattern(30, 41);
  struct Case {
    const char* what;
    ImageRGB img;
    uint64_t hash;
  } cases[] = {
      {"golden: imageLetterbox 64x48 -> 40x40", imageLetterbox(src, 40, 40), 0x8590d4e6a2893002ull},
      {"golden: imageResizePad 30x41 -> 112x112", imageResizePad(face, 112, 112),
       0x713ce774a668f357ull},
      {"golden: resizeImage 64x48 -> 20x30", resizeImage(src, 20, 30), 0x1af6e3ee46724466ull},
      {"golden: imageLetterboxReflect101 30x41 -> 80 (up)", imageLetterboxReflect101(face, 80),
       0xea6c1c269fde3076ull},
      {"golden: imageLetterboxReflect101 64x48 -> 32 (down)", imageLetterboxReflect101(src, 32),
       0xdce03c1789f79863ull},
      {"golden: imageSquareCropReflect101 box partly outside",
       imageSquareCropReflect101(src, 40, 10, 70, 50, 2.7f), 0x34d3527087973453ull},
      {"golden: crop 10,5,30,20", src.crop(10, 5, 30, 20), 0xc849b1177231fee4ull},
  };
  for (const auto& c : cases) {
    const uint64_t h = hashImage(c.img);
    if (c.hash == 0) {
      std::printf("  %s = 0x%016llxull (%dx%d)\n", c.what, static_cast<unsigned long long>(h),
                  c.img.width, c.img.height);
    }
    expect(h == c.hash, c.what);
  }
}

void checkConstruction() {
  expect(ImageRGB::byteSize(100000, 100000) == 0, "byteSize rejects 100000x100000");
  expect(ImageRGB::byteSize(16385, 1) == 0, "byteSize rejects width > 16384");
  expect(ImageRGB::byteSize(1, 16385) == 0, "byteSize rejects height > 16384");
  expect(ImageRGB::byteSize(16384, 2) == 16384u * 2 * 3, "byteSize accepts width == 16384");
  expect(isEmptyImage(ImageRGB(16385, 1)), "ImageRGB(16385, 1) is empty with zero dims");
  expect(isEmptyImage(ImageRGB(0, 10)), "ImageRGB(0, 10) is empty with zero dims");
  expect(isEmptyImage(ImageRGB(10, -1)), "ImageRGB(10, -1) is empty with zero dims");
}

void checkLetterbox() {
  for (int len : {1000, 10000}) {
    char what[128];
    const ImageRGB wide = pattern(len, 1);
    ImageRGB out = imageLetterbox(wide, 64, 64);
    std::snprintf(what, sizeof(what), "imageLetterbox %dx1 -> 64x64 keeps a row of pixels", len);
    expect(out.width == 64 && out.height == 64 && consistent(out) && hasContent(out, 114), what);

    const ImageRGB tall = pattern(1, len);
    out = imageLetterbox(tall, 64, 64);
    std::snprintf(what, sizeof(what), "imageLetterbox 1x%d -> 64x64 keeps a column of pixels", len);
    expect(out.width == 64 && out.height == 64 && consistent(out) && hasContent(out, 114), what);
  }
  expect(isEmptyImage(imageLetterbox(ImageRGB(), 64, 64)), "imageLetterbox of empty is empty");
  expect(isEmptyImage(imageLetterbox(pattern(8, 8), 0, 64)), "imageLetterbox to width 0 is empty");
  expect(isEmptyImage(imageLetterbox(pattern(8, 8), 64, -5)),
         "imageLetterbox to negative height is empty");
  expect(isEmptyImage(resizeImage(pattern(8, 8), 0, 4)), "resizeImage to width 0 is empty");

  for (int len : {1000, 10000}) {
    char what[128];
    ImageRGB out = imageLetterboxReflect101(pattern(len, 1), 32);
    std::snprintf(what, sizeof(what), "imageLetterboxReflect101 %dx1 -> 32x32", len);
    expect(out.width == 32 && out.height == 32 && consistent(out), what);
    out = imageLetterboxReflect101(pattern(1, len), 32);
    std::snprintf(what, sizeof(what), "imageLetterboxReflect101 1x%d -> 32x32", len);
    expect(out.width == 32 && out.height == 32 && consistent(out), what);
  }
}

void checkCrop() {
  const ImageRGB src = pattern(20, 10);
  ImageRGB c = src.crop(-5, -5, 5, 5);
  expect(c.width == 5 && c.height == 5 && consistent(c) && c.at(0, 0, 0) == src.at(0, 0, 0),
         "crop partly outside (top-left) clamps to the image");
  c = src.crop(15, 8, 40, 40);
  expect(c.width == 5 && c.height == 2 && c.at(1, 4, 2) == src.at(9, 19, 2),
         "crop partly outside (bottom-right) clamps to the image");
  expect(isEmptyImage(src.crop(25, 0, 30, 5)), "crop fully right of the image is empty");
  expect(isEmptyImage(src.crop(-10, -10, -1, -1)), "crop fully above-left is empty");
  expect(isEmptyImage(src.crop(5, 5, 5, 9)), "zero-width crop is empty");
  expect(isEmptyImage(src.crop(8, 8, 3, 3)), "inverted crop is empty");
  expect(isEmptyImage(ImageRGB().crop(0, 0, 5, 5)), "crop of empty is empty");

  ImageRGB sq = imageSquareCropReflect101(src, -8, -8, -2, -2, 2.7f);
  expect(sq.width == 16 && sq.height == 16 && consistent(sq),
         "square crop of a box outside but near the frame reflects into it");
  expect(isEmptyImage(imageSquareCropReflect101(src, 100, 100, 110, 110, 2.7f)),
         "square crop far outside the frame is empty");
  expect(isEmptyImage(imageSquareCropReflect101(src, 5, 5, 5, 8, 2.7f)),
         "square crop with a zero-area box is empty");

  expect(reflect101Index(-1, 5) == 1 && reflect101Index(5, 5) == 3 && reflect101Index(0, 0) == 0 &&
             reflect101Index(7, 1) == 0,
         "reflect101Index mirrors and handles n <= 1");
}

// Last: before the guard this tried a multi-exabyte allocation.
void checkHugeBox() {
  expect(isEmptyImage(imageSquareCropReflect101(pattern(20, 10), 0, 0, 1 << 29, 1 << 29, 2.7f)),
         "square crop with a huge box is empty");
}

// Encodes a w x h test pattern as a baseline JPEG with libjpeg-turbo.
std::vector<uint8_t> encodeJpeg(int w, int h) {
  const ImageRGB img = pattern(w, h);
  tjhandle handle = tjInitCompress();
  unsigned char* buf = nullptr;
  unsigned long size = 0;
  const int rc = tjCompress2(handle, img.ptr(), w, 0, h, TJPF_RGB, &buf, &size, TJSAMP_444, 90, 0);
  std::vector<uint8_t> out;
  if (rc == 0) {
    out.assign(buf, buf + size);
  }
  tjFree(buf);
  tjDestroy(handle);
  return out;
}

// Rewrites the frame size in the JPEG's SOF0 header (the data is left as is).
bool patchJpegSize(std::vector<uint8_t>& jpeg, int w, int h) {
  for (size_t i = 2; i + 8 < jpeg.size(); ++i) {
    if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xC0) {
      jpeg[i + 5] = static_cast<uint8_t>(h >> 8);
      jpeg[i + 6] = static_cast<uint8_t>(h);
      jpeg[i + 7] = static_cast<uint8_t>(w >> 8);
      jpeg[i + 8] = static_cast<uint8_t>(w);
      return true;
    }
  }
  return false;
}

void checkMjpeg() {
  const std::vector<uint8_t> jpeg = encodeJpeg(16, 8);
  expect(!jpeg.empty(), "test JPEG encodes");

  ImageRGB out;
  expect(biopass::mjpegToRgb(jpeg.data(), jpeg.size(), 16, 8, out) && out.width == 16 &&
             out.height == 8 && consistent(out),
         "MJPEG of the configured size decodes");

  out = pattern(2, 2);
  expect(!biopass::mjpegToRgb(jpeg.data(), jpeg.size(), 8, 8, out) && out.empty(),
         "MJPEG wider than the configured size is rejected");
  out = pattern(2, 2);
  expect(!biopass::mjpegToRgb(jpeg.data(), jpeg.size(), 32, 16, out) && out.empty(),
         "MJPEG smaller than the configured size is rejected");

  std::vector<uint8_t> huge = jpeg;
  expect(patchJpegSize(huge, 40000, 40000), "test JPEG has a SOF0 header");
  out = pattern(2, 2);
  expect(!biopass::mjpegToRgb(huge.data(), huge.size(), 40000, 40000, out) && out.empty(),
         "MJPEG over the image size limit is rejected");
  out = pattern(2, 2);
  expect(!biopass::mjpegToRgb(huge.data(), huge.size(), 16, 8, out) && out.empty(),
         "MJPEG header 40000x40000 on a 16x8 stream is rejected");

  expect(!biopass::mjpegToRgb(jpeg.data(), 1, 16, 8, out), "truncated MJPEG is rejected");
}

// Raw converters must refuse a frame size ImageRGB can't hold instead of
// writing into an empty image.
void checkRawConvertLimits() {
  const int w = ImageRGB::kMaxDim + 2;
  const std::vector<uint8_t> row(static_cast<size_t>(w) * 4, 0x80);
  ImageRGB out;
  expect(!biopass::yuyvToRgb(row.data(), row.size(), w, 1, w * 2, out),
         "yuyvToRgb rejects width over the limit");
  expect(!biopass::greyToRgb(row.data(), row.size(), w, 1, w, out),
         "greyToRgb rejects width over the limit");
  expect(!biopass::packedRgbToRgb(row.data(), row.size(), w, 1, w * 3,
                                  biopass::PackedRgbLayout::kRgb24, out),
         "packedRgbToRgb rejects width over the limit");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep output if a check crashes
  checkGolden();
  checkConstruction();
  checkCrop();
  checkLetterbox();
  checkHugeBox();
  checkMjpeg();
  checkRawConvertLimits();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

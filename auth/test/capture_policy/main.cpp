// Which pixel format and frame size a camera session asks for, how IR
// samples are ranked, and what counts as a monochrome (IR) frame
// (face/common/capture_policy.h). Exits non-zero on any failure.

#include <libcamera/formats.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <vector>

#include "capture_policy.h"

namespace {

using biopass::CameraCaptureFormat;
using biopass::irFrameScore;
using biopass::isMonochrome;
using biopass::meanChroma;
using biopass::meanLuma;
using biopass::pickCaptureSize;
using biopass::pickCaptureSizeInRange;
using biopass::pixelFormatPreference;
using libcamera::PixelFormat;
using libcamera::Size;
using libcamera::SizeRange;
namespace formats = libcamera::formats;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

bool picks(const std::vector<Size>& sizes, const PixelFormat& format, const Size& want) {
  const std::optional<Size> got = pickCaptureSize(sizes, format);
  return got && *got == want;
}

void testPreference() {
  const std::vector<PixelFormat> colour = {formats::MJPEG,    formats::YUYV,     formats::NV12,
                                           formats::NV21,     formats::YUV420,   formats::BGR888,
                                           formats::RGB888,   formats::XBGR8888, formats::XRGB8888,
                                           formats::ABGR8888, formats::ARGB8888, formats::R8};
  expect(pixelFormatPreference(CameraCaptureFormat::Default) == colour,
         "colour: MJPEG, YUYV, NV12, NV21, YUV420, packed RGB (BGR888 first), then R8");

  const std::vector<PixelFormat> ir = {formats::R8,       formats::NV12,     formats::NV21,
                                       formats::YUV420,   formats::YUYV,     formats::MJPEG,
                                       formats::BGR888,   formats::RGB888,   formats::XBGR8888,
                                       formats::XRGB8888, formats::ABGR8888, formats::ARGB8888};
  expect(pixelFormatPreference(CameraCaptureFormat::V4L2Grey) == ir,
         "ir: R8, NV12, NV21, YUV420, YUYV, MJPEG, then packed RGB (BGR888 first)");
}

void testCaptureSize() {
  expect(!pickCaptureSize({}, formats::YUYV), "no discrete sizes: nullopt");

  const std::vector<Size> common = {{640, 480}, {1280, 720}, {1920, 1080}};
  expect(picks(common, formats::MJPEG, {1280, 720}), "MJPEG capped at 1280x720");
  expect(picks(common, formats::YUYV, {640, 480}), "uncompressed capped at 640x480");
  expect(picks(common, formats::R8, {640, 480}), "R8 uses the uncompressed cap");
  expect(picks(common, formats::NV12, {640, 480}), "NV12 uses the uncompressed cap");

  expect(picks({{640, 360}, {640, 480}}, formats::YUYV, {640, 480}), "largest area within cap");
  expect(picks({{1280, 720}, {1280, 800}}, formats::MJPEG, {1280, 720}),
         "taller than the cap counts as above it");

  expect(picks({{320, 240}, {1920, 1080}, {1280, 720}}, formats::YUYV, {1280, 720}),
         "too narrow within cap: smallest above it instead");
  expect(picks({{320, 240}}, formats::YUYV, {320, 240}), "too narrow but nothing larger: keep it");
  expect(picks({{1920, 1080}, {1280, 720}}, formats::YUYV, {1280, 720}),
         "nothing within cap: smallest above it");
}

bool picksInRange(const SizeRange& range, const PixelFormat& format, const Size& want) {
  const std::optional<Size> got = pickCaptureSizeInRange(range, format);
  return got && *got == want;
}

void testCaptureSizeInRange() {
  const SizeRange loopback({1, 1}, {8192, 8192}, 1, 1);
  expect(picksInRange(loopback, formats::YUYV, {640, 480}), "huge range: uncompressed cap");
  expect(picksInRange(loopback, formats::MJPEG, {1280, 720}), "huge range: MJPEG cap");

  expect(picksInRange({{160, 120}, {640, 480}, 16, 8}, formats::YUYV, {640, 480}),
         "range ending at the cap: the cap");
  expect(picksInRange({{320, 240}, {352, 288}, 16, 16}, formats::YUYV, {352, 288}),
         "range below the cap: its max");
  expect(picksInRange({{1920, 1080}, {3840, 2160}, 8, 8}, formats::YUYV, {1920, 1080}),
         "range above the cap: its min");
  expect(picksInRange({{100, 100}, {2000, 2000}, 300, 300}, formats::YUYV, {700, 400}),
         "coarse grid: width stepped up to >= 640, height rounded down");
  expect(picksInRange({{100, 100}, {500, 2000}, 300, 300}, formats::YUYV, {400, 400}),
         "coarse grid, no step to 640: stays narrow");
  expect(!pickCaptureSizeInRange({{0, 0}, {8192, 8192}, 1, 1}, formats::YUYV), "zero min: nullopt");
  expect(!pickCaptureSizeInRange({{640, 480}, {320, 240}, 1, 1}, formats::YUYV),
         "min above max: nullopt");
  expect(picksInRange({{16, 16}, {8192, 8192}, 0, 0}, formats::YUYV, {640, 480}),
         "zero step treated as 1");
}

void testIrScore() {
  expect(irFrameScore(100.0) > irFrameScore(50.0), "brighter well-exposed frame wins");
  expect(irFrameScore(220.0) > irFrameScore(221.0), "220 is still well-exposed");
  expect(irFrameScore(10.0) > irFrameScore(230.0), "any well-exposed beats over-exposed");
  expect(irFrameScore(230.0) > irFrameScore(250.0), "least washed-out over-exposed wins");
  expect(irFrameScore(0.0) == 0.0, "black frame scores 0");
}

void testMeanLuma() {
  expect(meanLuma(ImageRGB{}) == 0.0, "empty image: 0");
  ImageRGB grey(8, 2);
  std::fill(grey.data.begin(), grey.data.end(), 90);
  expect(meanLuma(grey) == 90.0, "uniform grey: its level");
  ImageRGB bad(8, 2);
  bad.data.resize(3);
  expect(meanLuma(bad) == 0.0, "short buffer: 0");
}

// 8x8 frame of one colour.
ImageRGB filled(uint8_t r, uint8_t g, uint8_t b) {
  ImageRGB image(8, 8);
  for (size_t i = 0; i < image.data.size(); i += 3) {
    image.data[i] = r;
    image.data[i + 1] = g;
    image.data[i + 2] = b;
  }
  return image;
}

void testMonochrome() {
  expect(isMonochrome(filled(0, 0, 0)), "black is monochrome");
  expect(isMonochrome(filled(128, 128, 128)), "grey (R8/IR decode) is monochrome");
  expect(isMonochrome(filled(130, 128, 127)), "near-grey (chroma noise 3) is monochrome");
  expect(isMonochrome(filled(131, 128, 125)), "chroma 6 is still monochrome");
  expect(meanChroma(filled(131, 128, 125)) == 6.0, "meanChroma reports |R-G| + |G-B|");
  expect(meanChroma(ImageRGB{}) < 0.0, "meanChroma of an empty frame is negative");
  expect(!isMonochrome(filled(132, 128, 125)), "chroma 7 is colour");
  expect(!isMonochrome(filled(200, 150, 120)), "skin tone is colour");
  expect(!isMonochrome(filled(255, 0, 0)), "red is colour");

  // A colour patch in a grey frame: 16 of 64 pixels at chroma 255+255.
  ImageRGB patch = filled(128, 128, 128);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      patch.at(y, x, 0) = 255;
      patch.at(y, x, 1) = 0;
      patch.at(y, x, 2) = 255;
    }
  }
  expect(!isMonochrome(patch), "grey frame with a colour patch is colour");
  expect(!isMonochrome(ImageRGB()), "empty frame is not monochrome");
}

}  // namespace

int main() {
  testPreference();
  testCaptureSize();
  testCaptureSizeInRange();
  testIrScore();
  testMeanLuma();
  testMonochrome();
  return g_failures == 0 ? 0 : 1;
}

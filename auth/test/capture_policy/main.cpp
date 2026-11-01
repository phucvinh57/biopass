// Which pixel format and frame size a camera session asks for, and how IR
// samples are ranked (face/common/capture_policy.h). Exits non-zero on any
// failure.

#include <libcamera/formats.h>

#include <cstdio>
#include <optional>
#include <vector>

#include "capture_policy.h"

namespace {

using biopass::CameraCaptureFormat;
using biopass::irFrameScore;
using biopass::pickCaptureSize;
using biopass::pixelFormatPreference;
using libcamera::PixelFormat;
using libcamera::Size;
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
  const std::vector<PixelFormat> colour = {formats::MJPEG,    formats::YUYV,     formats::BGR888,
                                           formats::RGB888,   formats::XBGR8888, formats::XRGB8888,
                                           formats::ABGR8888, formats::ARGB8888, formats::R8};
  expect(pixelFormatPreference(CameraCaptureFormat::Default) == colour,
         "colour: MJPEG, YUYV, packed RGB (BGR888 first), then R8");

  const std::vector<PixelFormat> ir = {formats::R8,       formats::YUYV,     formats::MJPEG,
                                       formats::BGR888,   formats::RGB888,   formats::XBGR8888,
                                       formats::XRGB8888, formats::ABGR8888, formats::ARGB8888};
  expect(pixelFormatPreference(CameraCaptureFormat::V4L2Grey) == ir,
         "ir: R8, YUYV, MJPEG, then packed RGB (BGR888 first)");
}

void testCaptureSize() {
  expect(!pickCaptureSize({}, formats::YUYV), "no discrete sizes: nullopt");

  const std::vector<Size> common = {{640, 480}, {1280, 720}, {1920, 1080}};
  expect(picks(common, formats::MJPEG, {1280, 720}), "MJPEG capped at 1280x720");
  expect(picks(common, formats::YUYV, {640, 480}), "uncompressed capped at 640x480");
  expect(picks(common, formats::R8, {640, 480}), "R8 uses the uncompressed cap");

  expect(picks({{640, 360}, {640, 480}}, formats::YUYV, {640, 480}), "largest area within cap");
  expect(picks({{1280, 720}, {1280, 800}}, formats::MJPEG, {1280, 720}),
         "taller than the cap counts as above it");

  expect(picks({{320, 240}, {1920, 1080}, {1280, 720}}, formats::YUYV, {1280, 720}),
         "too narrow within cap: smallest above it instead");
  expect(picks({{320, 240}}, formats::YUYV, {320, 240}), "too narrow but nothing larger: keep it");
  expect(picks({{1920, 1080}, {1280, 720}}, formats::YUYV, {1280, 720}),
         "nothing within cap: smallest above it");
}

void testIrScore() {
  expect(irFrameScore(100.0) > irFrameScore(50.0), "brighter well-exposed frame wins");
  expect(irFrameScore(220.0) > irFrameScore(221.0), "220 is still well-exposed");
  expect(irFrameScore(10.0) > irFrameScore(230.0), "any well-exposed beats over-exposed");
  expect(irFrameScore(230.0) > irFrameScore(250.0), "least washed-out over-exposed wins");
  expect(irFrameScore(0.0) == 0.0, "black frame scores 0");
}

}  // namespace

int main() {
  testPreference();
  testCaptureSize();
  testIrScore();
  return g_failures == 0 ? 0 : 1;
}

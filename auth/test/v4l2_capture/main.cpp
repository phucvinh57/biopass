// V4L2 fourcc mapping and format choice for the direct V4L2 fallback
// (face/common/v4l2_capture.h). Exits non-zero on any failure.

#include <libcamera/formats.h>
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include "v4l2_capture.h"

int testNegotiation();

namespace {

using biopass::CameraCaptureFormat;
using biopass::isVideoCharDevice;
using biopass::pickV4l2Format;
using biopass::pixelFormatFromV4l2;
using biopass::v4l2BytesPerLine;
namespace formats = libcamera::formats;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

bool maps(uint32_t fourcc, const libcamera::PixelFormat& want) {
  const auto got = pixelFormatFromV4l2(fourcc);
  return got && *got == want;
}

bool picks(const std::vector<uint32_t>& available, CameraCaptureFormat format, uint32_t want) {
  const std::optional<uint32_t> got = pickV4l2Format(available, format);
  return got && *got == want;
}

void testMapping() {
  expect(maps(V4L2_PIX_FMT_YUYV, formats::YUYV), "YUYV -> YUYV");
  expect(maps(V4L2_PIX_FMT_MJPEG, formats::MJPEG), "MJPG -> MJPEG");
  expect(maps(V4L2_PIX_FMT_GREY, formats::R8), "GREY -> R8");
  expect(maps(V4L2_PIX_FMT_RGB24, formats::BGR888), "RGB3 (bytes R,G,B) -> BGR888");
  expect(maps(V4L2_PIX_FMT_BGR24, formats::RGB888), "BGR3 (bytes B,G,R) -> RGB888");
  expect(!pixelFormatFromV4l2(V4L2_PIX_FMT_NV12), "NV12 unsupported");
  expect(!pixelFormatFromV4l2(V4L2_PIX_FMT_YUV420), "YU12 unsupported");
  expect(!pixelFormatFromV4l2(0), "unknown fourcc unsupported");
}

void testSelection() {
  const auto colour = CameraCaptureFormat::Default;
  const auto ir = CameraCaptureFormat::V4L2Grey;
  const auto direct_ir = CameraCaptureFormat::DirectV4L2Grey;

  expect(picks({V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_GREY}, direct_ir, V4L2_PIX_FMT_GREY),
         "direct IR: native GREY is selected");
  expect(!pickV4l2Format({V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV}, direct_ir),
         "direct IR: reject colour-only formats");

  expect(picks({V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG}, colour, V4L2_PIX_FMT_MJPEG),
         "colour: MJPG over YUYV");
  expect(picks({V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_GREY}, ir, V4L2_PIX_FMT_GREY),
         "ir: GREY over YUYV");
  expect(picks({V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV}, ir, V4L2_PIX_FMT_YUYV),
         "ir without GREY: YUYV over MJPG");
  expect(picks({V4L2_PIX_FMT_GREY}, colour, V4L2_PIX_FMT_GREY), "colour: GREY as last resort");
  expect(picks({V4L2_PIX_FMT_GREY, V4L2_PIX_FMT_RGB24}, colour, V4L2_PIX_FMT_RGB24),
         "colour: packed RGB over GREY");
  expect(picks({V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_RGB24}, colour, V4L2_PIX_FMT_RGB24),
         "colour: RGB3 (libcamera BGR888) first among packed RGB");
  expect(picks({V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YUYV}, colour, V4L2_PIX_FMT_YUYV),
         "unsupported fourccs are skipped");
  expect(!pickV4l2Format({V4L2_PIX_FMT_NV12}, colour), "only unsupported: nullopt");
  expect(!pickV4l2Format({}, ir), "nothing available: nullopt");
}

void testBytesPerLine() {
  expect(v4l2BytesPerLine(V4L2_PIX_FMT_YUYV, 640, 1344) == 1344, "driver stride kept");
  expect(v4l2BytesPerLine(V4L2_PIX_FMT_YUYV, 640, 0) == 1280, "YUYV fallback: 2 bytes/px");
  expect(v4l2BytesPerLine(V4L2_PIX_FMT_GREY, 640, 0) == 640, "GREY fallback: 1 byte/px");
  expect(v4l2BytesPerLine(V4L2_PIX_FMT_RGB24, 640, 0) == 1920, "RGB3 fallback: 3 bytes/px");
  expect(v4l2BytesPerLine(V4L2_PIX_FMT_BGR24, 640, 0) == 1920, "BGR3 fallback: 3 bytes/px");
  expect(v4l2BytesPerLine(V4L2_PIX_FMT_MJPEG, 640, 0) == 0, "MJPG: no stride");
}

void testVideoCharDevice() {
  expect(isVideoCharDevice(S_IFCHR | 0660, makedev(81, 0)), "char device, major 81: accepted");
  expect(!isVideoCharDevice(S_IFBLK | 0660, makedev(81, 0)), "block device, major 81: rejected");
  expect(!isVideoCharDevice(S_IFCHR | 0600, makedev(10, 130)), "watchdog (10:130): rejected");
  expect(!isVideoCharDevice(S_IFREG | 0644, makedev(81, 0)), "regular file: rejected");

  struct stat dev_null{};
  expect(
      stat("/dev/null", &dev_null) == 0 && !isVideoCharDevice(dev_null.st_mode, dev_null.st_rdev),
      "/dev/null: rejected");
}

}  // namespace

int main() {
  testMapping();
  testSelection();
  testBytesPerLine();
  testVideoCharDevice();
  g_failures += testNegotiation();
  return g_failures == 0 ? 0 : 1;
}

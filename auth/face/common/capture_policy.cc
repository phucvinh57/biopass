#include "capture_policy.h"

#include <libcamera/formats.h>

#include <cstdint>

namespace biopass {

namespace {

// Detection letterboxes to 640px and recognition uses 112x112 crops, so
// anything beyond 720p only costs USB bandwidth. Uncompressed streams are
// capped harder: 1280x720 YUYV (~1.8 MB/frame) runs at a few fps on USB 2.0,
// or truncates once the IR stream shares the bus.
const libcamera::Size kMaxCompressedCaptureSize(1280, 720);
const libcamera::Size kMaxUncompressedCaptureSize(640, 480);
// Narrower frames leave too few pixels for a face at arm's length; prefer a
// size above the cap over one below it.
constexpr unsigned int kMinCaptureWidth = 640;

// Some emitters also produce washed-out (near all-white) frames, which hold no
// more face detail than dark ones. Above this mean, a frame counts as
// over-exposed and only wins when every sample is.
constexpr double kIrOverexposedMeanLuma = 220.0;

uint64_t area(const libcamera::Size& size) {
  return static_cast<uint64_t>(size.width) * static_cast<uint64_t>(size.height);
}

}  // namespace

std::vector<libcamera::PixelFormat> pixelFormatPreference(CameraCaptureFormat format) {
  // SoftISP outputs only packed RGB. BGR888 first since its bytes are
  // already in RGB order, then the other 24-bit and 32-bit variants.
  const std::vector<libcamera::PixelFormat> packed_rgb = {
      libcamera::formats::BGR888,   libcamera::formats::RGB888,   libcamera::formats::XBGR8888,
      libcamera::formats::XRGB8888, libcamera::formats::ABGR8888, libcamera::formats::ARGB8888};

  std::vector<libcamera::PixelFormat> preference;
  if (format == CameraCaptureFormat::V4L2Grey) {
    // Prefer a true grey stream, but some laptop IR sensors (e.g. Windows
    // Hello cameras) only expose the IR stream as YUYV/MJPEG/RGB; decode
    // those instead of failing outright.
    preference = {libcamera::formats::R8, libcamera::formats::YUYV, libcamera::formats::MJPEG};
    preference.insert(preference.end(), packed_rgb.begin(), packed_rgb.end());
  } else {
    // Prefer MJPEG for the colour stream: an uncompressed YUYV stream at a
    // webcam's maximum size starves a USB 2.0 bus (slow frame rates or
    // truncated frames, worse when the IR stream is open at the same time).
    preference = {libcamera::formats::MJPEG, libcamera::formats::YUYV};
    preference.insert(preference.end(), packed_rgb.begin(), packed_rgb.end());
    preference.push_back(libcamera::formats::R8);
  }
  return preference;
}

std::optional<libcamera::Size> pickCaptureSize(const std::vector<libcamera::Size>& sizes,
                                               const libcamera::PixelFormat& pixel_format) {
  const libcamera::Size& max_size = pixel_format == libcamera::formats::MJPEG
                                        ? kMaxCompressedCaptureSize
                                        : kMaxUncompressedCaptureSize;

  std::optional<libcamera::Size> best_within;
  std::optional<libcamera::Size> smallest_above;
  for (const auto& size : sizes) {
    if (size.width <= max_size.width && size.height <= max_size.height) {
      if (!best_within || area(size) > area(*best_within)) {
        best_within = size;
      }
    } else if (!smallest_above || area(size) < area(*smallest_above)) {
      smallest_above = size;
    }
  }

  if (best_within && (best_within->width >= kMinCaptureWidth || !smallest_above)) {
    return best_within;
  }
  return smallest_above;
}

double irFrameScore(double mean) { return mean <= kIrOverexposedMeanLuma ? mean : -mean; }

}  // namespace biopass

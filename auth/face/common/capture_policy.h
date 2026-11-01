#pragma once

// What a camera session asks the camera for, and which IR sample it keeps.
// Pure functions, so they can be tested without a camera.

#include <libcamera/geometry.h>
#include <libcamera/pixel_format.h>

#include <optional>
#include <vector>

#include "camera_capture.h"

namespace biopass {

// Pixel formats to ask for, most preferred first.
std::vector<libcamera::PixelFormat> pixelFormatPreference(CameraCaptureFormat format);

// Largest size within the format's cap, unless that is narrower than
// kMinCaptureWidth and a larger one exists; then the smallest above the cap.
// nullopt when the camera reports no discrete sizes.
std::optional<libcamera::Size> pickCaptureSize(const std::vector<libcamera::Size>& sizes,
                                               const libcamera::PixelFormat& pixel_format);

// Ranks an IR sample by its mean brightness: the brightest well-exposed frame
// first, then over-exposed frames from the least washed-out.
double irFrameScore(double mean);

}  // namespace biopass

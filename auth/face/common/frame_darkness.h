#pragma once

#include "image_utils.h"

namespace biopass {

struct LumaStats {
  double mean = 0.0;
  double stddev = 0.0;
};

// BT.601 luma over every `step`-th pixel in both axes (~1/16 of a full pass
// at the default step).
LumaStats measureLuma(const ImageRGB& image, int step = 4);

// Covered lens / closed shutter thresholds. At max gain such a frame has mean
// luma in the single digits and stddev of a few levels; a dim but real scene
// (face lit by the monitor, a lamp) has stddev well above 10. Both limits must
// hold. YUYV is expanded from limited range, so they hold for YUYV and MJPEG.
//
// FaceAuth only counts faceless frames and only reports when every frame was
// dark, so a misclassified frame can only change the failure message.
constexpr double kCoveredMaxMeanLuma = 20.0;
constexpr double kCoveredMaxLumaStdDev = 8.0;

// True for a closed shutter / covered lens. Empty frames return false
// (capture failure, not darkness).
bool looksCovered(const LumaStats& stats);
bool looksCovered(const ImageRGB& image);

}  // namespace biopass

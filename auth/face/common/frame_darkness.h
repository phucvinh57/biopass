#pragma once

#include "image_utils.h"

namespace biopass {

struct LumaStats {
  double mean = 0.0;
  double stddev = 0.0;
};

// BT.601 luma statistics over a sparse grid of pixels (every `step`-th pixel
// in both axes), so it costs ~1/16 of a full pass at the default step.
LumaStats measureLuma(const ImageRGB& image, int step = 4);

// Thresholds for "the lens is covered / the privacy shutter is closed".
//
// A shut or covered lens gives an almost featureless frame: with auto exposure
// at maximum gain, sensor noise typically leaves mean luma in the single digits
// and a standard deviation of a few levels. A dim room still has structure: a
// face lit only by the monitor, or a lamp in the background, pushes stddev well
// above 10 and auto exposure brings the mean up further. Both limits must hold,
// so a merely dark-but-structured scene is not flagged. YUYV frames are
// expanded from limited range (black = 0), so these hold for YUYV and MJPEG.
//
// FaceAuth additionally only counts frames where no face was detected, and
// only reports when every frame of the session was dark, so a misclassified
// frame can never block or change a login -- only the failure message.
constexpr double kCoveredMaxMeanLuma = 20.0;
constexpr double kCoveredMaxLumaStdDev = 8.0;

// True when the frame looks like a closed shutter / covered lens. Empty frames
// are not considered covered (that is a capture failure, not darkness).
bool looksCovered(const LumaStats& stats);
bool looksCovered(const ImageRGB& image);

}  // namespace biopass

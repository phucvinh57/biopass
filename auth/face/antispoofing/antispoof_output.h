#pragma once

#include <cstddef>

namespace biopass {

struct AntiSpoofDecision {
  bool spoof;
  float score;
};

// Interprets the MobileNetV3 anti-spoofing output: index 0 = spoof,
// index 1 = real. Spoof iff the spoof class wins and its value >= threshold.
AntiSpoofDecision decodeMobileNetV3Output(const float* data, size_t size, float threshold);

}  // namespace biopass

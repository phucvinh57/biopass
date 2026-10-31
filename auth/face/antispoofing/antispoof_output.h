#pragma once

#include <cstddef>

namespace biopass {

struct AntiSpoofDecision {
  // False when the output is not a 2-class probability vector.
  bool valid;
  bool spoof;
  // Spoof probability; 1 when the output is invalid.
  float score;
};

// Interprets the MobileNetV3 anti-spoofing output: two probabilities from a
// final Softmax, index 0 = spoof, index 1 = real. Spoof iff the spoof class
// wins (ties count as spoof) and its probability >= threshold.
//
// Fails closed: wrong size, NaN/inf, values outside [0, 1] or not summing to
// 1 (e.g. raw logits from a model without Softmax) give valid = false and
// spoof = true.
AntiSpoofDecision decodeMobileNetV3Output(const float* data, size_t size, float threshold);

// Interprets the MiniFASv2 anti-spoofing output: two raw logits, index 0 =
// real, index 1 = spoof. Spoof iff softmax spoof probability >= threshold.
AntiSpoofDecision decodeMiniFasV2Output(const float* data, size_t size, float threshold);

}  // namespace biopass

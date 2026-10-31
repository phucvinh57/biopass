#include "antispoof_output.h"

#include <cmath>

namespace biopass {

namespace {

// Slack for float rounding in a Softmax output.
constexpr float kRangeEpsilon = 1e-4f;
constexpr float kSumTolerance = 1e-3f;

bool isProbability(float p) {
  return std::isfinite(p) && p >= -kRangeEpsilon && p <= 1.0f + kRangeEpsilon;
}

}  // namespace

AntiSpoofDecision decodeMobileNetV3Output(const float* data, size_t size, float threshold) {
  if (data == nullptr || size != 2 || !isProbability(data[0]) || !isProbability(data[1]) ||
      std::fabs(data[0] + data[1] - 1.0f) > kSumTolerance) {
    return AntiSpoofDecision{false, true, 1.0f};
  }
  const float spoof_prob = data[0];
  const float real_prob = data[1];
  // The spoof class must win (ties go to spoof) and reach the threshold.
  const bool spoof = spoof_prob >= real_prob && spoof_prob >= threshold;
  return AntiSpoofDecision{true, spoof, spoof_prob};
}

}  // namespace biopass

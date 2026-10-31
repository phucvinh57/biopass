#include "antispoof_output.h"

namespace biopass {

AntiSpoofDecision decodeMobileNetV3Output(const float* data, size_t size, float threshold) {
  (void)size;
  // argmax over 2 classes; ties go to index 0 (spoof).
  const int winner = data[1] > data[0] ? 1 : 0;
  const float score = data[winner];
  return AntiSpoofDecision{winner == 0 && score >= threshold, score};
}

}  // namespace biopass

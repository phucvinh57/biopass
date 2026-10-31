// Checks how the MobileNetV3 anti-spoofing output is turned into a spoof
// decision (face/antispoofing/antispoof_output.h). Exits non-zero on failure.

#include <cmath>
#include <cstdio>
#include <vector>

#include "antispoof_output.h"

namespace {

using biopass::AntiSpoofDecision;
using biopass::decodeMobileNetV3Output;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

constexpr float kThreshold = 0.8f;

AntiSpoofDecision decode(const std::vector<float>& out, float threshold = kThreshold) {
  return decodeMobileNetV3Output(out.data(), out.size(), threshold);
}

void testValidOutputs() {
  expect(!decode({0.1f, 0.9f}).spoof, "real face: {0.1, 0.9} is not spoof");
  expect(decode({0.9f, 0.1f}).spoof, "spoof above threshold: {0.9, 0.1} is spoof");
  expect(decode({0.8f, 0.2f}).spoof, "spoof at threshold: {0.8, 0.2} is spoof");
  expect(!decode({0.7f, 0.3f}).spoof, "spoof below threshold: {0.7, 0.3} is not spoof");
  expect(!decode({0.5f, 0.5f}).spoof, "tie below threshold is not spoof");
  expect(decode({0.5f, 0.5f}, 0.5f).spoof, "tie goes to the spoof class");
  expect(decode({0.1f, 0.9f}).valid && decode({0.9f, 0.1f}).valid, "probabilities are valid");
  expect(decode({0.4f, 0.6001f}).valid, "sum within tolerance is valid");
}

void testScoreIsSpoofProbability() {
  expect(decode({0.1f, 0.9f}).score == 0.1f, "real face: score is the spoof probability");
  expect(decode({0.9f, 0.1f}).score == 0.9f, "spoof face: score is the spoof probability");
}

// A model without a final Softmax: must not pass as real.
void expectFailClosed(const std::vector<float>& out, size_t size, const char* what) {
  const AntiSpoofDecision d = decodeMobileNetV3Output(out.data(), size, kThreshold);
  expect(!d.valid && d.spoof, what);
}

void testInvalidOutputsFailClosed() {
  expectFailClosed({2.3f, -1.1f}, 2, "raw logits {2.3, -1.1} fail closed");
  // softmax({0.6, -3.0}) = {0.97, 0.03}: read raw, 0.6 < threshold would pass.
  expectFailClosed({0.6f, -3.0f}, 2, "raw logits {0.6, -3.0} fail closed");
  expectFailClosed({0.3f, 0.3f}, 2, "values not summing to 1 fail closed");
  expectFailClosed({NAN, 0.5f}, 2, "NaN spoof value fails closed");
  expectFailClosed({0.5f, NAN}, 2, "NaN real value fails closed");
  expectFailClosed({INFINITY, 0.0f}, 2, "inf fails closed");
  expectFailClosed({0.9f, 0.1f}, 1, "one value fails closed");
  expectFailClosed({0.0f, 1.0f, 0.0f}, 3, "three values fail closed");
  expectFailClosed({}, 0, "empty output fails closed");
}

}  // namespace

int main() {
  testValidOutputs();
  testScoreIsSpoofProbability();
  testInvalidOutputsFailClosed();
  if (g_failures > 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}

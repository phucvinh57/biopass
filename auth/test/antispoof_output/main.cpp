// Checks how the MobileNetV3 anti-spoofing output is turned into a spoof
// decision (face/antispoofing/antispoof_output.h). Exits non-zero on failure.

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
}

}  // namespace

int main() {
  testValidOutputs();
  if (g_failures > 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}

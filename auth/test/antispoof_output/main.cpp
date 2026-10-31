// Checks how the MobileNetV3 and MiniFASv2 anti-spoofing outputs are turned
// into a spoof decision (face/antispoofing/antispoof_output.h). Exits non-zero
// on failure.

#include <cmath>
#include <cstdio>
#include <vector>

#include "antispoof_output.h"

namespace {

using biopass::AntiSpoofDecision;
using biopass::decodeMiniFasV2Output;
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

AntiSpoofDecision decodeMiniFas(const std::vector<float>& out, float threshold = kThreshold) {
  return decodeMiniFasV2Output(out.data(), out.size(), threshold);
}

void testMiniFasV2ValidOutputs() {
  // Logits: index 0 = real, index 1 = spoof. softmax({0, 2})[1] = 0.8808.
  const AntiSpoofDecision spoof = decodeMiniFas({0.0f, 2.0f});
  expect(spoof.valid && spoof.spoof, "minifasv2: {0, 2} is spoof");
  expect(std::fabs(spoof.score - 0.880797f) < 1e-5f, "minifasv2: score is softmax spoof prob");
  const AntiSpoofDecision real = decodeMiniFas({2.0f, 0.0f});
  expect(real.valid && !real.spoof, "minifasv2: {2, 0} is real");
  expect(std::fabs(real.score - 0.119203f) < 1e-5f, "minifasv2: real score is spoof prob");
  expect(!decodeMiniFas({0.0f, 0.0f}).spoof, "minifasv2: 0.5 below threshold is real");
  expect(decodeMiniFas({0.0f, 0.0f}, 0.5f).spoof, "minifasv2: prob == threshold is spoof");
  expect(decodeMiniFas({0.0f, 100.0f}).spoof, "minifasv2: large spoof logit is stable");
  expect(!decodeMiniFas({100.0f, 0.0f}).spoof, "minifasv2: large real logit is stable");
  expect(decodeMiniFas({-3.0f, 5.0f}).valid, "minifasv2: finite logits are valid");
}

}  // namespace

int main() {
  testValidOutputs();
  testScoreIsSpoofProbability();
  testInvalidOutputsFailClosed();
  testMiniFasV2ValidOutputs();
  if (g_failures > 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}

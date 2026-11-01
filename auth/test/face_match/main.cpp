// How a face match is scored and decided (face/recognition): the score is
// the cosine similarity of the two embeddings (higher = more alike), and a
// match needs a similarity strictly above the threshold. Pure, no model
// needed. Exits non-zero on any failure.

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "face_recognition.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

void checkCosine() {
  using biopass::cosineSimilarity;
  expect(near(cosineSimilarity({1, 2, 3}, {2, 4, 6}), 1.0f), "same direction: similarity 1");
  expect(near(cosineSimilarity({1, 0}, {0, 1}), 0.0f), "orthogonal: similarity 0");
  expect(near(cosineSimilarity({1, 2}, {-1, -2}), -1.0f), "opposite: similarity -1");
  expect(cosineSimilarity({1, 0}, {1, 0.1f}) > cosineSimilarity({1, 0}, {1, 1}),
         "closer embeddings score higher");

  bool threw = false;
  try {
    cosineSimilarity({0, 0}, {1, 1});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  expect(threw, "zero-magnitude embedding throws (never a match)");
}

void checkThreshold() {
  using biopass::isSameFace;
  expect(isSameFace(0.51f, 0.5f), "similarity above threshold: same face");
  expect(!isSameFace(0.5f, 0.5f), "similarity equal to threshold: not the same face");
  expect(!isSameFace(0.49f, 0.5f), "similarity below threshold: not the same face");
  expect(!isSameFace(NAN, 0.5f), "NaN similarity: not the same face");
}

}  // namespace

int main() {
  checkCosine();
  checkThreshold();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

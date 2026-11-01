// biopass::firstLine (face/common/onnx_session.h): the part of a model-load
// error FaceAuth logs. Exits non-zero on any failure.

#include <cstdio>
#include <string>

#include "onnx_session.h"

namespace {

using biopass::firstLine;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

}  // namespace

int main() {
  expect(firstLine("") == "", "empty stays empty");
  expect(firstLine("one line") == "one line", "single line kept whole");
  expect(firstLine("first\nsecond\nthird") == "first", "cut at the first newline");
  expect(firstLine("\nrest") == "", "leading newline gives empty");
  expect(firstLine("trailing\n") == "trailing", "trailing newline dropped");
  expect(firstLine("cr\r\nlf") == "cr\r", "only \\n splits");

  return g_failures == 0 ? 0 : 1;
}

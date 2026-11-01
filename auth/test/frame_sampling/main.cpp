// How many frames a capture pulls when some are dropped
// (face/common/frame_sampling.h). Exits non-zero on any failure.

#include <cstdio>
#include <string>

#include "frame_sampling.h"

namespace {

using biopass::FrameSampling;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

// Feeds `frames` ('g' = usable, 'b' = dropped) while the policy wants more and
// returns how many it consumed.
int run(FrameSampling sampling, const std::string& frames) {
  int used = 0;
  for (char frame : frames) {
    if (!sampling.wantsMore()) {
      break;
    }
    if (frame == 'g') {
      sampling.recordGood();
    } else {
      sampling.recordDropped();
    }
    ++used;
  }
  return used;
}

void testColour() {
  expect(run(FrameSampling(1, 5), "gggg") == 1, "colour: first good frame ends it");
  expect(run(FrameSampling(1, 5), "bbgg") == 3, "colour: dropped frames don't end it");
  expect(run(FrameSampling(1, 5), "bbbbbbbbbb") == 5, "colour: stops after max dropped");
}

void testIr() {
  expect(run(FrameSampling(3, 5), "gbgbgg") == 5, "ir: dropped frames don't count as samples");
  expect(run(FrameSampling(3, 2), "gbbggg") == 3, "ir: cap applies across samples");
}

void testEdges() {
  expect(run(FrameSampling(0, 5), "g") == 0, "no samples wanted: nothing pulled");
  FrameSampling done(1, 5);
  done.recordGood();
  expect(done.gotAny(), "gotAny after a good frame");
  FrameSampling none(1, 1);
  none.recordDropped();
  expect(!none.gotAny() && !none.wantsMore(), "all dropped: nothing got, no more");
}

}  // namespace

int main() {
  testColour();
  testIr();
  testEdges();
  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}

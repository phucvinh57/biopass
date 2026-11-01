// The IR presence check (face/antispoofing/ir_camera_as.h) must stop waiting
// once the run is cancelled: its warmup sleep and retry loop would otherwise
// hold a parallel success or overrun the helper's time budget. Uses a fake
// IR session that never yields a frame, so no model is needed. Exits non-zero
// on any failure.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

#include "camera_capture.h"
#include "ir_camera_as.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

// Open, but every capture comes back empty (a blinking emitter at its worst).
class DarkSession : public biopass::ICameraCaptureSession {
 public:
  bool isOpen() const override { return true; }
  ImageRGB capture() override { return {}; }
};

long long runMs(int warmup_ms, int presence_ms, const std::atomic<bool>* cancel, bool* passed) {
  DarkSession session;
  const auto start = std::chrono::steady_clock::now();
  const biopass::IrCameraConfig ir{"/dev/fake-ir", warmup_ms, presence_ms};
  *passed = biopass::checkAntispoofByIRCamera(ir, nullptr, "nobody", false, &session, cancel);
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               start)
      .count();
}

}  // namespace

int main() {
  bool passed = true;

  std::atomic<bool> cancelled{true};
  long long took = runMs(5000, 10000, &cancelled, &passed);
  expect(!passed, "cancelled: presence check fails");
  expect(took < 500,
         "cancelled: skips the warmup sleep and retries (" + std::to_string(took) + " ms)");

  std::atomic<bool> live{false};
  took = runMs(50, 200, &live, &passed);
  expect(!passed, "no frames: presence check fails");
  expect(took >= 200 && took < 1000,
         "not cancelled: keeps retrying until its timeout (" + std::to_string(took) + " ms)");

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

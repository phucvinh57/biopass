// Teardown rules for a libcamera capture session
// (face/common/camera_lifecycle.h). Exits non-zero on any failure.

#include <cstdio>

#include "camera_lifecycle.h"

namespace {

using biopass::CameraLifecycle;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

void testNothingAcquired() {
  CameraLifecycle life;
  expect(!life.needsStop(), "fresh: no stop");
  expect(life.canFreeBuffers(), "fresh: buffers can be freed");
  expect(!life.needsRelease(), "fresh: no release");
}

void testAcquiredNotStarted() {
  // configure/allocate/start failed after acquire().
  CameraLifecycle life;
  life.markAcquired();
  expect(!life.needsStop(), "acquired: no stop");
  expect(life.canFreeBuffers(), "acquired: buffers can be freed");
  expect(life.needsRelease(), "acquired: release");
  life.markReleased();
  expect(!life.needsRelease(), "acquired: release only once");
}

void testStartedThenQueueFailed() {
  // start() succeeded, queueRequest() failed: the camera is running and owns
  // the queued requests until stop() returns.
  CameraLifecycle life;
  life.markAcquired();
  life.markStarted();
  expect(life.isStarted(), "started: reports started");
  expect(life.needsStop(), "started: stop first");
  expect(!life.canFreeBuffers(), "started: buffers kept until stopped");
  expect(!life.needsRelease(), "started: no release while running");

  life.markStopped(true);
  expect(!life.isStarted(), "stopped: not started");
  expect(!life.needsStop(), "stopped: stop only once");
  expect(life.canFreeBuffers(), "stopped: buffers can be freed");
  expect(life.needsRelease(), "stopped: release");
  life.markReleased();
  expect(!life.needsStop() && !life.needsRelease(), "stopped: second teardown is a no-op");
}

void testStopFailed() {
  // stop() failed (camera unplugged): libcamera may still hold the requests,
  // and release() refuses a running camera.
  CameraLifecycle life;
  life.markAcquired();
  life.markStarted();
  life.markStopped(false);
  expect(!life.isStarted(), "stop failed: not usable for capture");
  expect(!life.needsStop(), "stop failed: not retried");
  expect(!life.canFreeBuffers(), "stop failed: buffers kept");
  expect(!life.needsRelease(), "stop failed: no release");
}

}  // namespace

int main() {
  testNothingAcquired();
  testAcquiredNotStarted();
  testStartedThenQueueFailed();
  testStopFailed();
  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}

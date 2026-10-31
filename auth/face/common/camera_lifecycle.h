#pragma once

namespace biopass {

// Tracks how far a libcamera session got so teardown runs in the right order:
// stop -> (disconnect, unmap) -> free requests/buffers -> release.
//
// A started camera owns its queued requests until stop() returns, and
// release() refuses a running camera. The only way out of "started" is
// markStopped(), so a failed setup step can't skip stop().
class CameraLifecycle {
 public:
  void markAcquired() { acquired_ = true; }
  void markStarted() { started_ = true; }

  // `ok` is false when stop() failed; the camera is then treated as still
  // running, so its buffers are kept and it is not released.
  void markStopped(bool ok) {
    if (ok) {
      started_ = false;
    }
    stop_failed_ = !ok;
  }
  void markReleased() { acquired_ = false; }

  // Running and usable for capture. False once stop() was attempted.
  bool isStarted() const { return started_ && !stop_failed_; }
  bool needsStop() const { return isStarted(); }
  bool canFreeBuffers() const { return !started_; }
  bool needsRelease() const { return acquired_ && !started_; }

 private:
  bool acquired_ = false;
  bool started_ = false;
  bool stop_failed_ = false;
};

}  // namespace biopass

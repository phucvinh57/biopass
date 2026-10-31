#pragma once

namespace biopass {

// Tracks how far a libcamera session got so teardown runs in the right order:
// stop -> (disconnect, unmap) -> free requests/buffers -> release.
//
// A started camera owns its queued requests until stop() returns, and
// release() refuses a camera that isn't stopped. The only ways out of
// "started" are markStopped() and markDisconnected(), so a failed setup step
// can't skip stop().
//
// When the camera is unplugged while running, libcamera forces it back to
// Configured without cancelling its requests: stop() then returns 0 and does
// nothing, and the pipeline may still hold the requests. Those buffers are
// kept (leaked) rather than freed; release() is still allowed.
class CameraLifecycle {
 public:
  void markAcquired() { acquired_ = true; }
  void markStarted() { started_ = true; }

  // `ok` is false when stop() failed; the camera may then still be running,
  // so its buffers are kept and it is not released.
  void markStopped(bool ok) {
    started_ = false;
    if (!ok) {
      requests_held_ = true;
      stop_failed_ = true;
    }
  }
  void markDisconnected() {
    if (started_) {
      started_ = false;
      requests_held_ = true;
    }
  }
  void markReleased() { acquired_ = false; }

  // Running and usable for capture.
  bool isStarted() const { return started_; }
  bool needsStop() const { return started_; }
  bool canFreeBuffers() const { return !started_ && !requests_held_; }
  bool needsRelease() const { return acquired_ && !started_ && !stop_failed_; }

 private:
  bool acquired_ = false;
  bool started_ = false;
  bool requests_held_ = false;  // libcamera may still reference our requests
  bool stop_failed_ = false;
};

}  // namespace biopass

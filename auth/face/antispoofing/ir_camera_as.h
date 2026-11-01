#pragma once

#include <atomic>
#include <string>

namespace biopass {

class ICameraCaptureSession;
class FaceDetection;

// True when the caller's face detector finds a face in the IR frame.
//
// NOT a liveness detector: a printed photo in front of an IR camera can pass.
// Treat it as a blank-frame guard.
//
// warmup_delay_ms: one sleep before the first capture so IR LEDs and
// auto-exposure settle.
// presence_timeout_ms: retry budget for capture + inference, since a blinking
// emitter can yield all-white/all-dark frames. 0 = single attempt.
// cancel: the run's cancel signal (may be null); once set, the warmup sleep
// and the retries stop and the check fails.
bool checkAntispoofByIRCamera(const std::string& ir_camera_path, FaceDetection* detector,
                              const std::string& username, bool debug,
                              ICameraCaptureSession* session = nullptr, int warmup_delay_ms = 300,
                              int presence_timeout_ms = 1500,
                              const std::atomic<bool>* cancel = nullptr);

}  // namespace biopass

#pragma once

#include <atomic>
#include <string>

#include "auth_config.h"

namespace biopass {

class ICameraCaptureSession;
class FaceDetection;

// True when the caller's face detector finds a face in the IR frame.
//
// NOT a liveness detector: a printed photo in front of an IR camera can pass.
// Treat it as a blank-frame guard.
//
// ir: from irCameraConfig(); an empty device fails the check.
// ir.warmup_delay_ms: one sleep before the first capture so IR LEDs and
// auto-exposure settle.
// ir.presence_timeout_ms: retry budget for capture + inference, since a
// blinking emitter can yield all-white/all-dark frames. 0 = single attempt.
// session: the caller's open IR session; null opens one per attempt.
// cancel: the run's cancel signal (may be null); once set, the warmup sleep
// and the retries stop and the check fails.
bool checkAntispoofByIRCamera(const IrCameraConfig& ir, FaceDetection* detector,
                              const std::string& username, bool debug,
                              ICameraCaptureSession* session = nullptr,
                              const std::atomic<bool>* cancel = nullptr);

}  // namespace biopass

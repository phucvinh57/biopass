#pragma once

#include <atomic>
#include <string>

#include "auth_config.h"
#include "auth_method.h"
#include "image.h"
#include "model_registry.h"
#include "session_engine.h"

namespace biopass {

class ICameraCaptureSession;
class FaceDetection;
class FaceAntiSpoofing;
struct Box;

// shared_detector / model_registry: the caller's, reused instead of loading
// the model or opening the DB again.
// ai_model: the caller's anti-spoofing model slot. The first check that needs it
// loads it (as the user) and later attempts reuse it; a failed load keeps
// nothing and fails the check.
// frame/face_box: the full frame and the detected face; the model builds its
// own crop (MiniFASv2 needs context around the face).
// cancel: the run's cancel signal (may be null); stops the IR wait early.
bool checkAntiSpoof(const FaceMethodConfig& face_config, const std::string& username,
                    const ImageRGB& frame, const Box& face_box, const AuthConfig& config,
                    const ModelRegistry& model_registry, SessionEngine<FaceAntiSpoofing>& ai_model,
                    FaceDetection* shared_detector,
                    ICameraCaptureSession* ir_camera_session = nullptr,
                    const std::atomic<bool>* cancel = nullptr);

}  // namespace biopass

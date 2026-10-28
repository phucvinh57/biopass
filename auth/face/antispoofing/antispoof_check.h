#pragma once

#include <string>

#include "auth_config.h"
#include "auth_method.h"
#include "image_utils.h"
#include "model_registry.h"

namespace biopass {

class ICameraCaptureSession;
class FaceDetection;
struct Box;

// shared_detector: the caller's already-loaded face detector, reused for the
// IR presence check instead of loading a second copy of the model.
// model_registry: the caller's already-open sqlite connection, reused to
// resolve the anti-spoofing model_id instead of opening a second connection.
// frame/face_box: the full camera frame and the detected face in it; the AI
// model builds its own crop (MiniFASv2 needs context around the face).
bool checkAntiSpoof(const FaceMethodConfig& face_config, const std::string& username,
                    const ImageRGB& frame, const Box& face_box, const AuthConfig& config,
                    const ModelRegistry& model_registry, FaceDetection* shared_detector,
                    ICameraCaptureSession* ir_camera_session = nullptr);

}  // namespace biopass

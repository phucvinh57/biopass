#pragma once

#include <memory>

#include "auth_config.h"
#include "auth_method.h"
#include "camera_capture.h"
#include "face_detection.h"
#include "face_recognition.h"
#include "model_registry.h"

namespace biopass {

/**
 * Face authentication method.
 * Wraps existing face detection, recognition, and anti-spoofing.
 */
class FaceAuth : public IAuthMethod {
 public:
  // model_registry_ opens one sqlite connection for the lifetime of this
  // instance (one authentication session), reused for every model_id lookup
  // instead of opening/closing the DB per lookup.
  FaceAuth(const FaceMethodConfig& config, const std::string& username)
      : face_config_(config), model_registry_(username) {}
  ~FaceAuth() override = default;

  std::string name() const override { return "Face"; }
  bool isAvailable() const override;
  uint32_t getRetries() const override { return face_config_.retries; }
  uint32_t getRetryDelayMs() const override { return face_config_.retry_delay; }
  void beginAuthenticationSession() override;
  void endAuthenticationSession() override;
  AuthResult authenticate(const std::string& username, const AuthConfig& config,
                          std::atomic<bool>* cancel_signal = nullptr) override;

 private:
  void ensureIrSession();
  CaptureProfile irCaptureProfile() const;
  bool irIsMainCamera() const;
  std::unique_ptr<ICameraCaptureSession> openMainSession() const;
  // Loads the detection + recognition models once; returns false if either
  // model file is missing or fails to load.
  bool ensureModelsLoaded();

  FaceMethodConfig face_config_;
  ModelRegistry model_registry_;
  std::unique_ptr<ICameraCaptureSession> camera_session_;
  std::unique_ptr<ICameraCaptureSession> ir_camera_session_;
  std::unique_ptr<FaceDetection> detector_;
  std::unique_ptr<FaceRecognition> recognizer_;
};

}  // namespace biopass

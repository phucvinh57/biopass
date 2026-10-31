#pragma once

#include <memory>

#include "auth_config.h"
#include "auth_method.h"
#include "camera_capture.h"
#include "face_detection.h"
#include "face_recognition.h"
#include "model_registry.h"

namespace biopass {

class FaceAuth : public IAuthMethod {
 public:
  // One sqlite connection per auth session, reused for every model_id lookup.
  FaceAuth(const FaceMethodConfig& config, const std::string& username)
      : face_config_(config), username_(username), model_registry_(username) {}
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
  // One attempt; authenticate() wraps it so nothing it throws escapes.
  AuthResult attempt(const std::string& username, const AuthConfig& config,
                     std::atomic<bool>* cancel_signal);
  void ensureIrSession();
  // Loads detection + recognition once; false if either fails to load.
  bool ensureModelsLoaded();

  FaceMethodConfig face_config_;
  // Owner of the registry and model files, which are read as this user.
  std::string username_;
  ModelRegistry model_registry_;
  std::unique_ptr<ICameraCaptureSession> camera_session_;
  std::unique_ptr<ICameraCaptureSession> ir_camera_session_;
  std::unique_ptr<FaceDetection> detector_;
  std::unique_ptr<FaceRecognition> recognizer_;

  // Tells the user why the session failed when every frame looked like a
  // covered lens, then resets the per-session feedback state below.
  void finishSessionFeedback();

  // Per-session user feedback state (reset by finishSessionFeedback).
  bool scan_announced_ = false;
  uint32_t frames_captured_ = 0;
  uint32_t covered_frames_ = 0;
  std::atomic<bool>* cancel_signal_ = nullptr;
};

}  // namespace biopass

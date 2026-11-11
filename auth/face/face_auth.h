#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "auth_config.h"
#include "auth_method.h"
#include "camera_capture.h"
#include "camera_profile.h"
#include "face_as.h"
#include "face_detection.h"
#include "face_recognition.h"
#include "model_registry.h"
#include "session_engine.h"

namespace biopass {

class FaceAuth : public IAuthMethod {
 public:
  // Opens a camera session; same arguments as openCameraSession(). Used for
  // both the main and the IR camera.
  using CameraOpener = std::function<std::unique_ptr<ICameraCaptureSession>(
      const std::optional<std::string>& device_path, CameraCaptureFormat format, int warmup_frames,
      int capture_timeout_ms)>;

  using CameraShareProbe = bool (*)(const std::optional<std::string>& main_path,
                                    const std::string& ir_path);

  // One sqlite connection per auth session, reused for every model_id lookup.
  // open_camera: openCameraSession() unless a test passes a fake.
  // The camera profile is picked once here, for this helper run.
  FaceAuth(const FaceMethodConfig& config, const std::string& username,
           CameraOpener open_camera = nullptr,
           CameraShareProbe share_camera = shareLibcameraCamera);
  ~FaceAuth() override = default;

  std::string name() const override { return "Face"; }
  bool isAvailable() const override;
  uint32_t getRetries() const override { return face_config_.retries; }
  uint32_t getRetryDelayMs() const override { return face_config_.retry_delay; }
  void beginAuthenticationSession() override;
  void endAuthenticationSession() override;
  AuthResult authenticate(const std::string& username, const AuthConfig& config,
                          std::atomic<bool>* cancel_signal = nullptr) override;

  // The session the IR presence check captures from, or nullptr. When the IR
  // camera is the main camera it is the main session: libcamera can't acquire
  // one camera twice. Otherwise the IR session, opened on first use. Public
  // for tests.
  ICameraCaptureSession* irPresenceSession();

 private:
  // One attempt; authenticate() wraps it so nothing it throws escapes.
  AuthResult attempt(const std::string& username, const AuthConfig& config,
                     std::atomic<bool>* cancel_signal);
  // True when ir_camera names the same device as camera.
  bool irIsMainCamera() const;
  // The main camera; opened as grey when it is the IR camera.
  std::unique_ptr<ICameraCaptureSession> openMainCamera() const;
  // Loads detection + recognition once; false if either fails to load.
  bool ensureModelsLoaded();
  // True once `face` matches one of the user's enrolled images.
  bool matchAgainstEnrolled(const std::string& username, const ImageRGB& face,
                            const std::vector<std::string>& enrolled_paths);

  FaceMethodConfig face_config_;
  // Owner of the registry and model files, which are read as this user.
  std::string username_;
  ModelRegistry model_registry_;
  CameraOpener open_camera_;
  bool sequential_ir_ = false;
  // Opened by isAvailable() (hence mutable) and kept for the auth session.
  mutable std::unique_ptr<ICameraCaptureSession> camera_session_;
  std::unique_ptr<ICameraCaptureSession> ir_camera_session_;
  std::unique_ptr<FaceDetection> detector_;
  std::unique_ptr<FaceRecognition> recognizer_;
  // Loaded by the first anti-spoofing check and kept, like detector_ and
  // recognizer_, for this object's lifetime (one helper run).
  SessionEngine<FaceAntiSpoofing> antispoof_model_;

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

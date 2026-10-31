#include "face_auth.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "antispoof_check.h"
#include "camera_capture.h"
#include "debug_image_io.h"
#include "image_utils.h"

namespace biopass {

bool FaceAuth::isAvailable() const { return checkCameraAvailability(face_config_.camera); }

// Opens the IR session only when the IR presence check is about to run. A
// failed open leaves ir_camera_session_ null; checkAntiSpoof() then falls back
// to a one-shot capture per attempt.
void FaceAuth::ensureIrSession() {
  if (face_config_.anti_spoofing.ir_camera.has_value() &&
      !face_config_.anti_spoofing.ir_camera->empty() &&
      (!ir_camera_session_ || !ir_camera_session_->isOpen())) {
    ir_camera_session_ =
        openCameraSession(*face_config_.anti_spoofing.ir_camera, CameraCaptureFormat::V4L2Grey,
                          kIrCaptureWarmupFrames, kIrCaptureTimeoutMs);
    if (!ir_camera_session_) {
      spdlog::warn("FaceAuth: Could not open IR camera session on '{}'",
                   *face_config_.anti_spoofing.ir_camera);
    }
  }
}

bool FaceAuth::ensureModelsLoaded() {
  if (detector_ && recognizer_) {
    return true;
  }

  const auto detectModel = model_registry_.resolveModel(face_config_.detection.model_id);
  const auto recogModel = model_registry_.resolveModel(face_config_.recognition.model_id);
  const std::string detectModelPath = detectModel ? detectModel->path : "";
  const std::string recogModelPath = recogModel ? recogModel->path : "";
  if (!std::ifstream(recogModelPath).good() || !std::ifstream(detectModelPath).good()) {
    spdlog::error("FaceAuth: Model files not found");
    return false;
  }

  try {
    detector_ =
        std::make_unique<FaceDetection>(detectModelPath, 640, face_config_.detection.threshold);
    spdlog::debug("FaceAuth: Detection model loaded | threshold={:.3f}",
                  face_config_.detection.threshold);
  } catch (const std::exception& e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    spdlog::error("FaceAuth: Failed to load detection model: {}, skipping", msg);
    return false;
  }

  try {
    recognizer_ =
        std::make_unique<FaceRecognition>(recogModelPath, 112, face_config_.recognition.threshold);
    spdlog::debug("FaceAuth: Recognition model loaded | threshold={:.3f}",
                  face_config_.recognition.threshold);
  } catch (const std::exception& e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    spdlog::error("FaceAuth: Failed to load recognition model: {}, skipping", msg);
    detector_.reset();
    return false;
  }

  return true;
}

void FaceAuth::beginAuthenticationSession() {
  if (!camera_session_) {
    camera_session_ = openCameraSession(face_config_.camera);
  }
  // The IR session is opened lazily in authenticate(), right before the
  // anti-spoofing check: opening it here would stream IR alongside every
  // colour capture, sharing USB bandwidth even when no face is found yet.
  ensureModelsLoaded();
}

void FaceAuth::endAuthenticationSession() {
  ir_camera_session_.reset();
  camera_session_.reset();
}

AuthResult FaceAuth::authenticate(const std::string& username, const AuthConfig& config,
                                  std::atomic<bool>* cancel_signal) {
  if (!camera_session_) {
    camera_session_ = openCameraSession(face_config_.camera);
  }
  if (!camera_session_ || !camera_session_->isOpen()) {
    spdlog::error("FaceAuth: Could not open camera");
    if (!checkCameraAvailability(face_config_.camera)) {
      return AuthResult::Unavailable;
    }
    return AuthResult::Retry;
  }

  std::vector<std::string> enrolledFaces = biopass::listFaces(username);
  if (enrolledFaces.empty()) {
    spdlog::error("FaceAuth: No face enrolled for user {}, skipping", username);
    return AuthResult::Unavailable;
  }

  if (!ensureModelsLoaded()) {
    spdlog::error("FaceAuth: Models not available for user {}, skipping", username);
    return AuthResult::Unavailable;
  }

  if (cancel_signal && cancel_signal->load()) {
    return AuthResult::Failure;
  }

  ImageRGB loginFace = camera_session_->capture();
  if (loginFace.empty()) {
    spdlog::error("FaceAuth: Could not read frame");
    camera_session_.reset();
    return AuthResult::Retry;
  }

  std::vector<Detection> detectedImages = detector_->inference(loginFace);
  if (detectedImages.empty()) {
    spdlog::error("FaceAuth: No face detected");
    return AuthResult::Retry;
  }

  ImageRGB face = detectedImages[0].image;

  ensureIrSession();

  if (!checkAntiSpoof(face_config_, username, face, config, model_registry_, detector_.get(),
                      ir_camera_session_.get())) {
    spdlog::warn("FaceAuth: Anti-spoofing failed — returning Failure (no retry allowed)");
    // Always tear down the IR session so a subsequent call cannot reuse a
    // partially-warmed camera to bypass the check.
    ir_camera_session_.reset();
    return AuthResult::Failure;
  }

  // Match against all enrolled faces — succeed if any match.
  spdlog::debug("FaceAuth: Recognition | threshold={:.3f} enrolled_count={}",
                face_config_.recognition.threshold, enrolledFaces.size());
  for (const auto& facePath : enrolledFaces) {
    ImageRGB preparedFace = readImage(facePath);
    if (preparedFace.empty()) {
      spdlog::warn("FaceAuth: Recognition | could not load enrolled image: {}", facePath);
      continue;
    }

    MatchResult match = recognizer_->match(preparedFace, face);
    spdlog::debug("FaceAuth: Recognition | face='{}' score={:.4f} threshold={:.3f} similar={}",
                  facePath, match.dist, face_config_.recognition.threshold, match.similar);
    if (match.similar) {
      spdlog::debug("FaceAuth: Recognition PASSED | matched face='{}' score={:.4f}", facePath,
                    match.dist);
      return AuthResult::Success;
    }
  }

  if (config.debug) {
    saveFailedFace(username, face, "not_similar");
  }

  return AuthResult::Retry;
}

}  // namespace biopass

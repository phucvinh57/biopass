#include "face_auth.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "antispoof_check.h"
#include "camera_capture.h"
#include "debug_image_io.h"
#include "frame_darkness.h"
#include "image_utils.h"
#include "status_reporter.h"

namespace biopass {

bool FaceAuth::isAvailable() const { return checkCameraAvailability(face_config_.camera); }

void FaceAuth::ensureIrSession() {
  if (face_config_.anti_spoofing.ir_camera.has_value() &&
      !face_config_.anti_spoofing.ir_camera->empty() &&
      (!ir_camera_session_ || !ir_camera_session_->isOpen())) {
    ir_camera_session_ =
        openCameraSession(*face_config_.anti_spoofing.ir_camera, CameraCaptureFormat::V4L2Grey,
                          kIrCaptureWarmupFrames, kIrCaptureTimeoutMs);
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
  ensureIrSession();
  ensureModelsLoaded();
}

void FaceAuth::finishSessionFeedback() {
  // Every captured frame was dark and faceless: almost certainly a closed
  // privacy shutter or a covered lens, not a stranger. Skip it if another
  // method already succeeded (parallel mode) -- the login went through.
  const bool cancelled = cancel_signal_ && cancel_signal_->load();
  if (!cancelled && frames_captured_ > 0 && covered_frames_ == frames_captured_) {
    spdlog::warn("FaceAuth: All {} frames were dark, camera appears covered", frames_captured_);
    reportError("Biopass: camera appears covered or the shutter is closed");
  }
  scan_announced_ = false;
  frames_captured_ = 0;
  covered_frames_ = 0;
  cancel_signal_ = nullptr;
}

void FaceAuth::endAuthenticationSession() {
  finishSessionFeedback();
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

  cancel_signal_ = cancel_signal;
  if (cancel_signal && cancel_signal->load()) {
    return AuthResult::Failure;
  }

  // Announce once per session, only now that scanning will really happen
  // (camera open, faces enrolled, models loaded).
  if (!scan_announced_) {
    scan_announced_ = true;
    reportInfo("Biopass: looking for your face...");
  }

  ImageRGB loginFace = camera_session_->capture();
  if (loginFace.empty()) {
    spdlog::error("FaceAuth: Could not read frame");
    camera_session_.reset();
    return AuthResult::Retry;
  }
  frames_captured_++;

  std::vector<Detection> detectedImages = detector_->inference(loginFace);
  if (detectedImages.empty()) {
    // Detection always runs first, so the darkness check can only change the
    // failure message, never reject a frame that has a usable face.
    const LumaStats luma = measureLuma(loginFace);
    if (looksCovered(luma)) {
      covered_frames_++;
    }
    spdlog::error("FaceAuth: No face detected | luma mean={:.1f} stddev={:.1f}", luma.mean,
                  luma.stddev);
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

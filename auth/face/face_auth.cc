#include "face_auth.h"

#include <algorithm>
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

// IR capture tuned by the user's anti-spoofing settings (UI: IR warm-up delay).
CaptureProfile FaceAuth::irCaptureProfile() const {
  CaptureProfile profile = kIrCaptureProfile;
  profile.max_warmup_ms = std::max(0, face_config_.anti_spoofing.ir_warmup_delay_ms);
  return profile;
}

bool FaceAuth::irIsMainCamera() const {
  return face_config_.anti_spoofing.ir_camera.has_value() && face_config_.camera.has_value() &&
         !face_config_.anti_spoofing.ir_camera->empty() &&
         *face_config_.anti_spoofing.ir_camera == *face_config_.camera;
}

// The main camera is whatever the user picked as "Camera Device". If that is
// the IR sensor (Windows-Hello style: enrol and recognise on IR), open it as a
// grey stream with the IR profile; otherwise let the session pick the colour
// profile from the negotiated format.
std::unique_ptr<ICameraCaptureSession> FaceAuth::openMainSession() const {
  if (irIsMainCamera()) {
    return openCameraSession(face_config_.camera, CameraCaptureFormat::V4L2Grey,
                             irCaptureProfile());
  }
  return openCameraSession(face_config_.camera);
}

void FaceAuth::ensureIrSession() {
  if (face_config_.anti_spoofing.ir_camera.has_value() &&
      !face_config_.anti_spoofing.ir_camera->empty() &&
      (!ir_camera_session_ || !ir_camera_session_->isOpen())) {
    ir_camera_session_ = openCameraSession(*face_config_.anti_spoofing.ir_camera,
                                           CameraCaptureFormat::V4L2Grey, irCaptureProfile());
  }
}

bool FaceAuth::ensureModelsLoaded() {
  if (detector_ && recognizer_) {
    return true;
  }

  const std::string detectModelPath =
      model_registry_.resolveModelPath(face_config_.detection.model_id).value_or("");
  const std::string recogModelPath =
      model_registry_.resolveModelPath(face_config_.recognition.model_id).value_or("");
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
    camera_session_ = openMainSession();
  }
  // IR session is opened lazily in authenticate(), right before the anti-spoof
  // check, so the colour capture never shares USB bandwidth with the IR stream.
  ensureModelsLoaded();
}

void FaceAuth::endAuthenticationSession() {
  ir_camera_session_.reset();
  camera_session_.reset();
}

AuthResult FaceAuth::authenticate(const std::string& username, const AuthConfig& config,
                                  std::atomic<bool>* cancel_signal) {
  if (!camera_session_) {
    camera_session_ = openMainSession();
  }
  if (!camera_session_ || !camera_session_->isOpen()) {
    spdlog::error("FaceAuth: Could not open camera");
    if (!checkCameraAvailability(face_config_.camera)) {
      return AuthResult::Unavailable;
    }
    return AuthResult::Retry;
  }

  std::vector<std::string> allEnrolled = biopass::listFaces(username);
  if (allEnrolled.empty()) {
    spdlog::error("FaceAuth: No face enrolled for user {}, skipping", username);
    return AuthResult::Unavailable;
  }
  // RGB and IR images of the same face do not match reliably, so only compare
  // against enrolments captured by the same kind of sensor as the active
  // camera. Untagged (older) enrolments are kept for backwards compatibility.
  const SensorKind activeSensor =
      camera_session_->isGrey() ? SensorKind::Ir : SensorKind::Rgb;
  std::vector<std::string> enrolledFaces;
  for (const auto& path : allEnrolled) {
    const SensorKind kind = sensorKindOfEnrolment(path);
    if (kind == SensorKind::Unknown || kind == activeSensor) {
      enrolledFaces.push_back(path);
    }
  }
  if (enrolledFaces.empty()) {
    spdlog::warn(
        "FaceAuth: {} enrolment(s) exist for user {} but none captured with an {} camera; "
        "re-enrol with the currently selected camera, skipping",
        allEnrolled.size(), username, sensorTag(activeSensor));
    return AuthResult::Unavailable;
  }
  spdlog::debug("FaceAuth: Active sensor '{}' -> {} of {} enrolment(s) eligible",
                sensorTag(activeSensor), enrolledFaces.size(), allEnrolled.size());

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
  const ImageRGB& recogFace = detectedImages[0].recognitionFace();
  spdlog::debug("FaceAuth: Recognition input {}", detectedImages[0].aligned.empty()
                                                     ? "= raw crop (no landmarks)"
                                                     : "= 5-point aligned 112x112");

  // When the IR sensor is also the main camera (Windows-Hello style: enrol and
  // recognise on IR), libcamera cannot acquire the same device twice, so the
  // presence check reuses the already-open main session.
  const bool ir_is_main_camera = irIsMainCamera();
  ICameraCaptureSession* ir_session = nullptr;
  if (ir_is_main_camera) {
    spdlog::debug("FaceAuth: IR camera is the main camera; reusing main session for presence check");
    ir_session = camera_session_.get();
  } else {
    ensureIrSession();
    ir_session = ir_camera_session_.get();
  }

  if (!checkAntiSpoof(face_config_, username, face, config, model_registry_, detector_.get(),
                      ir_session)) {
    spdlog::warn("FaceAuth: Anti-spoofing failed — returning Failure (no retry allowed)");
    // Always tear down the IR session so a subsequent call cannot reuse a
    // partially-warmed camera to bypass the check.
    ir_camera_session_.reset();
    if (ir_is_main_camera) camera_session_.reset();
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

    MatchResult match = recognizer_->match(preparedFace, recogFace);
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

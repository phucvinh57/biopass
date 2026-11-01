#include "face_auth.h"

#include <spdlog/spdlog.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "antispoof_check.h"
#include "auth_deadline.h"
#include "camera_capture.h"
#include "debug_image_io.h"
#include "frame_darkness.h"
#include "image_io.h"
#include "onnx_session.h"
#include "status_reporter.h"
#include "user_file.h"

namespace biopass {

// A colour capture already waiting when the run is cancelled can't be
// interrupted. It must still end, with teardown, before the PAM module kills
// the helper, or a result reached within the budget could be lost.
static_assert(kAuthBudget + std::chrono::milliseconds(kColorCaptureTimeoutMs) + kTeardownMargin <=
                  kHelperTimeout,
              "kAuthBudget leaves no room for an in-flight capture before kHelperTimeout");

namespace {

// Builds a face engine from the user's model file, as the user. nullptr,
// logged, if that throws.
template <typename Engine, typename... Args>
std::unique_ptr<Engine> loadModel(const char* what, const std::string& username,
                                  const std::string& path, Args&&... args) {
  try {
    return makeEngineAsUser<Engine>(username, path, std::forward<Args>(args)...);
  } catch (const std::exception& e) {
    spdlog::error("FaceAuth: Failed to load {} model: {}, skipping", what, firstLine(e.what()));
    return nullptr;
  }
}

}  // namespace

bool FaceAuth::isAvailable() const { return checkCameraAvailability(face_config_.camera); }

// A failed open leaves ir_camera_session_ null; checkAntiSpoof() then falls
// back to a one-shot capture per attempt.
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
  if (!detectModel || !recogModel) {
    spdlog::error("FaceAuth: Models not found in the registry");
    return false;
  }

  // Model paths come from the user's registry: load them as the user.
  detector_ = loadModel<FaceDetection>("detection", username_, detectModel->path, 640,
                                       face_config_.detection.threshold);
  if (!detector_) {
    return false;
  }
  spdlog::debug("FaceAuth: Detection model loaded | threshold={:.3f}",
                face_config_.detection.threshold);

  recognizer_ = loadModel<FaceRecognition>("recognition", username_, recogModel->path, 112,
                                           face_config_.recognition.threshold);
  if (!recognizer_) {
    detector_.reset();
    return false;
  }
  spdlog::debug("FaceAuth: Recognition model loaded | threshold={:.3f}",
                face_config_.recognition.threshold);

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

// ONNX Runtime, the image code (std::out_of_range, bad_alloc) and the
// recognizer (zero-magnitude embedding) can throw. An attempt that threw is a
// failed attempt, never a match. The camera sessions are dropped since the
// throw may have left them mid-capture.
AuthResult FaceAuth::authenticate(const std::string& username, const AuthConfig& config,
                                  std::atomic<bool>* cancel_signal) {
  try {
    return attempt(username, config, cancel_signal);
  } catch (const std::exception& e) {
    spdlog::error("FaceAuth: attempt threw, failing it: {}", e.what());
  } catch (...) {
    spdlog::error("FaceAuth: attempt threw, failing it");
  }
  ir_camera_session_.reset();
  camera_session_.reset();
  return AuthResult::Failure;
}

AuthResult FaceAuth::attempt(const std::string& username, const AuthConfig& config,
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

  // The capture may have run past a sibling's success or the time budget;
  // don't open the IR camera or match faces for a run that is over.
  if (cancel_signal && cancel_signal->load()) {
    return AuthResult::Failure;
  }

  ensureIrSession();

  if (!checkAntiSpoof(face_config_, username, loginFace, detectedImages[0].box, config,
                      model_registry_, detector_.get(), ir_camera_session_.get(), cancel_signal)) {
    spdlog::warn("FaceAuth: Anti-spoofing failed — returning Failure (no retry allowed)");
    // Always tear down the IR session so a subsequent call cannot reuse a
    // partially-warmed camera to bypass the check.
    ir_camera_session_.reset();
    return AuthResult::Failure;
  }

  if (matchAgainstEnrolled(username, face, enrolledFaces)) {
    return AuthResult::Success;
  }

  if (config.debug) {
    saveFailedFace(username, face, "not_similar");
  }

  return AuthResult::Retry;
}

// Unreadable enrolled images are skipped, never matched.
bool FaceAuth::matchAgainstEnrolled(const std::string& username, const ImageRGB& face,
                                    const std::vector<std::string>& enrolled_paths) {
  spdlog::debug("FaceAuth: Recognition | threshold={:.3f} enrolled_count={}",
                face_config_.recognition.threshold, enrolled_paths.size());
  for (const auto& facePath : enrolled_paths) {
    const auto faceBytes = readUserFile(username, facePath, kMaxImageFileBytes);
    ImageRGB preparedFace = faceBytes ? decodeImage(*faceBytes) : ImageRGB{};
    if (preparedFace.empty()) {
      spdlog::warn("FaceAuth: Recognition | could not load enrolled image: {}", facePath);
      continue;
    }

    MatchResult match = recognizer_->match(preparedFace, face);
    spdlog::debug("FaceAuth: Recognition | face='{}' score={:.4f} threshold={:.3f} similar={}",
                  facePath, match.similarity, face_config_.recognition.threshold, match.similar);
    if (match.similar) {
      spdlog::debug("FaceAuth: Recognition PASSED | matched face='{}' score={:.4f}", facePath,
                    match.similarity);
      return true;
    }
  }
  return false;
}

}  // namespace biopass

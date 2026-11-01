#include "ir_camera_as.h"

#include <spdlog/spdlog.h>

#include <chrono>

#include "auth_deadline.h"
#include "camera_capture.h"
#include "debug_image_io.h"
#include "face_detection.h"

namespace biopass {

bool checkAntispoofByIRCamera(const std::string& device_path, FaceDetection* detector,
                              const std::string& username, bool debug,
                              ICameraCaptureSession* session, int warmup_delay_ms,
                              int presence_timeout_ms, const std::atomic<bool>* cancel) {
  spdlog::debug(
      "FaceAuth: IR presence check | device='{}' warmup_delay_ms={} presence_timeout_ms={}",
      device_path, warmup_delay_ms, presence_timeout_ms);

  if (device_path.empty()) {
    spdlog::error("FaceAuth: IR presence check skipped — device path is empty");
    return false;
  }

  if (warmup_delay_ms > 0) {
    spdlog::debug("FaceAuth: IR presence check — sleeping {}ms for camera stabilisation",
                  warmup_delay_ms);
    if (!sleepUnlessCancelled(std::chrono::milliseconds(warmup_delay_ms), cancel)) {
      spdlog::debug("FaceAuth: IR presence check cancelled during warmup");
      return false;
    }
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(std::max(0, presence_timeout_ms));

  ImageRGB last_frame;
  int attempt = 0;
  do {
    if (cancel != nullptr && cancel->load()) {
      spdlog::debug("FaceAuth: IR presence check cancelled after {} attempt(s)", attempt);
      return false;
    }
    ++attempt;

    ImageRGB frame;
    if (session && session->isOpen()) {
      spdlog::debug("FaceAuth: IR presence check — attempt {} capturing from existing open session",
                    attempt);
      frame = session->capture();
    } else if (session) {
      // The session was open at the start of the retry loop but a prior capture
      // timed out and tore it down; there is nothing left to retry against.
      spdlog::error("FaceAuth: IR presence check — session closed, aborting retries (device='{}')",
                    device_path);
      break;
    } else {
      spdlog::debug("FaceAuth: IR presence check — attempt {} opening new session on '{}'", attempt,
                    device_path);
      frame = captureImageByIRCamera(device_path);
    }

    if (frame.empty()) {
      spdlog::debug("FaceAuth: IR presence check — attempt {} frame capture failed from '{}'",
                    attempt, device_path);
      continue;
    }

    spdlog::debug("FaceAuth: IR presence check — attempt {} frame captured ({}x{})", attempt,
                  frame.width, frame.height);
    last_frame = frame;

    try {
      std::vector<Detection> detections = detector->inference(frame);

      // TODO: presence check only, not liveness (see header); needs a real IR
      // liveness model.
      if (detections.empty()) {
        spdlog::debug(
            "FaceAuth: IR presence check — attempt {} no face bounding box detected (device='{}')",
            attempt, device_path);
        continue;
      }

      // Log every detection so the caller can see confidence vs threshold.
      for (size_t i = 0; i < detections.size(); ++i) {
        spdlog::debug("FaceAuth: IR presence check — detection[{}] conf={:.4f}", i,
                      detections[i].conf);
      }

      spdlog::debug(
          "FaceAuth: IR presence check PASSED — attempt {}, {} face(s) detected, best conf={:.4f} "
          "(NOTE: presence check only, not liveness)",
          attempt, detections.size(), detections[0].conf);
      return true;
    } catch (const std::exception& e) {
      spdlog::error("FaceAuth: IR presence check — exception during detection: {}", e.what());
      return false;
    }
  } while (std::chrono::steady_clock::now() < deadline);

  spdlog::error(
      "FaceAuth: IR presence check FAILED — no face bounding box detected after {} attempt(s) "
      "(device='{}')",
      attempt, device_path);
  if (debug && !last_frame.empty()) {
    saveFailedFace(username, last_frame, "ir_no_face");
  }
  return false;
}

}  // namespace biopass

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "image_utils.h"

namespace biopass {

enum class CameraCaptureFormat {
  Default,   // Preference order: MJPEG -> YUYV -> R8 (grey).
  V4L2Grey,  // IR sensors. Preference order: R8 (grey) -> YUYV -> MJPEG,
             // since some IR sensors (e.g. Windows Hello cameras) only
             // expose the stream as YUYV/MJPEG.
};

// Shared tuning for IR (V4L2Grey) camera sessions: a short warmup and a
// shorter timeout than the default color capture, since IR sensors settle
// faster and the anti-spoofing check should fail fast rather than block login.
// Each grey capture then samples kIrCaptureSampleFrames consecutive frames and
// keeps the brightest one that isn't over-exposed, so a strobing emitter
// (lit/dark/washed-out cycle) yields a usable frame whenever one was sampled.
// Total frames per capture stay close to the old 5 + 1.
constexpr int kIrCaptureWarmupFrames = 3;
constexpr int kIrCaptureSampleFrames = 4;
constexpr int kIrCaptureTimeoutMs = 3000;

class ICameraCaptureSession {
 public:
  virtual ~ICameraCaptureSession() = default;
  virtual bool isOpen() const = 0;
  virtual ImageRGB capture() = 0;
};

bool checkCameraAvailability(const std::optional<std::string>& device_path);
std::unique_ptr<ICameraCaptureSession> openCameraSession(
    const std::optional<std::string>& device_path,
    CameraCaptureFormat format = CameraCaptureFormat::Default, int warmup_frames = 5,
    int capture_timeout_ms = 10000);
ImageRGB captureImage(const std::optional<std::string>& device_path,
                      CameraCaptureFormat format = CameraCaptureFormat::Default);
ImageRGB captureImageByIRCamera(const std::string& device_path,
                                int warmup_frames = kIrCaptureWarmupFrames,
                                int capture_timeout_ms = kIrCaptureTimeoutMs);

// Introspection helpers used by camera_capture_test (field debugging).
struct CameraDeviceInfo {
  std::string id;
  std::string model;
  std::vector<std::string> video_paths;
};
std::vector<CameraDeviceInfo> listCameraDevices();

struct CameraFormatDesc {
  std::string pixel_format;
  std::vector<std::pair<int, int>> sizes;
  bool supported = false;
};
std::vector<CameraFormatDesc> listCameraFormats(const std::string& device_path);

}  // namespace biopass

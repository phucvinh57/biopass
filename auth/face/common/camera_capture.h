#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "image_utils.h"

namespace biopass {

enum class CameraCaptureFormat {
  Default,   // MJPEG -> YUYV -> R8
  V4L2Grey,  // IR: R8 -> YUYV -> MJPEG (some IR sensors lack R8)
};

// IR sessions use a short warmup and timeout: IR sensors settle faster and
// the anti-spoofing check should fail fast rather than block login. Each
// capture keeps the brightest non-over-exposed of kIrCaptureSampleFrames
// frames, so a strobing emitter still yields a usable frame.
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
ImageRGB captureImageByIRCamera(const std::string& device_path,
                                int warmup_frames = kIrCaptureWarmupFrames,
                                int capture_timeout_ms = kIrCaptureTimeoutMs);

// Used by camera_capture_test for field debugging.
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

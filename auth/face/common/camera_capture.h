#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "image_utils.h"

namespace biopass {

enum class CameraCaptureFormat {
  Default,   // Preference order: YUYV -> MJPEG -> R8 (grey).
  V4L2Grey,  // IR sensors. Preference order: R8 (grey) -> YUYV -> MJPEG,
             // since some IR sensors (e.g. Windows Hello cameras) only
             // expose the stream as YUYV/MJPEG.
};

// How a session warms up and samples frames. Colour and IR sensors need
// different treatment, so the profile follows the negotiated stream (or the
// caller's explicit choice, e.g. the user's IR settings from the UI).
struct CaptureProfile {
  int max_warmup_ms;      // upper bound on warm-up; colour sessions stop earlier once
                          // auto-exposure has converged (frame brightness stable)
  int min_warmup_frames;  // always discard at least this many frames
  int brightest_of;       // sample N frames per capture and keep the brightest
  int capture_timeout_ms;
};

// RGB sensors: wait for auto-exposure to converge. Detected from the frames
// themselves (no light sensor needed): warm-up ends when the mean brightness
// of consecutive frames stops changing, or at max_warmup_ms. In daylight that
// is a few frames; in a dim room a slow USB 2.0 webcam needs most of the cap.
// Once per session; a single frame is captured.
constexpr CaptureProfile kColourCaptureProfile{2000, 3, 1, 10000};

// IR sensors: fixed short settle after the emitter comes on (brightness is
// not a convergence signal here because many emitters strobe, alternating
// bright/dark frames) and keep the brightest of a few frames per capture.
// Warm-up happens on every capture. Short timeout so a failing anti-spoof
// check does not block login.
constexpr CaptureProfile kIrCaptureProfile{300, 3, 4, 3000};

class ICameraCaptureSession {
 public:
  virtual ~ICameraCaptureSession() = default;
  virtual bool isOpen() const = 0;
  virtual ImageRGB capture() = 0;
  // True for grey/IR streams (negotiated R8 or requested V4L2Grey).
  virtual bool isGrey() const = 0;
};

// Enrolled faces are tagged with the sensor they were captured by, as
// `face_<ts>.ir.jpg` / `face_<ts>.rgb.jpg`, because RGB and IR images of the
// same person do not match reliably. Untagged files (older enrolments) are
// treated as matching any sensor.
enum class SensorKind { Unknown, Rgb, Ir };
const char* sensorTag(SensorKind kind);                 // "rgb" / "ir" / ""
SensorKind sensorKindOfEnrolment(const std::string& path);  // from the file name
// Inserts the tag before the extension unless the path is already tagged.
std::string tagEnrolmentPath(const std::string& path, SensorKind kind);

bool checkCameraAvailability(const std::optional<std::string>& device_path);
// `profile` overrides the automatic choice (kIrCaptureProfile for a grey
// stream, kColourCaptureProfile otherwise).
std::unique_ptr<ICameraCaptureSession> openCameraSession(
    const std::optional<std::string>& device_path,
    CameraCaptureFormat format = CameraCaptureFormat::Default,
    std::optional<CaptureProfile> profile = std::nullopt);
ImageRGB captureImage(const std::optional<std::string>& device_path,
                      CameraCaptureFormat format = CameraCaptureFormat::Default);
ImageRGB captureImageByIRCamera(const std::string& device_path,
                                const CaptureProfile& profile = kIrCaptureProfile);

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

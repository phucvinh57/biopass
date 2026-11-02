#pragma once

#include <functional>
#include <string>

#include "auth_config.h"

namespace biopass {

// Whether `path` names a character device. stat() follows symlinks, so a
// /dev/v4l/by-id or by-path link counts as its /dev/videoN.
bool isCharDevice(const std::string& path);

using CameraExists = std::function<bool(const std::string& path)>;

// The colour + IR camera pair a session uses.
// - No camera_profiles: `camera` and `anti_spoofing.ir_camera` (legacy).
// - Otherwise the first profile whose camera exists. A profile with no
//   camera means auto-select and always matches. Its IR camera is its own
//   ir_camera, else the fallback IR camera unless allow_without_ir is set.
// - No match: auto-select the camera, with the fallback IR camera.
// The fallback IR camera is `anti_spoofing.ir_camera`, else the first
// ir_camera set in any profile. A device that merely exists (any USB device
// can claim a by-path port) thus never turns the IR check off unless the
// user allowed it for that profile. A missing IR device fails the IR check,
// so face fails closed.
CameraProfile pickCameraProfile(const FaceMethodConfig& face,
                                const CameraExists& exists = isCharDevice);

// `face` with the picked profile in `camera` and `anti_spoofing.ir_camera`.
FaceMethodConfig withCameraProfile(FaceMethodConfig face,
                                   const CameraExists& exists = isCharDevice);

}  // namespace biopass

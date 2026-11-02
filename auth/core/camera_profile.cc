#include "camera_profile.h"

#include <spdlog/spdlog.h>
#include <sys/stat.h>

#include <optional>
#include <utility>

namespace biopass {

namespace {

bool isSet(const std::optional<std::string>& path) { return path.has_value() && !path->empty(); }

// anti_spoofing.ir_camera, else the first ir_camera set in any profile.
std::optional<std::string> fallbackIrCamera(const FaceMethodConfig& face) {
  if (isSet(face.anti_spoofing.ir_camera)) {
    return face.anti_spoofing.ir_camera;
  }
  for (const CameraProfile& profile : face.camera_profiles) {
    if (isSet(profile.ir_camera)) {
      return profile.ir_camera;
    }
  }
  return std::nullopt;
}

}  // namespace

bool isCharDevice(const std::string& path) {
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && S_ISCHR(info.st_mode);
}

CameraProfile pickCameraProfile(const FaceMethodConfig& face, const CameraExists& exists) {
  if (face.camera_profiles.empty()) {
    return {face.camera, face.anti_spoofing.ir_camera};
  }
  for (const CameraProfile& profile : face.camera_profiles) {
    if (isSet(profile.camera) && !exists(*profile.camera)) {
      continue;
    }
    CameraProfile picked = profile;
    // "" would be looked up as a path; nullopt auto-selects.
    if (!isSet(picked.camera)) {
      picked.camera = std::nullopt;
    }
    if (!isSet(picked.ir_camera)) {
      picked.ir_camera = picked.allow_without_ir ? std::nullopt : fallbackIrCamera(face);
    }
    return picked;
  }
  // Nothing connected: auto-select, never dropping the IR check.
  return {std::nullopt, fallbackIrCamera(face)};
}

FaceMethodConfig withCameraProfile(FaceMethodConfig face, const CameraExists& exists) {
  if (face.camera_profiles.empty()) {
    return face;
  }
  CameraProfile picked = pickCameraProfile(face, exists);
  spdlog::debug("FaceAuth: camera profile | camera='{}' ir_camera='{}'",
                picked.camera.value_or("auto"), picked.ir_camera.value_or(""));
  face.camera = std::move(picked.camera);
  face.anti_spoofing.ir_camera = std::move(picked.ir_camera);
  return face;
}

}  // namespace biopass

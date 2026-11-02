// Checks pickCameraProfile (core/camera_profile.h) against a fake list of
// connected cameras, and isCharDevice against real paths. Exits non-zero on
// any failure.

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "auth_config.h"
#include "camera_profile.h"

namespace {

using biopass::CameraProfile;
using biopass::FaceMethodConfig;

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

// An "exists" predicate for the given connected paths.
biopass::CameraExists connected(std::vector<std::string> paths) {
  return [paths](const std::string& path) {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
  };
}

bool same(const CameraProfile& a, std::optional<std::string> camera,
          std::optional<std::string> ir_camera) {
  return a.camera == camera && a.ir_camera == ir_camera;
}

const std::string kDock = "/dev/v4l/by-id/usb-Dock_Cam-video-index0";
const std::string kLid = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:8:1.0-video-index0";
const std::string kLidIr = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:8:1.2-video-index0";
const std::string kLegacyIr = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:9:1.2-video-index0";

CameraProfile profile(std::optional<std::string> camera, std::optional<std::string> ir_camera,
                      bool allow_without_ir = false) {
  return {std::move(camera), std::move(ir_camera), allow_without_ir};
}

// Dock camera (no IR of its own) first, then the lid camera with its IR.
FaceMethodConfig dockThenLid() {
  FaceMethodConfig face;
  face.camera = "/dev/video9";  // legacy colour camera, unused once profiles are set
  face.camera_profiles = {profile(kDock, std::nullopt), profile(kLid, kLidIr)};
  return face;
}

}  // namespace

int main() {
  {
    FaceMethodConfig face;
    face.camera = "/dev/video0";
    face.anti_spoofing.ir_camera = "/dev/video2";
    expect(same(biopass::pickCameraProfile(face, connected({})), "/dev/video0", "/dev/video2"),
           "no profiles: legacy camera + ir_camera, even when missing");
    expect(same(biopass::pickCameraProfile(FaceMethodConfig{}, connected({})), std::nullopt,
                std::nullopt),
           "no profiles, no legacy: auto-select, no IR");
  }

  const FaceMethodConfig face = dockThenLid();
  expect(same(biopass::pickCameraProfile(face, connected({kLid, kLidIr})), kLid, kLidIr),
         "undocked: the lid profile, with its own IR");
  expect(same(biopass::pickCameraProfile(face, connected({kLid})), kLid, kLidIr),
         "IR presence is not checked here: a missing IR fails the IR check later");
  expect(same(biopass::pickCameraProfile(face, connected({kDock, kLid, kLidIr})), kDock, kLidIr),
         "a no-IR profile inherits the fallback IR (first profile IR)");

  {
    FaceMethodConfig f = face;
    f.anti_spoofing.ir_camera = kLegacyIr;
    expect(same(biopass::pickCameraProfile(f, connected({kDock})), kDock, kLegacyIr),
           "the legacy ir_camera is the fallback once profiles exist");
    expect(same(biopass::pickCameraProfile(f, connected({kLid})), kLid, kLidIr),
           "a profile's own IR wins over the fallback");
    expect(same(biopass::pickCameraProfile(f, connected({})), std::nullopt, kLegacyIr),
           "none connected: auto-select with the legacy IR");
  }

  {
    FaceMethodConfig f = face;
    f.anti_spoofing.ir_camera = kLegacyIr;
    f.camera_profiles[0].allow_without_ir = true;
    expect(same(biopass::pickCameraProfile(f, connected({kDock})), kDock, std::nullopt),
           "allow_without_ir drops IR for that profile only");
    expect(same(biopass::pickCameraProfile(f, connected({})), std::nullopt, kLegacyIr),
           "allow_without_ir does not apply to the no-match fallback");
    f.camera_profiles[0].ir_camera = kLidIr;
    expect(same(biopass::pickCameraProfile(f, connected({kDock})), kDock, kLidIr),
           "allow_without_ir is ignored when the profile has its own IR");
  }

  expect(same(biopass::pickCameraProfile(face, connected({})), std::nullopt, kLidIr),
         "none connected: auto-select, first IR set in any profile");

  {
    FaceMethodConfig f = face;
    f.camera_profiles = {profile(kDock, std::nullopt)};
    expect(same(biopass::pickCameraProfile(f, connected({kDock})), kDock, std::nullopt),
           "no IR anywhere: none to inherit");
    expect(same(biopass::pickCameraProfile(f, connected({})), std::nullopt, std::nullopt),
           "none connected, no IR anywhere: auto-select, no IR");
  }

  {
    // What "Add profile" appends: no camera, no IR.
    FaceMethodConfig f;
    f.anti_spoofing.ir_camera = kLegacyIr;
    f.camera_profiles = {profile(kDock, kLidIr), profile(std::nullopt, std::nullopt)};
    expect(same(biopass::pickCameraProfile(f, connected({kLid})), std::nullopt, kLegacyIr),
           "an auto-select catch-all keeps the fallback IR");
    f.camera_profiles[1].camera = "";
    expect(same(biopass::pickCameraProfile(f, connected({kLid})), std::nullopt, kLegacyIr),
           "an empty camera counts as no camera (auto-select)");
    f.camera_profiles[1].ir_camera = "";
    f.camera_profiles[1].allow_without_ir = true;
    expect(same(biopass::pickCameraProfile(f, connected({kLid})), std::nullopt, std::nullopt),
           "an empty ir_camera counts as none (allow_without_ir drops IR)");
  }

  {
    const FaceMethodConfig applied = biopass::withCameraProfile(face, connected({kDock}));
    expect(applied.camera == kDock && applied.anti_spoofing.ir_camera == kLidIr,
           "withCameraProfile writes camera and anti_spoofing.ir_camera");
    FaceMethodConfig legacy;
    legacy.camera = "/dev/video0";
    const FaceMethodConfig unchanged = biopass::withCameraProfile(legacy, connected({}));
    expect(unchanged.camera == "/dev/video0" && !unchanged.anti_spoofing.ir_camera,
           "withCameraProfile leaves a legacy config alone");
  }

  expect(biopass::isCharDevice("/dev/null"), "isCharDevice: /dev/null");
  expect(!biopass::isCharDevice("/dev"), "isCharDevice: a directory is not");
  expect(!biopass::isCharDevice("/nonexistent/video0"), "isCharDevice: a missing path is not");

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

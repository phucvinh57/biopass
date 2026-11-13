// FaceAuth (face/face_auth.h) opens the colour camera once per auth session:
// the session opened to check availability is the one the attempts capture
// from. When an attempt can't open the camera, one more open still decides
// Unavailable vs Retry, and a session it gets is kept for the next attempt.
// When the IR camera is the main camera, the IR presence check reuses the
// main session instead of opening the device a second time.
// Uses fake cameras and a tiny constant ONNX model; no hardware or user data is needed.
// Exits non-zero on any failure.

#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "camera_capture.h"
#include "face_auth.h"
#include "user_paths.h"

namespace {

int g_failures = 0;
std::string g_test_home;
int g_ir_fallback_opens = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

constexpr const char* kUser = "biopass-no-such-user";

enum class Open { Ok, Fail, Dead };

struct CameraLog {
  int opens = 0;
  int closes = 0;
  int captures = 0;
  bool return_frame = false;
  // Sessions alive right now, and the most alive when an open started.
  int live = 0;
  int max_live_at_open = 0;
  // Result of each open, in order: an open session, nullptr, or a session
  // that is not open. Opens past the end succeed.
  std::deque<Open> results;
  // Each open's path, format and timeout, and the last session handed out.
  std::vector<std::string> paths;
  std::vector<biopass::CameraCaptureFormat> formats;
  std::vector<int> timeouts;
  biopass::ICameraCaptureSession* last = nullptr;
};

class FakeSession : public biopass::ICameraCaptureSession {
 public:
  FakeSession(CameraLog& log, bool open) : log_(log), open_(open) { log_.live++; }
  ~FakeSession() override {
    log_.closes++;
    log_.live--;
  }
  bool isOpen() const override { return open_; }
  ImageRGB capture() override {
    ++log_.captures;
    return log_.return_frame ? ImageRGB(8, 8) : ImageRGB{};
  }

 private:
  CameraLog& log_;
  bool open_;
};

biopass::FaceAuth::CameraOpener fakeOpener(CameraLog& log) {
  return [&log](const std::optional<std::string>& path, biopass::CameraCaptureFormat format, int,
                int timeout_ms) -> std::unique_ptr<biopass::ICameraCaptureSession> {
    log.opens++;
    log.max_live_at_open = std::max(log.max_live_at_open, log.live);
    log.paths.push_back(path.value_or("<default>"));
    log.formats.push_back(format);
    log.timeouts.push_back(timeout_ms);
    Open result = Open::Ok;
    if (!log.results.empty()) {
      result = log.results.front();
      log.results.pop_front();
    }
    if (result == Open::Fail) {
      log.last = nullptr;
      return nullptr;
    }
    auto session = std::make_unique<FakeSession>(log, result == Open::Ok);
    log.last = session.get();
    return session;
  };
}

// AuthManager's order: isAvailable(), then begin, attempts, end.
void checkAvailabilitySessionIsReused() {
  CameraLog log;
  {
    biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
    expect(face.isAvailable(), "camera present: available");
    expect(log.opens == 1, "availability check opens the camera once");
    expect(log.closes == 0, "availability check keeps the session open");

    face.beginAuthenticationSession();
    expect(log.opens == 1, "session start reuses the availability session (opens=" +
                               std::to_string(log.opens) + ")");

    face.endAuthenticationSession();
    expect(log.closes == 1, "session end closes the camera");
  }
  expect(log.opens == 1 && log.closes == 1, "one open, one close for the whole session");
}

void checkMissingCamera() {
  CameraLog log;
  log.results = {Open::Fail};
  biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
  expect(!face.isAvailable(), "camera missing: not available");
  expect(log.opens == 1, "camera missing: one open attempt");
}

// An attempt that can't open the camera: a second open that fails means the
// camera is gone (Unavailable), as before.
void checkAttemptOpenFailsTwice() {
  CameraLog log;
  log.results = {Open::Fail, Open::Fail};
  biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
  const auto result = face.authenticate(kUser, biopass::AuthConfig{});
  expect(result == biopass::AuthResult::Unavailable, "both opens fail: Unavailable");
  expect(log.opens == 2, "both opens fail: two open attempts");
}

// A second open that works means a passing glitch (Retry), as before; the
// session it opened is kept, so the next attempt doesn't open again.
void checkAttemptOpenRecovers() {
  CameraLog log;
  log.results = {Open::Fail, Open::Ok};
  biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
  auto result = face.authenticate(kUser, biopass::AuthConfig{});
  expect(result == biopass::AuthResult::Retry, "first open fails, second works: Retry");
  expect(log.opens == 2, "first open fails, second works: two opens");
  expect(log.closes == 0, "the working session is kept");

  // The unknown user has no enrolled face, so this attempt stops there, after
  // the camera step.
  result = face.authenticate(kUser, biopass::AuthConfig{});
  expect(result == biopass::AuthResult::Unavailable, "next attempt: no enrolled face");
  expect(log.opens == 2,
         "next attempt reuses the kept session (opens=" + std::to_string(log.opens) + ")");
}

// A session that came back not open is freed before the camera is opened
// again, so two sessions never hold the device at once.
void checkDeadSessionFreedBeforeReopen() {
  CameraLog log;
  log.results = {Open::Dead, Open::Dead};
  biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
  expect(!face.isAvailable(), "dead session: not available");
  expect(log.live == 0, "dead session from the availability check is freed");

  log.results = {Open::Dead, Open::Ok};
  const auto result = face.authenticate(kUser, biopass::AuthConfig{});
  expect(result == biopass::AuthResult::Retry, "dead session, then a working one: Retry");
  expect(log.max_live_at_open == 0, "no session alive when the camera is opened again (max=" +
                                        std::to_string(log.max_live_at_open) + ")");
}

biopass::FaceMethodConfig withCameras(const std::string& camera, const std::string& ir_camera) {
  biopass::FaceMethodConfig config;
  config.camera = camera;
  config.anti_spoofing.ir_camera = ir_camera;
  return config;
}

// No IR camera configured: no presence session, no extra open; the main
// camera opens as colour.
void checkNoIrCamera() {
  CameraLog log;
  biopass::FaceMethodConfig config;
  config.camera = "/dev/fake-rgb";
  biopass::FaceAuth face(config, kUser, fakeOpener(log));
  expect(face.isAvailable(), "no IR camera: available");
  expect(face.irPresenceSession() == nullptr, "no IR camera: no presence session");
  expect(log.opens == 1 && log.formats[0] == biopass::CameraCaptureFormat::Default,
         "no IR camera: main camera opened once, as colour");
}

// The IR camera is the main camera: libcamera can't acquire it twice, so the
// presence check captures from the main session.
void checkIrIsMainCameraReused(const std::string& camera, const std::string& ir_camera,
                               const std::string& label) {
  CameraLog log;
  biopass::FaceAuth face(withCameras(camera, ir_camera), kUser, fakeOpener(log));
  expect(face.isAvailable(), label + ": available");
  biopass::ICameraCaptureSession* main_session = log.last;
  expect(log.opens == 1 && log.formats[0] == biopass::CameraCaptureFormat::V4L2Grey,
         label + ": main camera opened once, as grey");
  expect(log.timeouts[0] == biopass::kColorCaptureTimeoutMs,
         label + ": main camera keeps the colour timeout");

  expect(face.irPresenceSession() == main_session,
         label + ": presence check reuses the main session");
  expect(face.irPresenceSession() == main_session, label + ": and again on the next attempt");
  expect(log.opens == 1,
         label + ": the device is never opened twice (opens=" + std::to_string(log.opens) + ")");

  face.endAuthenticationSession();
  expect(log.closes == 1 && log.live == 0, label + ": session end closes the one session");
}

// Different cameras: the IR session is a second, grey session, opened once
// and reused while it stays open.
void checkSeparateIrCamera() {
  CameraLog log;
  biopass::FaceAuth face(withCameras("/dev/fake-rgb", "/dev/fake-ir"), kUser, fakeOpener(log));
  expect(face.isAvailable(), "separate IR camera: available");
  biopass::ICameraCaptureSession* main_session = log.last;
  expect(log.formats[0] == biopass::CameraCaptureFormat::Default,
         "separate IR camera: main camera opened as colour");

  biopass::ICameraCaptureSession* ir = face.irPresenceSession();
  expect(ir != nullptr && ir != main_session, "separate IR camera: its own session");
  expect(log.opens == 2 && log.paths[1] == "/dev/fake-ir" &&
             log.formats[1] == biopass::CameraCaptureFormat::V4L2Grey &&
             log.timeouts[1] == biopass::kIrCaptureTimeoutMs,
         "separate IR camera: opened as grey with the IR timeout");
  expect(face.irPresenceSession() == ir && log.opens == 2,
         "separate IR camera: reused on the next attempt");

  face.endAuthenticationSession();
  expect(log.live == 0, "separate IR camera: session end closes both");
}

// Two nodes can resolve to one libcamera object, as on the Logitech BRIO.
void checkSharedCameraClosesRgbBeforeIr(Open ir_result) {
  CameraLog log;
  log.results = {Open::Ok, ir_result};
  const auto shared = [](const std::optional<std::string>& main, const std::string& ir) {
    return main == "/dev/fake-rgb" && ir == "/dev/fake-ir";
  };
  biopass::FaceAuth face(withCameras("/dev/fake-rgb", "/dev/fake-ir"), kUser, fakeOpener(log),
                         shared);
  expect(face.isAvailable(), "shared camera: RGB is available");
  const auto* ir = face.irPresenceSession();
  expect((ir != nullptr) == (ir_result == Open::Ok), "shared camera: IR open result is preserved");
  expect(log.opens == 2 && log.paths[1] == "/dev/fake-ir" &&
             log.formats[1] == biopass::CameraCaptureFormat::DirectV4L2Grey,
         "shared camera: the IR open directly targets the configured grey node");
  expect(log.closes == 1 && log.max_live_at_open == 0,
         "shared camera: RGB is released before the IR open");
  face.endAuthenticationSession();
  expect(log.live == 0, "shared camera: session end releases any open IR session");
}

// Float input [1,3,640,640], constant output [1,5,1]: one confident face box.
// Both engines can load it; recognition must never run after the IR open fails.
constexpr char kFaceModel[] =
    "\x08\x08\x3a\x74\x12\x09\x66\x61\x63\x65\x2d\x74\x65\x73\x74\x2a\x26\x08\x01\x08"
    "\x05\x08\x01\x10\x01\x42\x06\x6f\x75\x74\x70\x75\x74\x4a\x14\x00\x00\xa0\x43\x00"
    "\x00\xa0\x43\x00\x00\xa0\x43\x00\x00\xa0\x43\x00\x00\x80\x3f\x5a\x21\x0a\x05\x69"
    "\x6e\x70\x75\x74\x12\x18\x0a\x16\x08\x01\x12\x12\x0a\x02\x08\x01\x0a\x02\x08\x03"
    "\x0a\x03\x08\x80\x05\x0a\x03\x08\x80\x05\x62\x1c\x0a\x06\x6f\x75\x74\x70\x75\x74"
    "\x12\x12\x0a\x10\x08\x01\x12\x0c\x0a\x02\x08\x01\x0a\x02\x08\x05\x0a\x02\x08\x01"
    "\x42\x02\x10\x0d";

void checkSharedIrOpenFailureAbortsAuthentication() {
  char dir_template[] = "/tmp/biopass-ir-failure-XXXXXX";
  const char* dir = mkdtemp(dir_template);
  expect(dir != nullptr, "IR failure: create isolated user home");
  if (!dir) {
    return;
  }
  g_test_home = dir;
  const auto config_dir = g_test_home + "/.config/com.ticklab.biopass";
  const auto faces_dir = g_test_home + "/.local/share/com.ticklab.biopass/faces";
  std::filesystem::create_directories(config_dir);
  std::filesystem::create_directories(faces_dir);
  const auto model_path = g_test_home + "/face.onnx";
  std::ofstream(model_path, std::ios::binary).write(kFaceModel, sizeof(kFaceModel) - 1);
  std::ofstream(faces_dir + "/enrolled.jpg") << "not read before the IR check";

  sqlite3* db = nullptr;
  const auto sql =
      "CREATE TABLE models (id TEXT, path TEXT, architecture TEXT);"
      "INSERT INTO models VALUES ('fixture', '" +
      model_path + "', NULL);";
  const bool ready = sqlite3_open((config_dir + "/biopass.db").c_str(), &db) == SQLITE_OK &&
                     sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close(db);
  expect(ready, "IR failure: create model registry");
  if (ready) {
    CameraLog log;
    log.results = {Open::Ok, Open::Fail};
    log.return_frame = true;
    auto config = withCameras("/dev/fake-rgb", "/dev/fake-ir");
    config.detection.model_id = config.recognition.model_id = "fixture";
    config.anti_spoofing.enable = false;
    config.anti_spoofing.ir_warmup_delay_ms = 0;
    config.anti_spoofing.ir_presence_timeout_ms = 0;
    biopass::FaceAuth face(config, kUser, fakeOpener(log),
                           [](const auto&, const auto&) { return true; });
    g_ir_fallback_opens = 0;
    expect(face.authenticate(kUser, biopass::AuthConfig{}) == biopass::AuthResult::Failure,
           "IR open failure: authentication fails closed");
    expect(log.captures == 1 && log.opens == 2 &&
               log.formats.back() == biopass::CameraCaptureFormat::DirectV4L2Grey,
           "IR open failure: reached the direct IR open after capturing RGB");
    expect(g_ir_fallback_opens == 0, "IR open failure: no permissive one-shot IR fallback");
    expect(log.live == 0, "IR open failure: no camera session remains open");
  }
  g_test_home.clear();
  std::filesystem::remove_all(dir);
}

// A /dev/v4l/by-id style symlink names the same device as its target.
void checkIrIsMainCameraBySymlink() {
  char dir_template[] = "/tmp/biopass-camera-reuse-XXXXXX";
  const char* dir = mkdtemp(dir_template);
  if (!dir) {
    expect(false, "symlinked IR camera: could not create a temp dir");
    return;
  }
  const std::string link = std::string(dir) + "/by-id-camera";
  if (symlink("/dev/null", link.c_str()) != 0) {
    expect(false, "symlinked IR camera: could not create the symlink");
    rmdir(dir);
    return;
  }
  checkIrIsMainCameraReused("/dev/null", link, "symlinked IR camera");
  unlink(link.c_str());
  rmdir(dir);
}

}  // namespace

// Test doubles: all user files live in the fixture, and a one-shot IR fallback is observable.
namespace biopass {
std::optional<UserPaths> resolveUser(const std::string& username) {
  if (g_test_home.empty() || username != kUser) {
    return std::nullopt;
  }
  UserPaths paths;
  paths.uid = geteuid();
  paths.gid = getegid();
  paths.home = g_test_home;
  paths.config_dir = g_test_home + "/.config/com.ticklab.biopass";
  paths.data_dir = g_test_home + "/.local/share/com.ticklab.biopass";
  return paths;
}

ImageRGB captureImageByIRCamera(const std::string&, int, int) {
  ++g_ir_fallback_opens;
  return {};
}
}  // namespace biopass

int main() {
  checkAvailabilitySessionIsReused();
  checkMissingCamera();
  checkAttemptOpenFailsTwice();
  checkAttemptOpenRecovers();
  checkDeadSessionFreedBeforeReopen();
  checkNoIrCamera();
  checkIrIsMainCameraReused("/dev/fake-cam", "/dev/fake-cam", "IR camera is the main camera");
  checkSeparateIrCamera();
  checkSharedCameraClosesRgbBeforeIr(Open::Ok);
  checkSharedCameraClosesRgbBeforeIr(Open::Fail);
  checkSharedIrOpenFailureAbortsAuthentication();
  checkIrIsMainCameraBySymlink();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

// FaceAuth (face/face_auth.h) opens the colour camera once per auth session:
// the session opened to check availability is the one the attempts capture
// from. When an attempt can't open the camera, one more open still decides
// Unavailable vs Retry, and a session it gets is kept for the next attempt.
// Uses a fake camera and an unknown user, so no camera or model is needed.
// Exits non-zero on any failure.

#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "camera_capture.h"
#include "face_auth.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

constexpr const char* kUser = "biopass-no-such-user";

struct CameraLog {
  int opens = 0;
  int closes = 0;
  // Result of each open, in order: true = an open session, false = nullptr.
  // Opens past the end succeed.
  std::deque<bool> results;
};

class FakeSession : public biopass::ICameraCaptureSession {
 public:
  explicit FakeSession(CameraLog& log) : log_(log) {}
  ~FakeSession() override { log_.closes++; }
  bool isOpen() const override { return true; }
  ImageRGB capture() override { return {}; }

 private:
  CameraLog& log_;
};

biopass::FaceAuth::CameraOpener fakeOpener(CameraLog& log) {
  return
      [&log](const std::optional<std::string>&) -> std::unique_ptr<biopass::ICameraCaptureSession> {
        log.opens++;
        bool ok = true;
        if (!log.results.empty()) {
          ok = log.results.front();
          log.results.pop_front();
        }
        if (!ok) {
          return nullptr;
        }
        return std::make_unique<FakeSession>(log);
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
  log.results = {false};
  biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
  expect(!face.isAvailable(), "camera missing: not available");
  expect(log.opens == 1, "camera missing: one open attempt");
}

// An attempt that can't open the camera: a second open that fails means the
// camera is gone (Unavailable), as before.
void checkAttemptOpenFailsTwice() {
  CameraLog log;
  log.results = {false, false};
  biopass::FaceAuth face(biopass::FaceMethodConfig{}, kUser, fakeOpener(log));
  const auto result = face.authenticate(kUser, biopass::AuthConfig{});
  expect(result == biopass::AuthResult::Unavailable, "both opens fail: Unavailable");
  expect(log.opens == 2, "both opens fail: two open attempts");
}

// A second open that works means a passing glitch (Retry), as before; the
// session it opened is kept, so the next attempt doesn't open again.
void checkAttemptOpenRecovers() {
  CameraLog log;
  log.results = {false, true};
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

}  // namespace

int main() {
  checkAvailabilitySessionIsReused();
  checkMissingCamera();
  checkAttemptOpenFailsTwice();
  checkAttemptOpenRecovers();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

// The anti-spoofing model is loaded once per FaceAuth session and reused by
// every attempt (face/common/session_engine.h), and a model that can't be
// loaded still fails the check closed (face/antispoofing/antispoof_check.h).
// Uses a fake engine and an empty model registry, so no model is needed.
// Exits non-zero on any failure.

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

#include "antispoof_check.h"
#include "face_as.h"
#include "face_detection.h"
#include "model_registry.h"
#include "session_engine.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

struct FakeEngine {
  int id;
};

void checkLoadsOnce() {
  biopass::SessionEngine<FakeEngine> engine;
  int loads = 0;
  const auto load = [&loads]() {
    ++loads;
    return std::make_unique<FakeEngine>(FakeEngine{loads});
  };

  FakeEngine* first = engine.get(load);
  FakeEngine* second = engine.get(load);
  FakeEngine* third = engine.get(load);
  expect(loads == 1, "three attempts load the engine once (" + std::to_string(loads) + ")");
  expect(first != nullptr && first == second && second == third,
         "every attempt gets the same engine");
  expect(engine.loaded(), "the loaded engine is kept");
}

void checkFailedLoadRetries() {
  biopass::SessionEngine<FakeEngine> engine;
  int loads = 0;
  const auto fail = [&loads]() -> std::unique_ptr<FakeEngine> {
    ++loads;
    return nullptr;
  };
  expect(engine.get(fail) == nullptr, "failed load: no engine");
  expect(engine.get(fail) == nullptr, "failed load again: no engine");
  expect(loads == 2, "a failed load is not kept: the next attempt loads again");
  expect(!engine.loaded(), "nothing kept after failed loads");

  bool threw = false;
  try {
    engine.get([]() -> std::unique_ptr<FakeEngine> { throw std::runtime_error("bad model"); });
  } catch (const std::runtime_error&) {
    threw = true;
  }
  expect(threw, "a throwing load propagates");
  expect(!engine.loaded(), "nothing kept after a throwing load");

  FakeEngine* ok = engine.get([]() { return std::make_unique<FakeEngine>(FakeEngine{7}); });
  expect(ok != nullptr && ok->id == 7, "a later successful load is kept");
}

// AI anti-spoofing on, IR off, and the model id is not in the registry: the
// check fails on every attempt and keeps no engine.
void checkMissingModelFailsClosed() {
  biopass::FaceMethodConfig face_config;
  face_config.anti_spoofing.enable = true;
  face_config.anti_spoofing.ir_camera.reset();
  face_config.anti_spoofing.model.model_id = "no-such-model";

  biopass::AuthConfig config;
  biopass::ModelRegistry registry("biopass-no-such-user");
  biopass::SessionEngine<biopass::FaceAntiSpoofing> ai_model;

  const ImageRGB frame;
  const biopass::Box box{0, 0, 4, 4};
  for (int attempt = 1; attempt <= 2; ++attempt) {
    const bool passed = biopass::checkAntiSpoof(face_config, "biopass-no-such-user", frame, box,
                                                config, registry, ai_model, nullptr);
    expect(!passed, "missing model fails the check (attempt " + std::to_string(attempt) + ")");
    expect(!ai_model.loaded(),
           "missing model keeps no engine (attempt " + std::to_string(attempt) + ")");
  }
}

}  // namespace

int main() {
  checkLoadsOnce();
  checkFailedLoadRetries();
  checkMissingModelFailsClosed();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}

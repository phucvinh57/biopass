// Pins which anti-spoofing checks a config turns on (core/auth_config.h):
// irCameraConfig() and aiAntiSpoofingModel() are the only places that decide
// it. Exits non-zero on any failure.

#include <cstdio>
#include <optional>
#include <string>

#include "auth_config.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

biopass::AntiSpoofingConfig withIr(std::optional<std::string> camera) {
  biopass::AntiSpoofingConfig a;
  a.ir_camera = std::move(camera);
  a.ir_warmup_delay_ms = 123;
  a.ir_presence_timeout_ms = 456;
  return a;
}

}  // namespace

int main() {
  using biopass::aiAntiSpoofingModel;
  using biopass::irCameraConfig;

  // IR: on whenever ir_camera holds a non-empty string; enable has no say.
  expect(!irCameraConfig(withIr(std::nullopt)), "ir: nullopt is off");
  expect(!irCameraConfig(withIr("")), "ir: empty string is off");
  {
    // Not trimmed: whitespace is a device path that fails to open (fail closed).
    const auto ir = irCameraConfig(withIr("  "));
    expect(ir && ir->device == "  ", "ir: whitespace is on, kept as is");
  }
  {
    const auto ir = irCameraConfig(withIr("/dev/video2"));
    expect(ir && ir->device == "/dev/video2" && ir->warmup_delay_ms == 123 &&
               ir->presence_timeout_ms == 456,
           "ir: a device is on, with its warmup and timeout");
  }
  {
    auto a = withIr("/dev/video2");
    a.enable = false;
    expect(irCameraConfig(a).has_value(), "ir: on when the AI check is disabled");
    a.enable = true;
    expect(irCameraConfig(a).has_value(), "ir: on when the AI check is enabled too");
  }
  expect(!irCameraConfig(biopass::AntiSpoofingConfig{}), "ir: off by default");

  // AI: on exactly when enable is set, model or not.
  {
    biopass::AntiSpoofingConfig a;
    a.model.model_id = "as";
    a.enable = false;
    expect(!aiAntiSpoofingModel(a), "ai: disabled is off even with a model");
    a.enable = true;
    a.model.threshold = 0.7f;
    const auto ai = aiAntiSpoofingModel(a);
    expect(ai && ai->model_id == "as" && ai->threshold == 0.7f, "ai: enabled gives its model");
  }
  {
    // Enabled with no model still runs the check, which then fails closed.
    biopass::AntiSpoofingConfig a;
    a.enable = true;
    const auto ai = aiAntiSpoofingModel(a);
    expect(ai && ai->model_id.empty(), "ai: enabled without a model is still on");
  }
  {
    biopass::AntiSpoofingConfig a;
    a.ir_camera = "/dev/video2";
    expect(!aiAntiSpoofingModel(a), "ai: an IR camera does not turn it on");
  }
  expect(!aiAntiSpoofingModel(biopass::AntiSpoofingConfig{}), "ai: off by default");

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

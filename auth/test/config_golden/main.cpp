// Golden test against the app (app/src-tauri/src/config.rs), which writes
// auth/test/config/defaults.yaml and limits.yaml (its tests fail when the
// files are out of date):
// - parseConfig reads the app's default config as the helper's own
//   BiopassConfig{}, field by field, and validateConfig leaves it unchanged.
// - The app's value ranges match kMax*, kMinThreshold and kMaxThreshold.
// Exits non-zero on any failure.

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "auth_config.h"

namespace {

using biopass::BiopassConfig;

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

std::string readFile(const char* path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// Each field of `a` against `b`, so a failure names the field.
void expectSameConfig(const BiopassConfig& a, const BiopassConfig& b, const std::string& what) {
  const auto field = [&](bool same, const char* name) { expect(same, what + ": " + name); };
  field(a.schema_version == b.schema_version, "schema_version");
  field(a.strategy.debug == b.strategy.debug, "strategy.debug");
  field(a.strategy.execution_mode == b.strategy.execution_mode, "strategy.execution_mode");
  field(a.strategy.order == b.strategy.order, "strategy.order");
  field(a.strategy.ignore_services == b.strategy.ignore_services, "strategy.ignore_services");

  const auto& fa = a.methods.face;
  const auto& fb = b.methods.face;
  field(fa.enable == fb.enable, "face.enable");
  field(fa.retries == fb.retries, "face.retries");
  field(fa.retry_delay == fb.retry_delay, "face.retry_delay");
  field(fa.camera == fb.camera, "face.camera");
  field(fa.detection.model_id == fb.detection.model_id, "face.detection.model_id");
  field(fa.detection.threshold == fb.detection.threshold, "face.detection.threshold");
  field(fa.recognition.model_id == fb.recognition.model_id, "face.recognition.model_id");
  field(fa.recognition.threshold == fb.recognition.threshold, "face.recognition.threshold");

  const auto& aa = fa.anti_spoofing;
  const auto& ab = fb.anti_spoofing;
  field(aa.enable == ab.enable, "anti_spoofing.enable");
  field(aa.model.model_id == ab.model.model_id, "anti_spoofing.model.model_id");
  field(aa.model.threshold == ab.model.threshold, "anti_spoofing.model.threshold");
  field(aa.ir_camera == ab.ir_camera, "anti_spoofing.ir_camera");
  field(aa.ir_warmup_delay_ms == ab.ir_warmup_delay_ms, "anti_spoofing.ir_warmup_delay_ms");
  field(aa.ir_presence_timeout_ms == ab.ir_presence_timeout_ms,
        "anti_spoofing.ir_presence_timeout_ms");

  const auto& pa = a.methods.fingerprint;
  const auto& pb = b.methods.fingerprint;
  field(pa.enable == pb.enable, "fingerprint.enable");
  field(pa.retries == pb.retries, "fingerprint.retries");
  field(pa.timeout == pb.timeout, "fingerprint.timeout");
}

void checkDefaults() {
  const std::optional<BiopassConfig> parsed = biopass::parseConfig(readFile(DEFAULTS_FILE));
  expect(parsed.has_value(), "defaults.yaml parses");
  if (!parsed) {
    return;
  }

  BiopassConfig expected;
  // BiopassConfig{} has 0 (no config read yet); parseConfig accepts only the
  // current version.
  expected.schema_version = biopass::kCurrentSchemaVersion;

  // TODO(#222): known drift. The app fills in the built-in models; the helper
  // leaves model_id empty, so a hand-written config without a model_id loads
  // no model in the helper (face fails) but shows the default in the app.
  // Picking one side changes what authentication loads, so it is left to the
  // discussion. Pinned here so that changing either side fails this test.
  const auto& face = parsed->methods.face;
  expect(
      expected.methods.face.detection.model_id.empty() && face.detection.model_id == "yolov8n-face",
      "known drift (#222): detection.model_id is '' in C++, yolov8n-face in the app");
  expect(expected.methods.face.recognition.model_id.empty() &&
             face.recognition.model_id == "edgeface-s-gamma-05",
         "known drift (#222): recognition.model_id is '' in C++, edgeface-s-gamma-05 in the app");
  expect(expected.methods.face.anti_spoofing.model.model_id.empty() &&
             face.anti_spoofing.model.model_id == "mobilenetv3-antispoof",
         "known drift (#222): anti_spoofing.model.model_id is '' in C++, mobilenetv3-antispoof "
         "in the app");
  expected.methods.face.detection.model_id = face.detection.model_id;
  expected.methods.face.recognition.model_id = face.recognition.model_id;
  expected.methods.face.anti_spoofing.model.model_id = face.anti_spoofing.model.model_id;

  expectSameConfig(*parsed, expected, "app default = BiopassConfig{}");

  BiopassConfig validated = *parsed;
  biopass::validateConfig(validated);
  expectSameConfig(validated, *parsed, "validateConfig keeps the defaults");
}

void checkLimits() {
  const YAML::Node limits = YAML::LoadFile(LIMITS_FILE);
  const auto max = [&](const char* key) { return limits[key]["max"]; };

  expect(max("face_retries").as<uint32_t>() == biopass::kMaxFaceRetries, "face_retries max");
  expect(max("retry_delay_ms").as<uint32_t>() == biopass::kMaxRetryDelayMs, "retry_delay_ms max");
  expect(max("ir_warmup_delay_ms").as<int>() == biopass::kMaxIrWarmupDelayMs,
         "ir_warmup_delay_ms max");
  expect(max("ir_presence_timeout_ms").as<int>() == biopass::kMaxIrPresenceTimeoutMs,
         "ir_presence_timeout_ms max");
  expect(max("fingerprint_retries").as<uint32_t>() == biopass::kMaxFingerprintRetries,
         "fingerprint_retries max");
  expect(max("fingerprint_timeout_ms").as<uint32_t>() == biopass::kMaxFingerprintTimeoutMs,
         "fingerprint_timeout_ms max");
  expect(limits["threshold"]["min"].as<float>() == biopass::kMinThreshold, "threshold min");
  expect(max("threshold").as<float>() == biopass::kMaxThreshold, "threshold max");
  // The helper clamps the IR delays from 0; the app's other minima are its
  // own (the helper only clamps retries and timeouts from above).
  expect(limits["ir_warmup_delay_ms"]["min"].as<int>() == 0, "ir_warmup_delay_ms min");
  expect(limits["ir_presence_timeout_ms"]["min"].as<int>() == 0, "ir_presence_timeout_ms min");
}

}  // namespace

int main() {
  checkDefaults();
  try {
    checkLimits();
  } catch (const YAML::Exception& e) {
    expect(false, std::string("limits.yaml reads: ") + e.what());
  }

  std::printf("\n%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

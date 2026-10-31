// Checks validateConfig (core/auth_config.h): out-of-range thresholds turn
// the face method off instead of loosening it, and order, ignore_services
// and execution_mode come out normalised. Exits non-zero on any failure.

#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// The range edges and in-range values keep face on; anything else turns it
// off.
const std::vector<std::pair<float, bool>> kThresholdCases = {
    {0.0f, true},   {1.0f, true},  {0.5f, true},  {-0.01f, false}, {1.01f, false},
    {-2.0f, false}, {kNaN, false}, {kInf, false}, {-kInf, false},
};

using Setter = void (*)(BiopassConfig&, float);

void checkThreshold(const char* name, Setter set) {
  for (const auto& [value, keep] : kThresholdCases) {
    BiopassConfig c;
    set(c, value);
    biopass::validateConfig(c);
    expect(c.methods.face.enable == keep, std::string(name) + " " + std::to_string(value) +
                                              (keep ? " keeps face on" : " turns face off"));
  }
}

bool sameConfig(const BiopassConfig& a, const BiopassConfig& b) {
  const auto& fa = a.methods.face;
  const auto& fb = b.methods.face;
  const auto& pa = a.methods.fingerprint;
  const auto& pb = b.methods.fingerprint;
  return a.schema_version == b.schema_version && a.strategy.debug == b.strategy.debug &&
         a.strategy.execution_mode == b.strategy.execution_mode &&
         a.strategy.order == b.strategy.order &&
         a.strategy.ignore_services == b.strategy.ignore_services && fa.enable == fb.enable &&
         fa.retries == fb.retries && fa.retry_delay == fb.retry_delay && fa.camera == fb.camera &&
         fa.detection.model_id == fb.detection.model_id &&
         fa.detection.threshold == fb.detection.threshold &&
         fa.recognition.model_id == fb.recognition.model_id &&
         fa.recognition.threshold == fb.recognition.threshold &&
         fa.anti_spoofing.enable == fb.anti_spoofing.enable &&
         fa.anti_spoofing.model.model_id == fb.anti_spoofing.model.model_id &&
         fa.anti_spoofing.model.threshold == fb.anti_spoofing.model.threshold &&
         fa.anti_spoofing.ir_camera == fb.anti_spoofing.ir_camera &&
         fa.anti_spoofing.ir_warmup_delay_ms == fb.anti_spoofing.ir_warmup_delay_ms &&
         fa.anti_spoofing.ir_presence_timeout_ms == fb.anti_spoofing.ir_presence_timeout_ms &&
         pa.enable == pb.enable && pa.retries == pb.retries && pa.timeout == pb.timeout;
}

bool isDefaults(const BiopassConfig& c) { return sameConfig(c, BiopassConfig{}); }

const char* kFullConfig = R"(schema_version: 2
strategy:
  debug: true
  execution_mode: sequential
  order: [fingerprint, face]
  ignore_services: [sudo]
methods:
  face:
    enable: true
    retries: 3
    retry_delay: 100
    camera: /dev/video0
    detection: {model_id: det, threshold: 0.6}
    recognition: {model_id: rec, threshold: 0.7}
    anti_spoofing:
      enable: true
      model: {model_id: as, threshold: 0.9}
      ir_camera: /dev/video2
      ir_warmup_delay_ms: 100
      ir_presence_timeout_ms: 200
  fingerprint:
    enable: true
    retries: 2
    timeout: 3000
)";

void checkParse() {
  {
    const BiopassConfig c = biopass::parseConfig(kFullConfig);
    const auto& f = c.methods.face;
    expect(c.schema_version == 2 && c.strategy.debug && c.strategy.execution_mode == "sequential" &&
               c.strategy.order == std::vector<std::string>{"fingerprint", "face"} &&
               c.strategy.ignore_services == std::vector<std::string>{"sudo"},
           "parse: strategy read");
    expect(f.enable && f.retries == 3 && f.retry_delay == 100 && f.camera == "/dev/video0" &&
               f.detection.model_id == "det" && f.detection.threshold == 0.6f &&
               f.recognition.model_id == "rec" && f.recognition.threshold == 0.7f,
           "parse: face read");
    expect(f.anti_spoofing.enable && f.anti_spoofing.model.model_id == "as" &&
               f.anti_spoofing.model.threshold == 0.9f &&
               f.anti_spoofing.ir_camera == "/dev/video2" &&
               f.anti_spoofing.ir_warmup_delay_ms == 100 &&
               f.anti_spoofing.ir_presence_timeout_ms == 200,
           "parse: anti_spoofing read");
    expect(c.methods.fingerprint.enable && c.methods.fingerprint.retries == 2 &&
               c.methods.fingerprint.timeout == 3000,
           "parse: fingerprint read");
  }

  // A bad value after good ones: nothing parsed before it may stick.
  for (const auto& [what, yaml] : std::vector<std::pair<std::string, std::string>>{
           {"bad fingerprint.timeout",
            "schema_version: 2\nstrategy: {debug: true, execution_mode: sequential}\n"
            "methods:\n  face: {retries: 3}\n  fingerprint: {enable: true, timeout: abc}\n"},
           {"bad face threshold type",
            "schema_version: 2\nstrategy: {debug: true, ignore_services: []}\n"
            "methods:\n  face: {enable: false, recognition: {threshold: [1]}}\n"},
           {"bad order entry",
            "schema_version: 2\nstrategy: {debug: true, order: [face, {a: 1}]}\n"},
           {"YAML syntax error", "schema_version: 2\nstrategy: {debug: true\n"},
           {"not a map", "just a string\n"},
           {"other schema_version", "schema_version: 1\nstrategy: {debug: true}\n"},
           {"empty file", ""},
       }) {
    expect(isDefaults(biopass::parseConfig(yaml)),
           "parse: " + what + " -> defaults, nothing mixed");
  }

  {
    const BiopassConfig c = biopass::parseConfig(
        "schema_version: 2\nmethods:\n  face: {enable: true, recognition: {threshold: -2}}\n");
    expect(!c.methods.face.enable, "parse: recognition.threshold -2 -> face off");
  }

  {
    const BiopassConfig c = biopass::parseConfig(
        "schema_version: 2\nmethods:\n  face: {detection: {threshold: .nan}}\n");
    expect(!c.methods.face.enable, "parse: detection.threshold .nan -> face off");
  }

  {
    const BiopassConfig c = biopass::parseConfig(
        "schema_version: 2\nstrategy: {ignore_services: [polkit-1, '', polkit-1, sudo]}\n");
    expect(c.strategy.ignore_services == std::vector<std::string>{"polkit-1", "sudo"},
           "parse: ignore_services deduped");
  }

  {
    const BiopassConfig c = biopass::parseConfig("schema_version: 2\nstrategy: {debug: true}\n");
    expect(c.strategy.ignore_services == BiopassConfig{}.strategy.ignore_services,
           "parse: missing ignore_services keeps the default list");
  }
}

}  // namespace

int main() {
  checkParse();

  {
    BiopassConfig c;
    biopass::validateConfig(c);
    expect(c.methods.face.enable, "defaults keep face on");
  }

  checkThreshold("detection.threshold",
                 [](BiopassConfig& c, float v) { c.methods.face.detection.threshold = v; });
  checkThreshold("recognition.threshold",
                 [](BiopassConfig& c, float v) { c.methods.face.recognition.threshold = v; });
  checkThreshold("anti_spoofing.model.threshold (enabled)", [](BiopassConfig& c, float v) {
    c.methods.face.anti_spoofing.enable = true;
    c.methods.face.anti_spoofing.model.threshold = v;
  });

  {
    // Unused while anti-spoofing is off, so it doesn't cost the user face.
    BiopassConfig c;
    c.methods.face.anti_spoofing.enable = false;
    c.methods.face.anti_spoofing.model.threshold = 5.0f;
    biopass::validateConfig(c);
    expect(c.methods.face.enable, "bad anti_spoofing threshold is ignored while it is off");
  }

  {
    // Fingerprint doesn't use the face thresholds.
    BiopassConfig c;
    c.methods.fingerprint.enable = true;
    c.methods.face.recognition.threshold = -2.0f;
    biopass::validateConfig(c);
    expect(!c.methods.face.enable && c.methods.fingerprint.enable,
           "bad face threshold leaves fingerprint on");
  }

  {
    BiopassConfig c;
    c.strategy.order = {"fingerprint", "bogus", "fingerprint"};
    biopass::validateConfig(c);
    expect(c.strategy.order == std::vector<std::string>{"fingerprint", "face"},
           "order: unknown and repeated methods dropped, missing ones appended");
  }

  {
    BiopassConfig c;
    c.strategy.order = {};
    biopass::validateConfig(c);
    expect(c.strategy.order == std::vector<std::string>{"face", "fingerprint"},
           "order: empty -> every method in the default order");
  }

  {
    BiopassConfig c;
    c.strategy.ignore_services = {"", "sudo", "gdm-password", "sudo", ""};
    biopass::validateConfig(c);
    expect(c.strategy.ignore_services == std::vector<std::string>{"sudo", "gdm-password"},
           "ignore_services: empty and repeated entries dropped, order kept");
  }

  {
    BiopassConfig c;
    c.strategy.ignore_services = {};
    biopass::validateConfig(c);
    expect(c.strategy.ignore_services.empty(), "ignore_services: an empty list stays empty");
  }

  for (const auto& [mode, want] : std::vector<std::pair<std::string, std::string>>{
           {"sequential", "sequential"},
           {"parallel", "parallel"},
           {"bogus", "parallel"},
           {"", "parallel"},
       }) {
    BiopassConfig c;
    c.strategy.execution_mode = mode;
    biopass::validateConfig(c);
    expect(c.strategy.execution_mode == want, "execution_mode '" + mode + "' -> " + want);
  }

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

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

}  // namespace

int main() {
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

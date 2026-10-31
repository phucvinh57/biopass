#include "auth_config.h"

#include <spdlog/spdlog.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstring>

#include "fs_user.h"
#include "user_file.h"
#include "user_paths.h"

namespace biopass {

namespace {

// Real configs are a few hundred bytes.
constexpr size_t kMaxConfigBytes = 1 << 20;

}  // namespace

namespace {

const std::vector<std::string> kSupportedMethods = {"face", "fingerprint"};

// NaN fails both comparisons, so it is out of range too.
bool thresholdInRange(float t) { return t >= kMinThreshold && t <= kMaxThreshold; }

// Keeps known methods once each, in the user's order, then appends any the
// user left out.
std::vector<std::string> normalizeOrder(const std::vector<std::string>& order) {
  std::vector<std::string> out;
  for (const auto& method : order) {
    if (std::find(kSupportedMethods.begin(), kSupportedMethods.end(), method) !=
            kSupportedMethods.end() &&
        std::find(out.begin(), out.end(), method) == out.end()) {
      out.push_back(method);
    }
  }
  for (const auto& method : kSupportedMethods) {
    if (std::find(out.begin(), out.end(), method) == out.end()) {
      out.push_back(method);
    }
  }
  return out;
}

// Drops empty and repeated entries, keeping the first of each.
std::vector<std::string> dedupeServices(const std::vector<std::string>& services) {
  std::vector<std::string> out;
  for (const auto& service : services) {
    if (!service.empty() && std::find(out.begin(), out.end(), service) == out.end()) {
      out.push_back(service);
    }
  }
  return out;
}

// A threshold outside [0, 1] can only loosen or break a check: recognition
// matches when cosine similarity > threshold (-2 lets any face in),
// anti-spoofing flags a spoof when its probability >= threshold (above 1 or
// NaN never does), detection keeps boxes whose score >= threshold. None of
// them has a safe value to clamp to, so face is turned off and PAM falls
// through to the next module. The anti-spoofing threshold only counts when
// anti-spoofing is on.
bool faceThresholdsValid(const FaceMethodConfig& face) {
  return thresholdInRange(face.detection.threshold) &&
         thresholdInRange(face.recognition.threshold) &&
         (!face.anti_spoofing.enable || thresholdInRange(face.anti_spoofing.model.threshold));
}

}  // namespace

void validateConfig(BiopassConfig& config) {
  StrategyConfig& strategy = config.strategy;
  if (strategy.execution_mode != "sequential") {
    strategy.execution_mode = "parallel";
  }
  strategy.order = normalizeOrder(strategy.order);
  strategy.ignore_services = dedupeServices(strategy.ignore_services);

  FaceMethodConfig& face = config.methods.face;
  if (face.enable && !faceThresholdsValid(face)) {
    spdlog::warn("Biopass: a face threshold is outside [{}, {}], face is disabled", kMinThreshold,
                 kMaxThreshold);
    face.enable = false;
  }
  face.retries = std::min(face.retries, kMaxFaceRetries);
  face.retry_delay = std::min(face.retry_delay, kMaxRetryDelayMs);
  face.anti_spoofing.ir_warmup_delay_ms =
      std::clamp(face.anti_spoofing.ir_warmup_delay_ms, 0, kMaxIrWarmupDelayMs);
  face.anti_spoofing.ir_presence_timeout_ms =
      std::clamp(face.anti_spoofing.ir_presence_timeout_ms, 0, kMaxIrPresenceTimeoutMs);

  FingerprintMethodConfig& fp = config.methods.fingerprint;
  fp.retries = std::min(fp.retries, kMaxFingerprintRetries);
  if (fp.timeout == 0 || fp.timeout > kMaxFingerprintTimeoutMs) {
    fp.timeout = kMaxFingerprintTimeoutMs;
  }
}

namespace {

// Sets `out` from node[key] when the key is there. Throws YAML::Exception if
// the value has the wrong type.
template <typename T>
void readIf(const YAML::Node& node, const char* key, T& out) {
  if (node[key]) {
    out = node[key].as<T>();
  }
}

void readOptionalString(const YAML::Node& node, const char* key, std::optional<std::string>& out) {
  if (node[key] && !node[key].IsNull()) {
    out = node[key].as<std::string>();
  }
}

void parseStrategy(const YAML::Node& s, StrategyConfig& out) {
  readIf(s, "debug", out.debug);
  readIf(s, "execution_mode", out.execution_mode);
  if (s["order"] && s["order"].IsSequence()) {
    out.order.clear();
    for (const auto& m : s["order"]) {
      out.order.push_back(m.as<std::string>());
    }
  }
  if (s["ignore_services"]) {
    out.ignore_services.clear();
    if (s["ignore_services"].IsSequence()) {
      for (const auto& item : s["ignore_services"]) {
        if (item.IsScalar()) {
          out.ignore_services.push_back(item.as<std::string>());
        }
      }
    }
  }
}

void parseAntiSpoofing(const YAML::Node& a, AntiSpoofingConfig& out) {
  readIf(a, "enable", out.enable);
  if (a["model"] && a["model"].IsMap()) {
    readIf(a["model"], "model_id", out.model.model_id);
    readIf(a["model"], "threshold", out.model.threshold);
  }
  readOptionalString(a, "ir_camera", out.ir_camera);
  readIf(a, "ir_warmup_delay_ms", out.ir_warmup_delay_ms);
  readIf(a, "ir_presence_timeout_ms", out.ir_presence_timeout_ms);
}

void parseFace(const YAML::Node& f, FaceMethodConfig& out) {
  readIf(f, "enable", out.enable);
  readIf(f, "retries", out.retries);
  readIf(f, "retry_delay", out.retry_delay);
  if (f["detection"]) {
    readIf(f["detection"], "model_id", out.detection.model_id);
    readIf(f["detection"], "threshold", out.detection.threshold);
  }
  if (f["recognition"]) {
    readIf(f["recognition"], "model_id", out.recognition.model_id);
    readIf(f["recognition"], "threshold", out.recognition.threshold);
  }
  readOptionalString(f, "camera", out.camera);
  if (f["anti_spoofing"]) {
    parseAntiSpoofing(f["anti_spoofing"], out.anti_spoofing);
  }
}

void parseFingerprint(const YAML::Node& fp, FingerprintMethodConfig& out) {
  readIf(fp, "enable", out.enable);
  readIf(fp, "retries", out.retries);
  readIf(fp, "timeout", out.timeout);
}

// Builds a config from a document of the current schema. Throws
// YAML::Exception on the first bad value.
BiopassConfig parseDocument(const YAML::Node& yaml) {
  BiopassConfig config;
  config.schema_version = kCurrentSchemaVersion;
  if (yaml["strategy"]) {
    parseStrategy(yaml["strategy"], config.strategy);
  }
  if (yaml["methods"]) {
    const YAML::Node& m = yaml["methods"];
    if (m["face"]) {
      parseFace(m["face"], config.methods.face);
    }
    if (m["fingerprint"]) {
      parseFingerprint(m["fingerprint"], config.methods.fingerprint);
    }
  }
  return config;
}

}  // namespace

BiopassConfig parseConfig(const std::string& text) {
  // Defaults unless the whole document parses: a bad value halfway through
  // must not leave the values before it mixed with defaults.
  BiopassConfig config;

  try {
    const YAML::Node yaml = YAML::Load(text);

    // Schema mismatch = defaults (only the app migrates). Default model_ids
    // don't resolve, so face auth reports Unavailable and PAM falls through
    // to normal auth instead of locking anyone out.
    const int schema_version = yaml["schema_version"] ? yaml["schema_version"].as<int>() : 0;
    if (schema_version == kCurrentSchemaVersion) {
      config = parseDocument(yaml);
    } else {
      spdlog::warn(
          "Biopass: config.yaml schema_version {} does not match expected {}, using defaults",
          schema_version, kCurrentSchemaVersion);
    }
  } catch (const YAML::Exception& e) {
    spdlog::error("Biopass: Failed to parse config: {}, using defaults", e.what());
  }

  validateConfig(config);
  return config;
}

std::optional<BiopassConfig> readConfig(const std::string& username) {
  // The config path is user-controlled, so it is read as the user and only
  // if it is a regular file. Anything else counts as missing.
  const std::optional<UserPaths> user = resolveUser(username);
  if (!user) {
    return std::nullopt;
  }
  const std::optional<std::string> text =
      readUserFile(username, user->configFile(), kMaxConfigBytes);
  if (!text) {
    return std::nullopt;
  }
  return parseConfig(*text);
}

static int mkdir_p(const std::string& path) {
  size_t pos = 0;
  std::string dir;
  int ret;
  while ((pos = path.find('/', pos)) != std::string::npos) {
    dir = path.substr(0, pos++);
    if (dir.empty())
      continue;
    ret = mkdir(dir.c_str(), 0700);
    if (ret == -1 && errno != EEXIST) {
      spdlog::error("Biopass: Failed to create directory '{}': {}", dir, strerror(errno));
      return 1;
    }
  }
  ret = mkdir(path.c_str(), 0700);
  if (ret == -1 && errno != EEXIST) {
    spdlog::error("Biopass: Failed to create directory '{}': {}", path, strerror(errno));
    return 1;
  }
  return 0;
}

std::vector<std::string> listFaces(const std::string& username) {
  std::vector<std::string> faces;
  const std::optional<UserPaths> user = resolveUser(username);
  if (!user) {
    return faces;
  }
  const std::string dir = user->facesDir();
  // The dir is the user's: list it as the user. Callers read each file with
  // readUserFile(), which skips anything that isn't a regular file.
  ScopedFsUser as_user(user->uid, user->gid);
  if (!as_user.ok())
    return faces;
  DIR* dp = opendir(dir.c_str());
  if (!dp)
    return faces;

  struct dirent* entry;
  while ((entry = readdir(dp)) != nullptr) {
    std::string name(entry->d_name);
    if (name.size() > 4) {
      std::string ext = name.substr(name.size() - 4);
      if (ext == ".jpg" || ext == ".JPG" || ext == ".png" || ext == ".PNG" || ext == ".bmp" ||
          ext == ".BMP" || ext == ".tga" || ext == ".TGA") {
        faces.push_back(dir + "/" + name);
      } else if (name.size() > 5) {
        std::string ext5 = name.substr(name.size() - 5);
        if (ext5 == ".jpeg" || ext5 == ".JPEG") {
          faces.push_back(dir + "/" + name);
        }
      }
    }
  }
  closedir(dp);
  std::sort(faces.begin(), faces.end());
  return faces;
}

int setupDataDirs(const std::string& dataDir) {
  if (dataDir.empty()) {
    return 1;
  }
  for (const char* sub : {"/faces", "/debugs", "/logs"}) {
    if (mkdir_p(dataDir + sub) != 0) {
      return 1;
    }
  }
  return 0;
}

}  // namespace biopass

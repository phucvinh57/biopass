#include "auth_config.h"

#include <pwd.h>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace biopass {

std::string getConfigPath(const std::string& username) {
  struct passwd* pw = getpwnam(username.c_str());
  if (pw == nullptr) {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + "/.config/com.ticklab.biopass/config.yaml";
    }
    return "/etc/com.ticklab.biopass/config.yaml";
  }
  return std::string(pw->pw_dir) + "/.config/com.ticklab.biopass/config.yaml";
}

bool configExists(const std::string& username) {
  std::ifstream f(getConfigPath(username));
  return f.good();
}

std::string user_data_dir(const std::string& username) {
  struct passwd* pw = getpwnam(username.c_str());
  if (pw != nullptr) {
    return std::string(pw->pw_dir) + "/.local/share/com.ticklab.biopass";
  }
  const char* home = getenv("HOME");
  if (home)
    return std::string(home) + "/.local/share/com.ticklab.biopass";
  return "";
}

BiopassConfig readConfig(const std::string& username) {
  BiopassConfig config;

  std::string config_path = getConfigPath(username);

  try {
    YAML::Node yaml = YAML::LoadFile(config_path);

    // Schema gate: this side never migrates config.yaml (only the Tauri app
    // writes it), so an absent/mismatched schema_version is treated exactly
    // like a missing/corrupt file -- fall back to defaults. A defaulted
    // config's model_ids won't resolve against biopass.db either, so
    // ModelRegistry::resolveModel() returns nullopt and
    // ensureModelsLoaded() safely reports Unavailable, letting PAM fall
    // through to normal system auth instead of locking anyone out.
    int schema_version = yaml["schema_version"] ? yaml["schema_version"].as<int>() : 0;
    if (schema_version != kCurrentSchemaVersion) {
      spdlog::warn(
          "Biopass: config.yaml schema_version {} does not match expected {}, using defaults",
          schema_version, kCurrentSchemaVersion);
      return config;
    }
    config.schema_version = schema_version;

    static const std::vector<std::string> supported_methods = {"face", "fingerprint"};

    if (yaml["strategy"]) {
      const auto& s = yaml["strategy"];
      if (s["debug"])
        config.strategy.debug = s["debug"].as<bool>();
      if (s["execution_mode"])
        config.strategy.execution_mode = s["execution_mode"].as<std::string>();
      if (s["order"] && s["order"].IsSequence()) {
        config.strategy.order.clear();
        for (const auto& m : s["order"]) config.strategy.order.push_back(m.as<std::string>());
      }
      if (s["ignore_services"]) {
        config.strategy.ignore_services.clear();
        if (s["ignore_services"].IsSequence()) {
          for (const auto& item : s["ignore_services"]) {
            if (item.IsScalar()) {
              const auto service = item.as<std::string>();
              if (service.empty()) {
                continue;
              }
              if (std::find(config.strategy.ignore_services.begin(),
                            config.strategy.ignore_services.end(),
                            service) == config.strategy.ignore_services.end()) {
                config.strategy.ignore_services.push_back(service);
              }
            }
          }
        }
      }
    }
    std::vector<std::string> normalized_order;
    for (const auto& method : config.strategy.order) {
      if (std::find(supported_methods.begin(), supported_methods.end(), method) ==
          supported_methods.end()) {
        continue;
      }
      if (std::find(normalized_order.begin(), normalized_order.end(), method) ==
          normalized_order.end()) {
        normalized_order.push_back(method);
      }
    }
    for (const auto& method : supported_methods) {
      if (std::find(normalized_order.begin(), normalized_order.end(), method) ==
          normalized_order.end()) {
        normalized_order.push_back(method);
      }
    }
    config.strategy.order = std::move(normalized_order);

    if (yaml["methods"]) {
      const auto& m = yaml["methods"];

      if (m["face"]) {
        const auto& f = m["face"];
        if (f["enable"])
          config.methods.face.enable = f["enable"].as<bool>();
        if (f["retries"])
          config.methods.face.retries = f["retries"].as<uint32_t>();
        if (f["retry_delay"])
          config.methods.face.retry_delay = f["retry_delay"].as<uint32_t>();
        if (f["detection"]) {
          if (f["detection"]["model_id"])
            config.methods.face.detection.model_id = f["detection"]["model_id"].as<std::string>();
          if (f["detection"]["threshold"])
            config.methods.face.detection.threshold = f["detection"]["threshold"].as<float>();
        }
        if (f["recognition"]) {
          if (f["recognition"]["model_id"])
            config.methods.face.recognition.model_id =
                f["recognition"]["model_id"].as<std::string>();
          if (f["recognition"]["threshold"])
            config.methods.face.recognition.threshold = f["recognition"]["threshold"].as<float>();
        }
        if (f["camera"] && !f["camera"].IsNull()) {
          config.methods.face.camera = f["camera"].as<std::string>();
        }
        if (f["anti_spoofing"]) {
          const auto& anti_spoofing = f["anti_spoofing"];
          if (anti_spoofing["enable"]) {
            config.methods.face.anti_spoofing.enable = anti_spoofing["enable"].as<bool>();
          }

          if (anti_spoofing["model"] && anti_spoofing["model"].IsMap()) {
            const auto& model = anti_spoofing["model"];
            if (model["model_id"])
              config.methods.face.anti_spoofing.model.model_id =
                  model["model_id"].as<std::string>();
            if (model["threshold"])
              config.methods.face.anti_spoofing.model.threshold = model["threshold"].as<float>();
          }

          if (anti_spoofing["ir_camera"] && !anti_spoofing["ir_camera"].IsNull()) {
            config.methods.face.anti_spoofing.ir_camera =
                anti_spoofing["ir_camera"].as<std::string>();
          }

          if (anti_spoofing["ir_warmup_delay_ms"]) {
            config.methods.face.anti_spoofing.ir_warmup_delay_ms =
                anti_spoofing["ir_warmup_delay_ms"].as<int>();
          }

          if (anti_spoofing["ir_presence_timeout_ms"]) {
            config.methods.face.anti_spoofing.ir_presence_timeout_ms =
                anti_spoofing["ir_presence_timeout_ms"].as<int>();
          }
        }
      }

      if (m["fingerprint"]) {
        const auto& fp = m["fingerprint"];
        if (fp["enable"])
          config.methods.fingerprint.enable = fp["enable"].as<bool>();
        if (fp["retries"])
          config.methods.fingerprint.retries = fp["retries"].as<uint32_t>();
        if (fp["timeout"])
          config.methods.fingerprint.timeout = fp["timeout"].as<uint32_t>();
      }
    }

    if (yaml["appearance"] && yaml["appearance"].IsScalar()) {
      config.appearance = yaml["appearance"].as<std::string>();
    }
  } catch (const YAML::BadFile& e) {
    spdlog::warn("Biopass: Config file not found at {}, using defaults", config_path);
  } catch (const YAML::Exception& e) {
    spdlog::error("Biopass: Failed to parse config: {}, using defaults", e.what());
  }

  return config;
}

// ---------------------------------------------------------------------------
// Directory / path helpers
// ---------------------------------------------------------------------------

static int mkdir_p(const std::string& path) {
  size_t pos = 0;
  std::string dir;
  int ret;
  while ((pos = path.find('/', pos)) != std::string::npos) {
    dir = path.substr(0, pos++);
    if (dir.empty())
      continue;
    ret = mkdir(dir.c_str(), 0777);
    if (ret == -1 && errno != EEXIST) {
      spdlog::error("Biopass: Failed to create directory '{}': {}", dir, strerror(errno));
      return 1;
    }
  }
  ret = mkdir(path.c_str(), 0777);
  if (ret == -1 && errno != EEXIST) {
    spdlog::error("Biopass: Failed to create directory '{}': {}", path, strerror(errno));
    return 1;
  }
  return 0;
}

std::vector<std::string> listFaces(const std::string& username) {
  std::vector<std::string> faces;
  std::string dir = user_data_dir(username) + "/faces";
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

std::string getDebugPath(const std::string& username) {
  return user_data_dir(username) + "/debugs";
}

std::string getLogPath(const std::string& username) { return user_data_dir(username) + "/logs"; }

void fixOwnership(const std::string& path, const std::string& username) {
  if (geteuid() != 0) {
    return;
  }
  struct passwd* pw = getpwnam(username.c_str());
  if (pw == nullptr) {
    return;
  }
  if (chown(path.c_str(), pw->pw_uid, pw->pw_gid) != 0) {
    spdlog::error("Biopass: Failed to chown '{}' to {}: {}", path, username, strerror(errno));
  }
}

int setupConfig(const std::string& username) {
  const std::string dataDir = user_data_dir(username);
  if (mkdir_p(dataDir + "/faces") != 0)
    return 1;
  if (mkdir_p(dataDir + "/debugs") != 0)
    return 1;
  if (mkdir_p(dataDir + "/logs") != 0)
    return 1;

  // PAM auth flows run this as root, which leaves freshly created
  // directories owned by root:root; a later user-invoked run (the desktop
  // app capturing a preview, or a dev testing biopass-helper directly) then
  // can't write into them. Hand the whole subtree back to the target user
  // so root- and user-invoked runs never fight over ownership.
  fixOwnership(dataDir, username);
  fixOwnership(dataDir + "/faces", username);
  fixOwnership(dataDir + "/debugs", username);
  fixOwnership(dataDir + "/logs", username);
  return 0;
}

}  // namespace biopass

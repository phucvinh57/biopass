#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "auth_manager.h"

namespace biopass {

// Keep in sync with CURRENT_SCHEMA_VERSION in app/src-tauri/src/config.rs.
// On mismatch readConfig() falls back to defaults; only the app migrates.
inline constexpr int kCurrentSchemaVersion = 2;

// Mirrors app/src-tauri/src/config.rs.

struct StrategyConfig {
  bool debug = false;
  std::string execution_mode = "parallel";
  std::vector<std::string> order = {"face", "fingerprint"};
  std::vector<std::string> ignore_services = {"polkit-1", "pkexec"};
};

// `model_id` resolves to an .onnx path via ModelRegistry.
struct DetectionConfig {
  std::string model_id;
  float threshold = 0.5f;
};

struct RecognitionConfig {
  std::string model_id;
  float threshold = 0.5f;
};

struct AntiSpoofingModelConfig {
  std::string model_id;
  float threshold = 0.8f;
};

struct AntiSpoofingConfig {
  bool enable = false;
  AntiSpoofingModelConfig model;
  // e.g. "/dev/video2"; nullopt = disabled.
  std::optional<std::string> ir_camera = std::nullopt;
  // Delay after warmup frames so IR LEDs and auto-exposure settle.
  // NOTE: the IR check is a face-presence check, not liveness: a printed
  // photo the detector finds in IR still passes.
  int ir_warmup_delay_ms = 300;
  // Retry budget for the IR presence check: the emitter blinks, so single
  // frames may be all-white or all-dark. 0 = single attempt.
  int ir_presence_timeout_ms = 1500;
};

struct FaceMethodConfig {
  bool enable = true;
  uint32_t retries = 5;
  uint32_t retry_delay = 200;
  // e.g. "/dev/video0"; nullopt = auto-select.
  std::optional<std::string> camera = std::nullopt;
  DetectionConfig detection;
  RecognitionConfig recognition;
  AntiSpoofingConfig anti_spoofing;
};

// No enrolled-fingers field: fprintd is the source of truth.
struct FingerprintMethodConfig {
  bool enable = false;
  uint32_t retries = 1;
  uint32_t timeout = 5000;
};

struct MethodsConfig {
  FaceMethodConfig face;
  FingerprintMethodConfig fingerprint;
};

// ~/.config/com.ticklab.biopass/config.yaml
struct BiopassConfig {
  int schema_version = 0;
  StrategyConfig strategy = {};
  MethodsConfig methods = {};
};
std::string getConfigPath(const std::string& username);
BiopassConfig readConfig(const std::string& username);
bool configExists(const std::string& username);

std::vector<std::string> listFaces(const std::string& username);
std::string getDebugPath(const std::string& username);
std::string getLogPath(const std::string& username);
int setupConfig(const std::string& username);

// Chowns `path` to `username`'s uid/gid; no-op unless running as root.
void fixOwnership(const std::string& path, const std::string& username);

}  // namespace biopass

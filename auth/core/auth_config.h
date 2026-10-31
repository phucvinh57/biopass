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
// Upper bounds for the timing values in the user's config. The PAM module
// kills the helper after 60 s regardless (pam/helper_wait.h); these keep a
// normal run below that so the user isn't cut off mid-attempt.
inline constexpr uint32_t kMaxFaceRetries = 10;
inline constexpr uint32_t kMaxRetryDelayMs = 5000;
inline constexpr int kMaxIrWarmupDelayMs = 5000;
inline constexpr int kMaxIrPresenceTimeoutMs = 10000;
inline constexpr uint32_t kMaxFingerprintRetries = 5;
inline constexpr uint32_t kMaxFingerprintTimeoutMs = 10000;

// Clamps retries, delays and timeouts into [0, max]. fingerprint.timeout 0
// used to mean "wait forever"; it now means the maximum. readConfig() calls
// this on every config it returns.
void clampStrategyLimits(MethodsConfig& methods);

std::string getConfigPath(const std::string& username);
// nullopt when config.yaml is missing, unreadable or not a regular file.
// A config that doesn't parse or has another schema_version gives defaults.
std::optional<BiopassConfig> readConfig(const std::string& username);

std::vector<std::string> listFaces(const std::string& username);
std::string getDebugPath(const std::string& username);
std::string getLogPath(const std::string& username);
// Creates faces/, debugs/ and logs/ (0700) under the user's data dir. The
// filesystem calls run as the user (see fs_user.h), so a symlink in that dir
// can't make root create or chown anything outside it.
int setupConfig(const std::string& username);
// Same, for an explicit data dir. Callers that run as root must hold a
// ScopedFsUser for the owner first.
int setupDataDirs(const std::string& dataDir);

}  // namespace biopass

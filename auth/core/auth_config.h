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
// On mismatch readConfig() gives no config; only the app migrates.
inline constexpr int kCurrentSchemaVersion = 2;

// Mirrors app/src-tauri/src/config.rs.

// A `strategy.order` entry: "face" or "fingerprint" in config.yaml.
enum class MethodKind { Face, Fingerprint };

struct StrategyConfig {
  bool debug = false;
  // config.yaml: "sequential" or "parallel"; anything else is parallel.
  ExecutionMode execution_mode = ExecutionMode::Parallel;
  std::vector<MethodKind> order = {MethodKind::Face, MethodKind::Fingerprint};
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

// The IR presence check's settings, once the config turns it on.
struct IrCameraConfig {
  std::string device;
  int warmup_delay_ms = 300;
  int presence_timeout_ms = 1500;
};

// The IR check runs when `ir_camera` is set to a non-empty string; nullopt and
// "" both mean off. Not trimmed: "  " is on and fails to open (fail closed).
// Independent of `enable`, which only governs the AI model.
std::optional<IrCameraConfig> irCameraConfig(const AntiSpoofingConfig& anti_spoofing);

// The AI model check runs when `enable` is set, even with an empty model_id:
// the model then fails to resolve and the check fails closed.
std::optional<AntiSpoofingModelConfig> aiAntiSpoofingModel(const AntiSpoofingConfig& anti_spoofing);

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
// Upper bounds for the timing values in the user's config, so no single
// value can stretch one wait or the retry count without limit. They do not
// bound a whole run: retries x (delay + capture + IR waits), then the next
// method in sequential mode, can add up past a minute. The helper's time
// budget (kAuthBudget in auth_deadline.h) bounds the run; these keep each
// wait well inside it.
inline constexpr uint32_t kMaxFaceRetries = 10;
inline constexpr uint32_t kMaxRetryDelayMs = 5000;
inline constexpr int kMaxIrWarmupDelayMs = 5000;
inline constexpr int kMaxIrPresenceTimeoutMs = 10000;
inline constexpr uint32_t kMaxFingerprintRetries = 5;
inline constexpr uint32_t kMaxFingerprintTimeoutMs = 10000;

// Thresholds are probabilities or cosine similarities in [0, 1].
inline constexpr float kMinThreshold = 0.0f;
inline constexpr float kMaxThreshold = 1.0f;

// Puts a config into the ranges the helper relies on. readConfig() calls it
// on every config it returns.
// - A face threshold outside [kMinThreshold, kMaxThreshold] (or NaN) turns
//   face off: there is no safe value to clamp it to. The anti-spoofing
//   threshold only counts while anti-spoofing is on.
// - order keeps each method once and appends the missing ones;
//   ignore_services drops empty and repeated names.
// - Retries, delays and timeouts are clamped into [0, max].
//   fingerprint.timeout 0 used to mean "wait forever"; it now means the
//   maximum.
void validateConfig(BiopassConfig& config);

// Parses config.yaml's text and runs validateConfig() on the result.
// execution_mode is "sequential" only when spelled exactly so; order drops
// names other than "face" and "fingerprint".
// nullopt for a config with any bad value or another schema_version: never
// a mix of parsed values and defaults, and never defaults alone (they would
// turn face back on for a user who turned it off).
std::optional<BiopassConfig> parseConfig(const std::string& text);
// nullopt when the user is unknown (see resolveUser), or config.yaml is
// missing, unreadable, not a regular file or rejected by parseConfig().
std::optional<BiopassConfig> readConfig(const std::string& username);

// Whether a faces/ entry name is a face image: .jpg, .jpeg or .png (ASCII
// case-insensitive) after a non-empty stem. The app's face.rs
// has_image_extension must accept the same names; both are tested against
// auth/test/face_image_name/cases.txt.
bool hasImageExtension(const std::string& name);
// Image files in the user's faces/ dir, sorted. Empty for an unknown user.
std::vector<std::string> listFaces(const std::string& username);
// Creates faces/, debugs/ and logs/ (0700) under a data dir (see
// UserPaths::data_dir). Callers that run as root must hold a ScopedFsUser for
// the owner first, so a symlink in that dir can't make root create anything
// outside it.
int setupDataDirs(const std::string& dataDir);

}  // namespace biopass

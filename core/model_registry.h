#pragma once

#include <optional>
#include <string>

struct sqlite3;

namespace biopass {

// Path to the per-user biopass.db sqlite database (sibling of config.yaml,
// resolved the same getpwnam-first way as getConfigPath()).
std::string getDbPath(const std::string& username);

// A row of the `models` table, as far as the auth side needs it.
struct ModelInfo {
  std::string path;
  // Network architecture (e.g. "mobilenetv3", "minifasv2" for anti-spoofing).
  // Empty for tasks with a single implementation.
  std::string architecture;
};

// Read-only handle onto a user's biopass.db model catalog. Opens the
// connection once at construction and reuses it for every subsequent lookup
// -- callers that need to resolve several model_ids (e.g. detection +
// recognition + anti-spoofing, or repeated lookups across an authentication
// session) should keep one instance around instead of resolving through a
// function that opens/closes sqlite per call.
class ModelRegistry {
 public:
  explicit ModelRegistry(const std::string& username);
  ~ModelRegistry();

  ModelRegistry(const ModelRegistry&) = delete;
  ModelRegistry& operator=(const ModelRegistry&) = delete;

  // Resolves a model_id (as read from config.yaml's detection/recognition/
  // anti_spoofing model_id fields) to its absolute .onnx file path and
  // architecture by looking it up in the `models` table of biopass.db.
  // Returns nullopt on any failure (connection never opened, missing row,
  // sqlite error) rather than throwing -- callers should treat that the same
  // as "model not configured".
  std::optional<ModelInfo> resolveModel(const std::string& model_id) const;

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace biopass

#pragma once

#include <optional>
#include <string>

struct sqlite3;

namespace biopass {

// Per-user biopass.db, next to config.yaml.
std::string getDbPath(const std::string& username);

struct ModelInfo {
  std::string path;
  // e.g. "minifasv2"; empty for tasks with a single implementation.
  std::string architecture;
};

// Read-only view of biopass.db's `models` table, from a snapshot of the
// file taken at construction. Reuse the instance for multiple lookups.
class ModelRegistry {
 public:
  explicit ModelRegistry(const std::string& username);
  ~ModelRegistry();

  ModelRegistry(const ModelRegistry&) = delete;
  ModelRegistry& operator=(const ModelRegistry&) = delete;

  // model_id -> .onnx path + architecture. nullopt on any failure (treat
  // as "not configured"); never throws.
  std::optional<ModelInfo> resolveModel(const std::string& model_id) const;

 private:
  // The db file's bytes; db_ reads them in place, so they outlive it.
  std::string db_bytes_;
  sqlite3* db_ = nullptr;
};

}  // namespace biopass

#pragma once

#include <optional>
#include <string>

struct sqlite3;

namespace biopass {

struct ModelInfo {
  std::string path;
  // e.g. "minifasv2"; empty for tasks with a single implementation.
  std::string architecture;
};

// Read-only view of biopass.db's `models` table, from a snapshot of the
// file taken at construction. Reuse the instance for multiple lookups.
class ModelRegistry {
 public:
  // The user's biopass.db (UserPaths::dbFile). Empty for an unknown user.
  explicit ModelRegistry(const std::string& username);
  // An explicit db file, still read as `username`.
  ModelRegistry(const std::string& username, const std::string& db_path);
  ~ModelRegistry();

  ModelRegistry(const ModelRegistry&) = delete;
  ModelRegistry& operator=(const ModelRegistry&) = delete;

  // model_id -> .onnx path + architecture. nullopt on any failure (treat
  // as "not configured"); never throws.
  std::optional<ModelInfo> resolveModel(const std::string& model_id) const;

 private:
  void load(const std::string& username, const std::string& db_path);

  // The db file's bytes; db_ reads them in place, so they outlive it.
  std::string db_bytes_;
  sqlite3* db_ = nullptr;
};

}  // namespace biopass

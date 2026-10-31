#include "model_registry.h"

#include <pwd.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <unistd.h>

#include "user_file.h"

namespace biopass {

namespace {

// A registry of a few models is a few dozen KiB.
constexpr size_t kMaxDbBytes = 64 << 20;

}  // namespace

std::string getDbPath(const std::string& username) {
  struct passwd* pw = getpwnam(username.c_str());
  if (pw == nullptr) {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + "/.config/com.ticklab.biopass/biopass.db";
    }
    return "/etc/com.ticklab.biopass/biopass.db";
  }
  return std::string(pw->pw_dir) + "/.config/com.ticklab.biopass/biopass.db";
}

ModelRegistry::ModelRegistry(const std::string& username) {
  const std::string db_path = getDbPath(username);

  // The db lives in the user's home: read it as the user, only if it is a
  // regular file, then query a read-only in-memory copy. sqlite itself then
  // opens nothing by path (no journal lookups as root either).
  std::optional<std::string> bytes = readUserFile(username, db_path, kMaxDbBytes);
  if (!bytes) {
    spdlog::warn("Biopass: Model registry not found or not readable at {}", db_path);
    return;
  }
  db_bytes_ = std::move(*bytes);

  const auto size = static_cast<sqlite3_int64>(db_bytes_.size());
  if (sqlite3_open_v2(":memory:", &db_, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK ||
      sqlite3_deserialize(db_, "main", reinterpret_cast<unsigned char*>(db_bytes_.data()), size,
                          size, SQLITE_DESERIALIZE_READONLY) != SQLITE_OK) {
    spdlog::warn("Biopass: Failed to open model registry at {}: {}", db_path,
                 db_ ? sqlite3_errmsg(db_) : "unknown error");
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }
}

ModelRegistry::~ModelRegistry() {
  if (db_) {
    sqlite3_close(db_);
  }
}

std::optional<ModelInfo> ModelRegistry::resolveModel(const std::string& model_id) const {
  if (model_id.empty() || db_ == nullptr) {
    return std::nullopt;
  }

  sqlite3_stmt* stmt = nullptr;
  static const char* kQuery = "SELECT path, architecture FROM models WHERE id = ?1";
  if (sqlite3_prepare_v2(db_, kQuery, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::warn("Biopass: Failed to prepare model lookup query: {}", sqlite3_errmsg(db_));
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, model_id.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<ModelInfo> result = std::nullopt;
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char* path = sqlite3_column_text(stmt, 0);
    const unsigned char* architecture = sqlite3_column_text(stmt, 1);
    if (path != nullptr) {
      result =
          ModelInfo{std::string(reinterpret_cast<const char*>(path)),
                    architecture ? std::string(reinterpret_cast<const char*>(architecture)) : ""};
    }
  } else if (rc != SQLITE_DONE) {
    spdlog::warn("Biopass: Failed to query model '{}': {}", model_id, sqlite3_errmsg(db_));
  }

  sqlite3_finalize(stmt);
  return result;
}

}  // namespace biopass

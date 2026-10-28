#include "model_registry.h"

#include <pwd.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <unistd.h>

namespace biopass {

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

  if (sqlite3_open_v2(db_path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    spdlog::warn("Biopass: Failed to open model registry at {}: {}", db_path,
                 db_ ? sqlite3_errmsg(db_) : "unknown error");
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return;
  }
  sqlite3_busy_timeout(db_, 2000);
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

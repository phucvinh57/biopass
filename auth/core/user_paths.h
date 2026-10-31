#pragma once

#include <sys/types.h>

#include <optional>
#include <string>

namespace biopass {

// Where a user's Biopass files live, from their passwd entry.
struct UserPaths {
  uid_t uid = 0;
  gid_t gid = 0;
  std::string home;
  // <home>/.config/com.ticklab.biopass: config.yaml and biopass.db.
  std::string config_dir;
  // <home>/.local/share/com.ticklab.biopass: faces/, debugs/ and logs/.
  std::string data_dir;

  std::string configFile() const { return config_dir + "/config.yaml"; }
  std::string dbFile() const { return config_dir + "/biopass.db"; }
  std::string facesDir() const { return data_dir + "/faces"; }
  std::string debugsDir() const { return data_dir + "/debugs"; }
  std::string logsDir() const { return data_dir + "/logs"; }
};

// Looks the user up with getpwnam_r (thread-safe). nullopt when the user has
// no passwd entry or the home dir isn't an absolute path. There is no
// fallback ($HOME, /etc): a user that can't be resolved has no Biopass files.
std::optional<UserPaths> resolveUser(const std::string& username);

}  // namespace biopass

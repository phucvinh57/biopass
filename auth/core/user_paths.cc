#include "user_paths.h"

#include <pwd.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

namespace biopass {

namespace {

// Large enough for any sane passwd entry; ERANGE beyond it means "unknown".
constexpr size_t kMaxPwBuf = 1 << 20;

}  // namespace

std::optional<UserPaths> resolveUser(const std::string& username) {
  if (username.empty()) {
    return std::nullopt;
  }
  const long hint = sysconf(_SC_GETPW_R_SIZE_MAX);
  std::vector<char> buf(hint > 0 ? static_cast<size_t>(hint) : 16384);
  struct passwd pw{};
  struct passwd* result = nullptr;
  int rc;
  while ((rc = getpwnam_r(username.c_str(), &pw, buf.data(), buf.size(), &result)) == ERANGE &&
         buf.size() < kMaxPwBuf) {
    buf.resize(buf.size() * 2);
  }
  if (rc != 0 || result == nullptr || pw.pw_dir == nullptr || pw.pw_dir[0] != '/') {
    return std::nullopt;
  }

  UserPaths paths;
  paths.uid = pw.pw_uid;
  paths.gid = pw.pw_gid;
  paths.home = pw.pw_dir;
  paths.config_dir = paths.home + "/.config/com.ticklab.biopass";
  paths.data_dir = paths.home + "/.local/share/com.ticklab.biopass";
  return paths;
}

}  // namespace biopass

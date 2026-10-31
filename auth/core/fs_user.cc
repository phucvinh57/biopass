#include "fs_user.h"

#include <pwd.h>
#include <sys/fsuid.h>
#include <unistd.h>

#include <vector>

namespace biopass {

ScopedFsUser::ScopedFsUser(uid_t uid, gid_t gid) { enter(uid, gid); }

ScopedFsUser::ScopedFsUser(const std::string& username) {
  if (geteuid() != 0) {
    ok_ = true;
    return;
  }
  long size = sysconf(_SC_GETPW_R_SIZE_MAX);
  std::vector<char> buf(size > 0 ? static_cast<size_t>(size) : 16384);
  struct passwd pw{};
  struct passwd* result = nullptr;
  if (getpwnam_r(username.c_str(), &pw, buf.data(), buf.size(), &result) != 0 ||
      result == nullptr) {
    return;
  }
  enter(pw.pw_uid, pw.pw_gid);
}

void ScopedFsUser::enter(uid_t uid, gid_t gid) {
  if (geteuid() != 0) {
    ok_ = true;
    return;
  }
  // setfsuid/setfsgid return the previous value and never report errors, so
  // call them again to read back what is in effect.
  prev_gid_ = static_cast<gid_t>(setfsgid(gid));
  prev_uid_ = static_cast<uid_t>(setfsuid(uid));
  switched_ = true;
  ok_ = static_cast<uid_t>(setfsuid(uid)) == uid && static_cast<gid_t>(setfsgid(gid)) == gid;
}

ScopedFsUser::~ScopedFsUser() {
  if (switched_) {
    setfsuid(prev_uid_);
    setfsgid(prev_gid_);
  }
}

}  // namespace biopass

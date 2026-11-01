#include "fs_user.h"

#include <sys/fsuid.h>
#include <unistd.h>

#include <optional>

#include "user_paths.h"

namespace biopass {

ScopedFsUser::ScopedFsUser(uid_t uid, gid_t gid) { enter(uid, gid); }

ScopedFsUser::ScopedFsUser(const std::string& username) {
  const std::optional<UserPaths> user = resolveUser(username);
  if (!user) {
    return;
  }
  enter(user->uid, user->gid);
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

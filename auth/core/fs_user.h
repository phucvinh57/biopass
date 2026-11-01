#pragma once

#include <sys/types.h>

#include <string>

namespace biopass {

// Runs this thread's filesystem calls as another user (setfsuid/setfsgid)
// until the guard is destroyed. Switching the filesystem uid away from 0
// makes the kernel drop CAP_DAC_OVERRIDE, CAP_CHOWN and the other file
// capabilities from the thread, so every path component is checked with the
// user's permissions. A symlink the user planted can then only reach files
// the user could already write. New files are owned by the user, so no chown
// is needed.
//
// No-op (and ok()) when the process isn't root. Per-thread, so it is safe to
// use while other auth methods run in parallel threads.
class ScopedFsUser {
 public:
  ScopedFsUser(uid_t uid, gid_t gid);
  // Resolves the user with resolveUser(). ok() is false if the user is
  // unknown, root or not.
  explicit ScopedFsUser(const std::string& username);
  ~ScopedFsUser();

  ScopedFsUser(const ScopedFsUser&) = delete;
  ScopedFsUser& operator=(const ScopedFsUser&) = delete;

  // True when filesystem calls now run as the user (or we aren't root).
  // Callers must not write anything when this is false.
  bool ok() const { return ok_; }

 private:
  void enter(uid_t uid, gid_t gid);

  bool ok_ = false;
  bool switched_ = false;
  uid_t prev_uid_ = 0;
  gid_t prev_gid_ = 0;
};

}  // namespace biopass

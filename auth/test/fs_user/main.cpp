// Checks that root-run code can't be tricked into writing through symlinks a
// user planted in their own data dir (core/fs_user.h). Needs root: run it
// with sudo or inside `unshare --map-root-user --map-auto`. Exits 77 (ctest
// SKIP) when not root.

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "auth_config.h"
#include "fs_user.h"

namespace {

constexpr uid_t kUser = 65534;
constexpr gid_t kGroup = 65534;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

std::string makeTempDir(const char* tag) {
  std::string tmpl = std::string("/tmp/biopass-fsuser-") + tag + "-XXXXXX";
  char* dir = mkdtemp(tmpl.data());
  return dir ? std::string(dir) : std::string();
}

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool ownedBy(const std::string& path, uid_t uid) {
  struct stat st{};
  return lstat(path.c_str(), &st) == 0 && st.st_uid == uid;
}

mode_t modeOf(const std::string& path) {
  struct stat st{};
  lstat(path.c_str(), &st);
  return st.st_mode & 07777;
}

}  // namespace

int main() {
  if (geteuid() != 0) {
    std::printf("SKIP: needs root (sudo or unshare --map-root-user --map-auto)\n");
    return 77;
  }

  // A root-owned file the user must not be able to touch.
  const std::string victim_dir = makeTempDir("victim");
  const std::string victim = victim_dir + "/victim";
  {
    std::ofstream(victim) << "secret";
  }
  chmod(victim.c_str(), 0644);

  // The user's home, owned by the user.
  const std::string home = makeTempDir("home");
  expect(chown(home.c_str(), kUser, kGroup) == 0, "chown temp home to user");
  const std::string data = home + "/data";

  {
    biopass::ScopedFsUser as_user(kUser, kGroup);
    expect(as_user.ok(), "switching filesystem user succeeds as root");
    expect(biopass::setupDataDirs(data) == 0, "setupDataDirs creates the tree as the user");
  }
  expect(ownedBy(data, kUser) && ownedBy(data + "/logs", kUser), "data dirs are owned by the user");
  expect(modeOf(data + "/faces") == 0700, "faces dir is private (0700)");

  // The user plants a symlink where root will write its log.
  const std::string log = data + "/logs/today.log";
  expect(symlink(victim.c_str(), log.c_str()) == 0, "plant symlink to victim");
  {
    biopass::ScopedFsUser as_user(kUser, kGroup);
    FILE* f = std::fopen(log.c_str(), "a");
    expect(f == nullptr && errno == EACCES, "write through symlink is denied");
    if (f) {
      std::fputs("pwned", f);
      std::fclose(f);
    }
  }
  expect(readFile(victim) == "secret", "victim content unchanged");
  expect(ownedBy(victim, 0), "victim still owned by root");

  // A whole directory swapped for a symlink to a root-owned dir.
  const std::string debugs = data + "/debugs";
  rmdir(debugs.c_str());
  expect(symlink(victim_dir.c_str(), debugs.c_str()) == 0, "plant symlinked debugs dir");
  {
    biopass::ScopedFsUser as_user(kUser, kGroup);
    expect(biopass::setupDataDirs(data) == 0, "setupDataDirs tolerates existing entries");
    FILE* f = std::fopen((debugs + "/new.jpg").c_str(), "w");
    expect(f == nullptr, "create inside symlinked root dir is denied");
    if (f) {
      std::fclose(f);
    }
  }
  expect(ownedBy(victim_dir, 0), "victim dir still owned by root");

  // Root access comes back once the guard is gone.
  {
    FILE* f = std::fopen(victim.c_str(), "a");
    expect(f != nullptr, "root can write again after the guard");
    if (f) {
      std::fclose(f);
    }
  }

  biopass::ScopedFsUser unknown("biopass-no-such-user");
  expect(!unknown.ok(), "unknown user is not ok()");

  std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}

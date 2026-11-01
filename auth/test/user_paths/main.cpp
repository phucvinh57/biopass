// Checks resolveUser (core/user_paths.h), and that a user with no passwd
// entry gets no config or faces even when $HOME holds them: paths come from
// the passwd entry only, never from $HOME or a system-wide fallback. Exits
// non-zero on any failure.

#include <pwd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

#include "auth_config.h"
#include "user_paths.h"

namespace {

const char* kUnknown = "biopass-no-such-user";

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

bool writeFile(const std::string& path, const std::string& text) {
  std::ofstream out(path);
  out << text;
  return static_cast<bool>(out);
}

// $HOME with a config and an enrolled face, where the old $HOME fallback
// found them.
bool fillHome(const std::string& home) {
  const std::string config = home + "/.config/com.ticklab.biopass";
  const std::string data = home + "/.local/share/com.ticklab.biopass";
  for (const std::string& dir : {home + "/.config", config, home + "/.local",
                                 home + "/.local/share", data, data + "/faces"}) {
    if (mkdir(dir.c_str(), 0700) != 0) {
      return false;
    }
  }
  return writeFile(config + "/config.yaml", "schema_version: 2\nstrategy: {debug: true}\n") &&
         writeFile(data + "/faces/me.jpg", "not really a jpeg");
}

}  // namespace

int main() {
  std::string tmpl = "/tmp/biopass-user-paths-XXXXXX";
  const char* home = mkdtemp(tmpl.data());
  expect(home != nullptr, "create temp home");
  if (home == nullptr) {
    return 1;
  }
  expect(fillHome(home), "fill temp home with config and a face");
  setenv("HOME", home, 1);

  expect(!biopass::readConfig(kUnknown), "unknown user: no config, $HOME is not used");
  expect(biopass::listFaces(kUnknown).empty(), "unknown user: no faces, $HOME is not used");

  expect(!biopass::resolveUser(kUnknown), "resolveUser: unknown user -> nullopt");
  expect(!biopass::resolveUser(""), "resolveUser: empty name -> nullopt");

  const struct passwd* self = getpwuid(geteuid());
  expect(self != nullptr, "current user has a passwd entry");
  if (self != nullptr) {
    const std::string pw_home = self->pw_dir;
    const std::optional<biopass::UserPaths> user = biopass::resolveUser(self->pw_name);
    expect(user && user->uid == self->pw_uid && user->gid == self->pw_gid,
           "resolveUser: current user -> its uid and gid");
    expect(user && user->home == pw_home && user->home != home,
           "resolveUser: home comes from passwd, not $HOME");
    expect(user && user->configFile() == pw_home + "/.config/com.ticklab.biopass/config.yaml" &&
               user->dbFile() == pw_home + "/.config/com.ticklab.biopass/biopass.db",
           "resolveUser: config and db paths");
    expect(user && user->facesDir() == pw_home + "/.local/share/com.ticklab.biopass/faces" &&
               user->debugsDir() == pw_home + "/.local/share/com.ticklab.biopass/debugs" &&
               user->logsDir() == pw_home + "/.local/share/com.ticklab.biopass/logs",
           "resolveUser: data paths");
  }

  std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}

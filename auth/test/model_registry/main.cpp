// Checks that ModelRegistry (core/model_registry.h) reads biopass.db only
// when it is a regular file, and never blocks or reads a device. Uses an
// unknown user so the db path falls back to $HOME; root refuses unknown
// users, so this exits 77 (ctest SKIP) when run as root.

#include <sqlite3.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "model_registry.h"

namespace {

const char* kUser = "biopass-no-such-user";

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

bool createDb(const std::string& path) {
  sqlite3* db = nullptr;
  bool ok = sqlite3_open(path.c_str(), &db) == SQLITE_OK &&
            sqlite3_exec(db,
                         "CREATE TABLE models (id TEXT PRIMARY KEY, path TEXT, architecture TEXT);"
                         "INSERT INTO models VALUES ('det', '/models/det.onnx', NULL);"
                         "INSERT INTO models VALUES ('as', '/models/as.onnx', 'minifasv2');",
                         nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close(db);
  return ok;
}

}  // namespace

int main() {
  if (geteuid() == 0) {
    std::printf("SKIP: run as a normal user\n");
    return 77;
  }

  std::string tmpl = "/tmp/biopass-registry-XXXXXX";
  const char* home = mkdtemp(tmpl.data());
  expect(home != nullptr, "create temp home");
  if (home == nullptr) {
    return 1;
  }
  setenv("HOME", home, 1);
  const std::string dir = std::string(home) + "/.config";
  mkdir(dir.c_str(), 0700);
  mkdir((dir + "/com.ticklab.biopass").c_str(), 0700);
  const std::string db_path = biopass::getDbPath(kUser);
  expect(db_path == dir + "/com.ticklab.biopass/biopass.db", "db path falls back to $HOME");

  expect(createDb(db_path), "create registry db");
  {
    biopass::ModelRegistry registry(kUser);
    const auto det = registry.resolveModel("det");
    expect(det && det->path == "/models/det.onnx" && det->architecture.empty(),
           "regular db resolves a model");
    const auto as = registry.resolveModel("as");
    expect(as && as->architecture == "minifasv2", "regular db resolves the architecture");
    expect(!registry.resolveModel("missing"), "unknown model id is not resolved");
  }

  unlink(db_path.c_str());
  expect(symlink("/dev/zero", db_path.c_str()) == 0, "plant db symlink to /dev/zero");
  {
    biopass::ModelRegistry registry(kUser);
    expect(!registry.resolveModel("det"), "db symlinked to /dev/zero resolves nothing");
  }

  unlink(db_path.c_str());
  expect(mkfifo(db_path.c_str(), 0600) == 0, "create db FIFO");
  alarm(10);  // a hang becomes a failed run
  {
    biopass::ModelRegistry registry(kUser);
    expect(!registry.resolveModel("det"), "db FIFO resolves nothing without blocking");
  }
  alarm(0);

  std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}

// Checks the sealed password store (keyring/sealed_store.h) in a temp dir:
// modes, atomic replace, and refusing symlinks and loose permissions.

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "sealed_store.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

int finish() {
  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

}  // namespace

mode_t modeOf(const std::string& path) {
  struct stat st{};
  return lstat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0;
}

std::string readText(const std::string& path) {
  std::string text;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    char buf[64];
    const size_t n = std::fread(buf, 1, sizeof(buf), f);
    text.assign(buf, n);
    std::fclose(f);
  }
  return text;
}

int main() {
  char tmpl[] = "/tmp/biopass-store-XXXXXX";
  const char* root = mkdtemp(tmpl);
  if (root == nullptr) {
    std::printf("mkdtemp failed\n");
    return 1;
  }
  umask(022);
  const std::string base = std::string(root) + "/lib";
  biopass::SealedStore store{base + "/keyring", geteuid()};
  const std::string file = store.dir + "/1000.sealed";
  std::vector<uint8_t> out;

  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::NotFound,
         "nothing stored yet");
  expect(biopass::removeSealed(store, 1000) == biopass::StoreResult::NotFound,
         "forget with nothing stored");

  const std::vector<uint8_t> first = {1, 2, 3, 4};
  expect(biopass::writeSealed(store, 1000, first) == biopass::StoreResult::Ok, "write");
  expect(modeOf(base) == 0755, "parent dir is 0755");
  expect(modeOf(store.dir) == 0711, "store dir is 0711");
  expect(modeOf(file) == 0600, "file is 0600");
  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::Ok && out == first,
         "read back");

  const std::vector<uint8_t> second = {9, 9};
  expect(biopass::writeSealed(store, 1000, second) == biopass::StoreResult::Ok, "replace");
  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::Ok && out == second,
         "read replaced");
  expect(access((store.dir + "/.1000.sealed.tmp").c_str(), F_OK) != 0, "no temp file left");
  expect(biopass::readSealed(store, 1001, out) == biopass::StoreResult::NotFound,
         "other uid has nothing");
  expect(biopass::writeSealed(store, 1000, {}) == biopass::StoreResult::Error,
         "empty data refused");

  chmod(file.c_str(), 0644);
  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::Unsafe,
         "world-readable file refused");
  chmod(file.c_str(), 0600);

  chmod(store.dir.c_str(), 0731);
  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::Unsafe,
         "group-writable dir refused");
  expect(biopass::writeSealed(store, 1000, first) == biopass::StoreResult::Unsafe,
         "group-writable dir refused for writes");
  chmod(store.dir.c_str(), 0711);

  const std::string target = std::string(root) + "/elsewhere";
  expect(symlink(target.c_str(), (store.dir + "/1002.sealed").c_str()) == 0, "plant symlink");
  expect(biopass::readSealed(store, 1002, out) == biopass::StoreResult::Unsafe,
         "symlinked file refused");
  expect(biopass::writeSealed(store, 1002, first) == biopass::StoreResult::Ok &&
             modeOf(store.dir + "/1002.sealed") == 0600 && access(target.c_str(), F_OK) != 0,
         "write replaces the symlink, never follows it");

  biopass::SealedStore linked{std::string(root) + "/link/keyring", geteuid()};
  expect(symlink(base.c_str(), (std::string(root) + "/link").c_str()) == 0, "plant dir symlink");
  expect(biopass::readSealed(linked, 1000, out) != biopass::StoreResult::Ok,
         "symlinked parent dir refused");

  const std::string mode_file = store.dir + "/1000.mode";
  expect(biopass::removeSealedMode(store, 1000) == biopass::StoreResult::NotFound,
         "no mode file yet");
  expect(biopass::writeSealedMode(store, 1000, true) == biopass::StoreResult::Ok &&
             modeOf(mode_file) == 0644 && readText(mode_file) == "pcr7\n",
         "mode file says pcr7, readable by the user");
  expect(biopass::writeSealedMode(store, 1000, false) == biopass::StoreResult::Ok &&
             readText(mode_file) == "none\n" &&
             access((store.dir + "/.1000.mode.tmp").c_str(), F_OK) != 0,
         "mode file replaced with none");
  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::Ok && out == second,
         "mode file leaves the sealed file alone");
  expect(biopass::removeSealedMode(store, 1000) == biopass::StoreResult::Ok &&
             access(mode_file.c_str(), F_OK) != 0,
         "mode file removed");

  expect(biopass::removeSealed(store, 1000) == biopass::StoreResult::Ok, "forget");
  expect(biopass::readSealed(store, 1000, out) == biopass::StoreResult::NotFound, "gone");

  biopass::SealedStore wrong_owner{store.dir, geteuid() + 1};
  expect(biopass::readSealed(wrong_owner, 1002, out) == biopass::StoreResult::Unsafe,
         "dir owned by someone else refused");

  const std::string cleanup = std::string("rm -rf '") + root + "'";
  if (system(cleanup.c_str()) != 0) {
    std::printf("cleanup failed\n");
  }
  return finish();
}

// Checks password verification against crypt(3) hashes
// (keyring/shadow_check.h), with hashes made here for yescrypt and SHA-512.

#include <crypt.h>

#include <cstdio>
#include <string>

#include "shadow_check.h"

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

std::string makeHash(const char* prefix, const char* password) {
  char salt[CRYPT_GENSALT_OUTPUT_SIZE];
  if (crypt_gensalt_rn(prefix, 0, nullptr, 0, salt, sizeof(salt)) == nullptr) {
    return {};
  }
  struct crypt_data data{};
  const char* hash = crypt_r(password, salt, &data);
  return hash != nullptr && hash[0] != '*' ? std::string(hash) : std::string();
}

int main() {
  using biopass::PasswordCheck;
  for (const char* prefix : {"$y$", "$6$"}) {
    const std::string tag = std::string(" (") + prefix + ")";
    const std::string hash = makeHash(prefix, "correct horse");
    expect(!hash.empty(), "made a hash" + tag);
    expect(biopass::verifyAgainstHash("correct horse", hash.c_str()) == PasswordCheck::Match,
           "right password matches" + tag);
    expect(biopass::verifyAgainstHash("correct hors", hash.c_str()) == PasswordCheck::Mismatch,
           "wrong password" + tag);
    expect(biopass::verifyAgainstHash("", hash.c_str()) == PasswordCheck::Mismatch,
           "empty password" + tag);
    expect(
        biopass::verifyAgainstHash("correct horse", ("!" + hash).c_str()) == PasswordCheck::Locked,
        "locked (!) hash" + tag);
  }
  expect(biopass::verifyAgainstHash("x", "") == PasswordCheck::Locked, "empty hash is locked");
  expect(biopass::verifyAgainstHash("x", "*") == PasswordCheck::Locked, "* is locked");
  expect(biopass::verifyAgainstHash("x", "!") == PasswordCheck::Locked, "! is locked");
  expect(biopass::verifyAgainstHash("x", nullptr) == PasswordCheck::Locked, "null is locked");
  expect(biopass::verifyAgainstHash("x", "$nope$abc") == PasswordCheck::Error,
         "unknown method is an error");
  // Not root (or no such user): no shadow entry to read.
  expect(biopass::verifyLocalPassword("biopass-no-such-user-xyz", "x") ==
             PasswordCheck::NoLocalAccount,
         "unknown user has no local account");
  return finish();
}

// Checks PKEXEC_UID parsing (keyring/pkexec_uid.h): biopass-keyring runs as
// root and seals for this uid, so anything but a plain non-root uid is refused.

#include <cstdio>
#include <string>

#include "pkexec_uid.h"

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

int main() {
  expect(biopass::parsePkexecUid("1000") == std::optional<uid_t>(1000), "plain uid");
  expect(biopass::parsePkexecUid("1") == std::optional<uid_t>(1), "uid 1");
  expect(biopass::parsePkexecUid("4294967294") == std::optional<uid_t>(4294967294u),
         "largest valid uid");
  for (const char* bad : {(const char*)nullptr, "", "0", "00", "01000", "-1", "+1000", " 1000",
                          "1000 ", "10a", "4294967295", "4294967296", "99999999999999999999"}) {
    expect(!biopass::parsePkexecUid(bad).has_value(),
           std::string("rejects '") + (bad ? bad : "(null)") + "'");
  }
  return finish();
}

// Checks which PAM services get the saved keyring password
// (keyring/keyring_services.h): login screens only.

#include <cstdio>
#include <string>

#include "keyring_services.h"

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
  for (const char* svc : {"gdm-password", "sddm", "lightdm", "login", "greetd", "lxdm", "ly"}) {
    expect(biopass::isKeyringService(svc), std::string("login service ") + svc);
  }
  for (const char* svc : {"sudo", "su", "polkit-1", "pkexec", "kde", "gdm-fingerprint", "",
                          "GDM-PASSWORD", "gdm-password ", "swaylock"}) {
    expect(!biopass::isKeyringService(svc), std::string("not for '") + svc + "'");
  }
  return finish();
}

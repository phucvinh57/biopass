// Checks the argv the PAM module builds for biopass-helper
// (pam/helper_argv.h): untrusted values must each stay inside their own
// `--option=value` argument. Exits non-zero on any failure.

#include <cstdio>
#include <string>
#include <vector>

#include "helper_argv.h"

namespace {

using Args = std::vector<std::string>;

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

}  // namespace

int main() {
  expect(biopass::buildHelperArgv("alice", "sudo", 7) ==
             Args{"biopass-helper", "auth", "--username=alice", "--service=sudo", "--status-fd=7"},
         "full argv");
  expect(biopass::buildHelperArgv("alice", nullptr, -1) ==
             Args{"biopass-helper", "auth", "--username=alice"},
         "no service, no status fd");
  expect(biopass::buildHelperArgv("alice", "", -1) ==
             Args{"biopass-helper", "auth", "--username=alice"},
         "empty service is left out");

  for (const char* name : {"--help", "-h", "--version", "-v", "--username", "", "a=b", "x y"}) {
    const Args args = biopass::buildHelperArgv(name, "--help", -1);
    expect(args == Args{"biopass-helper", "auth", std::string("--username=") + name,
                        "--service=--help"},
           std::string("hostile name '") + name + "' stays one argument");
  }

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

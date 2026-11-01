// Checks the helper exit-code contract (core/helper_exit.h) and how the PAM
// module maps a real waitpid() status back to a PAM code
// (pam/helper_status.h). Exits non-zero on any failure.

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>

#include "helper_exit.h"
#include "helper_status.h"

namespace {

using biopass::AuthOutcome;
using biopass::HelperExit;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

// Status of a child that exits with `code`, or dies from `sig` when non-zero.
int childStatus(int code, int sig = 0) {
  pid_t pid = fork();
  if (pid == 0) {
    if (sig != 0) {
      signal(sig, SIG_DFL);
      raise(sig);
    }
    _exit(code);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return status;
}

}  // namespace

int main() {
  // Numbers are a contract with installed PAM modules and the app.
  expect(biopass::exitCode(HelperExit::Ok) == 0, "Ok is exit 0");
  expect(biopass::exitCode(HelperExit::AuthErr) == 1, "AuthErr is exit 1");
  expect(biopass::exitCode(HelperExit::Ignore) == 2, "Ignore is exit 2");

  expect(biopass::toHelperExit(AuthOutcome::Success) == HelperExit::Ok, "Success -> Ok");
  expect(biopass::toHelperExit(AuthOutcome::Failure) == HelperExit::AuthErr, "Failure -> AuthErr");
  expect(biopass::toHelperExit(AuthOutcome::Ignore) == HelperExit::Ignore, "Ignore -> Ignore");

  expect(biopass::pamCodeForHelperStatus(childStatus(0)) == PAM_SUCCESS, "exit 0 -> PAM_SUCCESS");
  expect(biopass::pamCodeForHelperStatus(childStatus(1)) == PAM_AUTH_ERR, "exit 1 -> PAM_AUTH_ERR");
  expect(biopass::pamCodeForHelperStatus(childStatus(2)) == PAM_IGNORE, "exit 2 -> PAM_IGNORE");
  expect(biopass::pamCodeForHelperStatus(childStatus(3)) == PAM_AUTH_ERR,
         "unknown exit 3 -> PAM_AUTH_ERR");
  expect(biopass::pamCodeForHelperStatus(childStatus(255)) == PAM_AUTH_ERR,
         "unknown exit 255 -> PAM_AUTH_ERR");
  expect(biopass::pamCodeForHelperStatus(childStatus(0, SIGKILL)) == PAM_AUTH_ERR,
         "killed by SIGKILL -> PAM_AUTH_ERR");
  expect(biopass::pamCodeForHelperStatus(childStatus(0, SIGTERM)) == PAM_AUTH_ERR,
         "killed by SIGTERM -> PAM_AUTH_ERR");

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

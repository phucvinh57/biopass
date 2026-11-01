#pragma once

#include <security/_pam_types.h>
#include <sys/wait.h>

#include "helper_exit.h"

namespace biopass {

// Maps a waitpid() status of `biopass-helper auth` to the PAM result. Fails
// closed: only a normal exit with HelperExit::Ok is PAM_SUCCESS; signals and
// unknown codes are PAM_AUTH_ERR.
inline int pamCodeForHelperStatus(int wait_status) {
  if (!WIFEXITED(wait_status)) {
    return PAM_AUTH_ERR;  // e.g. killed by a signal
  }
  switch (WEXITSTATUS(wait_status)) {
    case exitCode(HelperExit::Ok):
      return PAM_SUCCESS;
    case exitCode(HelperExit::Ignore):
      return PAM_IGNORE;
    default:
      return PAM_AUTH_ERR;
  }
}

}  // namespace biopass

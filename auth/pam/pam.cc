#include <fcntl.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "authtok_channel.h"
#include "helper_argv.h"
#include "helper_status.h"
#include "helper_wait.h"

namespace {

// Shows one message through the application's conversation function. Does
// nothing when there is none (non-interactive callers) or when it fails:
// status text is a courtesy and must never change the authentication result.
void sendMessage(pam_handle_t* pamh, int style, const std::string& text) {
  const void* item = nullptr;
  if (pam_get_item(pamh, PAM_CONV, &item) != PAM_SUCCESS || item == nullptr) {
    return;
  }
  const struct pam_conv* conv = static_cast<const struct pam_conv*>(item);
  if (conv->conv == nullptr) {
    return;
  }

  struct pam_message msg;
  msg.msg_style = style;
  msg.msg = text.c_str();
  const struct pam_message* msgp = &msg;
  struct pam_response* resp = nullptr;
  if (conv->conv(1, &msgp, &resp, conv->appdata_ptr) == PAM_SUCCESS && resp != nullptr) {
    free(resp->resp);
    free(resp);
  }
}

// Relays one "<KIND> <text>" line from biopass-helper (protocol described in
// core/status_reporter.h). Unknown kinds are dropped.
void relayStatusLine(pam_handle_t* pamh, const std::string& line) {
  static constexpr char kInfo[] = "INFO ";
  static constexpr char kError[] = "ERROR ";
  if (line.compare(0, sizeof(kInfo) - 1, kInfo) == 0) {
    sendMessage(pamh, PAM_TEXT_INFO, line.substr(sizeof(kInfo) - 1));
  } else if (line.compare(0, sizeof(kError) - 1, kError) == 0) {
    sendMessage(pamh, PAM_ERROR_MSG, line.substr(sizeof(kError) - 1));
  }
}

// Sets PAM_AUTHTOK from what the helper wrote to `fd` (the saved keyring
// password, see docs/Keyring.md), unless another module set it first. Only
// called after a match; nothing here can change the result.
void takeAuthtok(pam_handle_t* pamh, int fd) {
  char buf[biopass::kMaxAuthtokLen + 2];
  if (biopass::readAuthtok(fd, buf, sizeof(buf)) > 0) {
    const void* existing = nullptr;
    if (pam_get_item(pamh, PAM_AUTHTOK, &existing) == PAM_SUCCESS && existing == nullptr) {
      pam_set_item(pamh, PAM_AUTHTOK, buf);  // libpam keeps its own copy
    }
  }
  explicit_bzero(buf, sizeof(buf));
}

// Forks and execs biopass-helper for `username`, relays its status lines
// unless `quiet`, and returns the PAM result. Any failure, or a helper still
// running after kHelperTimeout, is PAM_AUTH_ERR. With `want_authtok`, a
// match also sets PAM_AUTHTOK when the helper hands over a saved password.
int runHelperWithDeadline(pam_handle_t* pamh, const char* username, const char* service, bool quiet,
                          bool want_authtok) {
  // Pipe carrying status lines from the helper. Both ends are close-on-exec;
  // the child clears the flag on its write end only. If the pipe can't be
  // created, authenticate without status messages.
  int status_pipe[2] = {-1, -1};
  if (!quiet && pipe2(status_pipe, O_CLOEXEC) != 0) {
    status_pipe[0] = -1;
    status_pipe[1] = -1;
  }
  // Same for the authtok pipe; without it, auth goes on with no keyring.
  int authtok_pipe[2] = {-1, -1};
  if (want_authtok && pipe2(authtok_pipe, O_CLOEXEC) != 0) {
    authtok_pipe[0] = -1;
    authtok_pipe[1] = -1;
  }
  auto close_pipes = [&]() {
    for (int fd : {status_pipe[0], status_pipe[1], authtok_pipe[0], authtok_pipe[1]}) {
      if (fd >= 0) {
        close(fd);
      }
    }
  };

  // Built before fork(): allocation isn't async-signal-safe, and the PAM
  // caller may be multithreaded. No exception may cross into the C caller.
  // The relay callback too, so nothing between fork() and the wait can throw
  // and leave the helper running unwatched.
  std::vector<std::string> arg_strings;
  std::vector<char*> args;
  biopass::StatusLineFn on_line;
  try {
    arg_strings = biopass::buildHelperArgv(username, service, status_pipe[1], authtok_pipe[1]);
    for (std::string& arg : arg_strings) {
      args.push_back(arg.data());
    }
    args.push_back(nullptr);
    on_line = [pamh](const std::string& line) { relayStatusLine(pamh, line); };
  } catch (...) {
    args.clear();
  }

  pid_t pid = args.empty() ? -1 : fork();
  if (pid < 0) {
    close_pipes();
    return PAM_AUTH_ERR;
  }
  if (pid == 0) {
    if (status_pipe[1] >= 0) {
      close(status_pipe[0]);
      fcntl(status_pipe[1], F_SETFD, 0);
    }
    if (authtok_pipe[1] >= 0) {
      close(authtok_pipe[0]);
      fcntl(authtok_pipe[1], F_SETFD, 0);
    }

    // The helper runs with the PAM caller's privileges (often root), so it
    // gets a fixed environment instead of the caller's: LD_*, LIBCAMERA_IPA_*,
    // HOME and XDG_CONFIG_HOME would otherwise let the invoking user pick code
    // or config (e.g. libcamera IPA modules and proxy workers) it loads.
    char* const helper_env[] = {const_cast<char*>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"), nullptr};

    execve("/usr/bin/biopass-helper", args.data(), helper_env);

    // execve failed. No perror(): stdio is inherited from the PAM caller,
    // which may parse it as a strict line protocol (GNOME Shell's polkit
    // agent); a stray line derails its auth state machine.
    _exit(biopass::exitCode(biopass::HelperExit::AuthErr));
  }

  if (status_pipe[1] >= 0) {
    close(status_pipe[1]);
  }
  if (authtok_pipe[1] >= 0) {
    close(authtok_pipe[1]);
  }
  // noexcept: kills the helper and fails closed if relaying throws.
  const int result = biopass::waitForHelper(pid, status_pipe[0], biopass::kHelperTimeout, on_line);
  if (authtok_pipe[0] >= 0) {
    // The helper is reaped, so whatever it wrote is in the pipe already.
    if (result == PAM_SUCCESS) {
      takeAuthtok(pamh, authtok_pipe[0]);
    }
    close(authtok_pipe[0]);
  }
  return result;
}

}  // namespace

PAM_EXTERN int pam_sm_authenticate(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  // Status messages ("looking for your face", "camera appears covered", ...)
  // are suppressed by PAM_SILENT or the "quiet" module option.
  bool quiet = (flags & PAM_SILENT) != 0;
  // "authtok": after a match, hand the password saved in the app to the
  // modules below (pam_gnome_keyring, pam_kwallet). Opt-in, because a stack
  // that runs pam_unix with try_first_pass after this module would then take
  // it as a typed password. Only root can read the saved passwords.
  bool want_authtok = false;
  for (int i = 0; i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (strcmp(argv[i], "quiet") == 0) {
      quiet = true;
    } else if (strcmp(argv[i], "authtok") == 0) {
      want_authtok = geteuid() == 0;
    }
  }

  const char* service = nullptr;
  if (pam_get_item(pamh, PAM_SERVICE, (const void**)&service) != PAM_SUCCESS) {
    service = nullptr;
  }

  const char* username = nullptr;
  int retval = pam_get_user(pamh, &username, NULL);
  if (retval != PAM_SUCCESS) {
    return retval;
  }

  // C caller: no exception may cross this boundary.
  try {
    return runHelperWithDeadline(pamh, username, service, quiet, want_authtok);
  } catch (...) {
    return PAM_AUTH_ERR;
  }
}

// Required by PAM, unused here.
PAM_EXTERN int pam_sm_open_session(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)pamh;
  (void)flags;
  (void)argc;
  (void)argv;
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)pamh;
  (void)flags;
  (void)argc;
  (void)argv;
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)pamh;
  (void)flags;
  (void)argc;
  (void)argv;
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_chauthtok(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)pamh;
  (void)flags;
  (void)argc;
  (void)argv;
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)pamh;
  (void)flags;
  (void)argc;
  (void)argv;
  return PAM_IGNORE;
}

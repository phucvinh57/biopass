#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "helper_argv.h"
#include "helper_status.h"

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
  static const std::string kInfo = "INFO ";
  static const std::string kError = "ERROR ";
  if (line.compare(0, kInfo.size(), kInfo) == 0) {
    sendMessage(pamh, PAM_TEXT_INFO, line.substr(kInfo.size()));
  } else if (line.compare(0, kError.size(), kError) == 0) {
    sendMessage(pamh, PAM_ERROR_MSG, line.substr(kError.size()));
  }
}

// Relays status lines from `fd` as they arrive until the helper exits, then
// reaps it into `status`. Polls with a timeout instead of blocking until EOF
// so we still return if something else ends up holding the pipe's write end.
// Returns false if the helper could not be reaped (e.g. ECHILD because the
// caller ignores SIGCHLD or reaped it first); `status` is then meaningless.
bool relayUntilExit(pam_handle_t* pamh, int fd, pid_t pid, int* status) {
  std::string pending;
  bool fd_open = fd >= 0;
  bool reaped = false;

  for (;;) {
    if (fd_open) {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int ready = poll(&pfd, 1, 200);
      if (ready > 0) {
        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
          pending.append(buf, static_cast<size_t>(n));
          size_t nl;
          while ((nl = pending.find('\n')) != std::string::npos) {
            relayStatusLine(pamh, pending.substr(0, nl));
            pending.erase(0, nl + 1);
          }
          if (pending.size() > 4096) {
            pending.clear();  // Runaway line without a newline; drop it.
          }
          continue;
        }
        if (n == 0 || errno != EINTR) {
          fd_open = false;  // EOF or error: just wait for the child below.
        }
      } else if (ready < 0 && errno != EINTR) {
        fd_open = false;
      }
    }

    pid_t r = waitpid(pid, status, fd_open ? WNOHANG : 0);
    if (r == pid) {
      reaped = true;
      break;
    }
    if (r < 0 && errno != EINTR) {
      break;
    }
  }

  if (fd >= 0) {
    close(fd);
  }
  return reaped;
}

}  // namespace

PAM_EXTERN int pam_sm_authenticate(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  // Status messages ("looking for your face", "camera appears covered", ...)
  // are suppressed by PAM_SILENT or the "quiet" module option.
  bool quiet = (flags & PAM_SILENT) != 0;
  for (int i = 0; i < argc; ++i) {
    if (argv[i] != nullptr && strcmp(argv[i], "quiet") == 0) {
      quiet = true;
    }
  }

  int retval;

  const char* service = nullptr;
  retval = pam_get_item(pamh, PAM_SERVICE, (const void**)&service);
  if (retval != PAM_SUCCESS) {
    service = nullptr;
  }

  const char* pUsername;
  retval = pam_get_user(pamh, &pUsername, NULL);
  if (retval != PAM_SUCCESS) {
    return retval;
  }

  // Pipe carrying status lines from the helper. Both ends are close-on-exec;
  // the child clears the flag on its write end only. If the pipe can't be
  // created, authenticate without status messages.
  int status_pipe[2] = {-1, -1};
  if (!quiet && pipe2(status_pipe, O_CLOEXEC) != 0) {
    status_pipe[0] = -1;
    status_pipe[1] = -1;
  }

  // Built before fork(): allocation isn't async-signal-safe, and the PAM
  // caller may be multithreaded. No exception may cross into the C caller.
  std::vector<std::string> arg_strings;
  std::vector<char*> args;
  try {
    arg_strings = biopass::buildHelperArgv(pUsername, service, status_pipe[1]);
    for (std::string& arg : arg_strings) {
      args.push_back(arg.data());
    }
    args.push_back(nullptr);
  } catch (...) {
    args.clear();
  }

  pid_t pid = args.empty() ? -1 : fork();
  if (pid < 0) {
    if (status_pipe[0] >= 0) {
      close(status_pipe[0]);
      close(status_pipe[1]);
    }
    return PAM_AUTH_ERR;
  } else if (pid == 0) {
    if (status_pipe[1] >= 0) {
      close(status_pipe[0]);
      fcntl(status_pipe[1], F_SETFD, 0);
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
  } else {
    if (status_pipe[1] >= 0) {
      close(status_pipe[1]);
    }
    int status = 0;
    if (!relayUntilExit(pamh, status_pipe[0], pid, &status)) {
      // We never saw the helper's exit status, so we can't know it succeeded.
      return PAM_AUTH_ERR;
    }

    return biopass::pamCodeForHelperStatus(status);
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

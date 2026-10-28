#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Timeout in seconds for the biopass-helper child process.  If the helper
// hangs (e.g. D-Bus activation of fprintd stalls at boot) the alarm fires,
// kills the child, and PAM falls through to the password prompt instead of
// freezing the login screen.
static const int kHelperTimeout = 8;

// The forked child's PID so the SIGALRM handler can kill it.
static volatile pid_t g_child_pid = 0;

static void alarm_handler(int sig) {
  (void)sig;
  pid_t pid = g_child_pid;
  if (pid > 0) {
    kill(pid, SIGKILL);
  }
}

// Returns true if a display server (Wayland or X11) is available.
// At the login screen neither is set.  Once the user has a desktop session
// one of them will be present.
static bool has_display() {
  const char* wayland = getenv("WAYLAND_DISPLAY");
  const char* x11 = getenv("DISPLAY");
  return (wayland != nullptr && wayland[0] != '\0') ||
         (x11 != nullptr && x11[0] != '\0');
}

// Called by PAM when a user needs to be authenticated
PAM_EXTERN int pam_sm_authenticate(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)flags;
  (void)argc;
  (void)argv;

  // At the login screen there is no Wayland or X11 display yet.  Face auth
  // requires a display, and pam_fprintd.so (which runs before us in the
  // PAM stack) already handles fingerprint.  Skip entirely so the password
  // prompt appears immediately.
  if (!has_display()) {
    return PAM_IGNORE;
  }

  // Set up the SIGALRM handler and save the old one so we can restore it.
  struct sigaction sa, old_sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = alarm_handler;
  sigaction(SIGALRM, &sa, &old_sa);

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

  pid_t pid = fork();
  if (pid < 0) {
    return PAM_AUTH_ERR;
  } else if (pid == 0) {
    // Child process: no alarm handler needed, restore default and exec.
    signal(SIGALRM, SIG_DFL);

    // When no display is available the PAM module returns PAM_IGNORE early
    // so we never reach this fork.  Here we always have a display.
    const char* helper = "/usr/bin/biopass-helper";
    const char* auth_cmd = "auth";
    const char* user_flag = "--username";
    const char* service_flag = "--service";

    if (service != nullptr && service[0] != '\0') {
      execl(helper, "biopass-helper", auth_cmd, user_flag, pUsername,
            service_flag, service, NULL);
    } else {
      execl(helper, "biopass-helper", auth_cmd, user_flag, pUsername, NULL);
    }

    // If execl returns, it failed. Don't perror() here: this process's
    // stdio is inherited from the PAM caller (e.g. polkit-agent-helper-1),
    // which some callers (GNOME Shell's polkit agent) parse as a strict
    // line protocol -- any unexpected line on it derails the caller's
    // authentication state machine instead of a clean failure.
    _exit(1);
  } else {
    // Parent: arm the timeout, then wait for the child.
    g_child_pid = pid;
    alarm(kHelperTimeout);

    int status;
    pid_t waited = waitpid(pid, &status, 0);

    // Cancel the alarm (it already fired if we got here via SIGALRM, but
    // calling alarm(0) is harmless.)
    alarm(0);
    g_child_pid = 0;

    // Restore the old SIGALRM handler.
    sigaction(SIGALRM, &old_sa, nullptr);

    // If waitpid was interrupted by SIGALRM (waited == -1 && errno == EINTR),
    // the child was already killed by the handler above -- wait one more time
    // to reap it, then return failure.
    if (waited < 0) {
      int dummy;
      waitpid(pid, &dummy, 0);
      return PAM_AUTH_ERR;
    }

    if (WIFEXITED(status)) {
      int exit_code = WEXITSTATUS(status);
      if (exit_code == 0) {
        return PAM_SUCCESS;
      } else if (exit_code == 2) {
        return PAM_IGNORE;
      } else {
        return PAM_AUTH_ERR;
      }
    } else {
      // Child did not exit normally (e.g., killed by signal / timeout)
      return PAM_AUTH_ERR;
    }
  }
}

// The functions below are required by PAM, but not needed in this module
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

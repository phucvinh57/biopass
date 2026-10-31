#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Called by PAM when a user needs to be authenticated
PAM_EXTERN int pam_sm_authenticate(pam_handle_t* pamh, int flags, int argc, const char** argv) {
  (void)flags;
  (void)argc;
  (void)argv;

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
    // The helper runs with the PAM caller's privileges (often root), so it
    // gets a fixed environment instead of the caller's: LD_*, LIBCAMERA_IPA_*,
    // HOME and XDG_CONFIG_HOME would otherwise let the invoking user pick code
    // or config (e.g. libcamera IPA modules and proxy workers) it loads.
    char* const helper_env[] = {const_cast<char*>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"), nullptr};

    // Run "biopass-helper auth --username <username> [--service <name>]"
    if (service != nullptr && service[0] != '\0') {
      execle("/usr/bin/biopass-helper", "biopass-helper", "auth", "--username", pUsername,
             "--service", service, NULL, helper_env);
    } else {
      execle("/usr/bin/biopass-helper", "biopass-helper", "auth", "--username", pUsername, NULL,
             helper_env);
    }

    // If execle returns, it failed. Don't perror() here: this process's
    // stdio is inherited from the PAM caller (e.g. polkit-agent-helper-1),
    // which some callers (GNOME Shell's polkit agent) parse as a strict
    // line protocol -- any unexpected line on it derails the caller's
    // authentication state machine instead of a clean failure.
    exit(1);
  } else {
    int status;
    waitpid(pid, &status, 0);

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
      // Child did not exit normally (e.g., killed by signal)
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

# PAM Setup Guide

## Debian/Ubuntu

1. Verify the Biopass PAM profile exists:
    ```bash
    ls /usr/share/pam-configs/biopass
    ```
2. Enable the Biopass PAM profile.
    ```bash
    sudo pam-auth-update
    ```
3. Enable `Biopass` option. If `Fingerprint authentication` is selected, please disable it if you have enabled fingerprint auth in Biopass.
4. Test in a new terminal:
    ```bash
    sudo -k
    sudo true
    ```

## Fedora/RHEL

Fedora-based OS (Fedora, RHEL, CentOS, Rocky, Alma ...) use `authselect`, not `pam-auth-update`.

Follow your distro's `authselect` workflow and test in a new terminal before closing the current root session.

## Arch Linux

Arch Linux does not use `pam-auth-update` nor `authselect` by default. Configure PAM manually.

Keep a root terminal open while testing. Incorrect PAM configuration can lock you out.

1. Verify the PAM module is installed:
    ```bash
    ls /usr/lib/security/libbiopass_pam.so
    ```
2. Edit the PAM service you want to protect, for example:
    ```bash
    sudoedit /etc/pam.d/system-auth
    ```
3. Insert Biopass and fprintd before the existing `pam_unix.so` auth rule:
    ```pam
    auth sufficient pam_fprintd.so timeout=3 max-tries=1
    auth sufficient libbiopass_pam.so
    auth [success=1 default=ignore] pam_unix.so nullok
    auth requisite pam_deny.so
    ```

    `pam_fprintd.so` handles fingerprint at the login screen so the user
    can swipe right after clicking Login.  `libbiopass_pam.so` handles
    face and fingerprint inside an active desktop session (lock screen,
    sudo, PolKit dialogs).
4. Test in a new terminal before closing the root terminal:
    ```bash
    sudo -k
    sudo true
    ```

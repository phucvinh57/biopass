# PAM Setup Guide

Biopass is meant as `auth sufficient`, an alternative to the password, not a second factor: when no method can run (e.g. no camera, fprintd not reachable) it returns `PAM_IGNORE` and the stack falls through to the next rule.

## Debian/Ubuntu

1. Verify the Biopass PAM profile exists:
    ```bash
    ls /usr/share/pam-configs/biopass
    ```
2. Enable the Biopass PAM profile.
    ```bash
    sudo pam-auth-update
    ```
3. Select `Biopass`. If you use fingerprint in Biopass, deselect `Fingerprint authentication`.
4. Test in a new terminal:
    ```bash
    sudo -k
    sudo true
    ```

## Fedora/RHEL

Fedora-based distros (Fedora, RHEL, CentOS, Rocky, Alma, ...) use `authselect`, not `pam-auth-update`. Follow your distro's `authselect` workflow and test in a new terminal before closing the current root session.

## Arch Linux

Arch uses neither `pam-auth-update` nor `authselect` by default; configure PAM manually. Keep a root terminal open while testing: incorrect PAM configuration can lock you out.

1. Verify the PAM module is installed:
    ```bash
    ls /usr/lib/security/libbiopass_pam.so
    ```
2. Edit the PAM service you want to protect, for example:
    ```bash
    sudoedit /etc/pam.d/system-auth
    ```
3. Insert Biopass before the existing `pam_unix.so` auth rule:
    ```pam
    auth sufficient libbiopass_pam.so
    auth [success=1 default=ignore] pam_unix.so nullok
    auth requisite pam_deny.so
    ```
4. Test in a new terminal before closing the root terminal:
    ```bash
    sudo -k
    sudo true
    ```

## Module options

| Option | Effect |
| :--- | :--- |
| `quiet` | No status messages ("looking for your face", ...). |
| `authtok` | After a face or fingerprint match on a login screen, set `PAM_AUTHTOK` to the password saved in the app, so the keyring unlocks. See [Keyring.md](Keyring.md). The Debian/Ubuntu profile sets it. |

Before adding `authtok` to a manual setup:

- Biopass must be an alternative to the password rules, not run before them. If a rule such as `pam_unix.so try_first_pass` ran after a Biopass success, it would take the saved password as a typed one, so a face match alone would pass a stack meant to need both.
- The keyring module's `auth` line (`pam_gnome_keyring.so`, `pam_kwallet5.so`, ...) must still run after a Biopass success. The Debian/Ubuntu profile does this: `success=end` skips only the rest of `common-auth`, and the login screen's own keyring lines follow it. `sufficient` in an included file such as `system-auth` ends the whole stack at once, so the keyring lines after it never run.

## Login screen and keyring

If Biopass is enabled for your login screen (GDM, SDDM, LightDM, ...), the login keyring / KWallet stays locked because no password is typed, unless you save your password in the app. See [Keyring.md](Keyring.md).

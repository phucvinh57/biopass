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

## Login screen and keyring

If Biopass is enabled for your login screen (GDM, SDDM, LightDM, ...), the login keyring / KWallet is not unlocked because no password is typed. See [Keyring.md](Keyring.md) for the workaround.

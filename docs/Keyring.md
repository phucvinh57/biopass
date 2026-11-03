# Keyring / KWallet Unlock After Login

After logging in with your face or fingerprint, you may see *"The login keyring did not get unlocked"* (or a KWallet password prompt). The keyring is encrypted with your password, and a biometric login doesn't type it.

There are two fixes: save your password in Biopass, or skip Biopass at the login screen.

## Fix 1: save your password in Biopass

1. Open **Configuration** → **Keyring Unlock** in the Biopass app.
2. Type your login password and click **Save password**, then authenticate when asked.
3. Log out and log back in with your face or fingerprint. The keyring is unlocked.

**Remove** in the same card deletes the saved password.

### Requirements

- A TPM 2.0 (`/dev/tpmrm0`). Most computers since ~2016 have one; you may need to enable it in the firmware.
- A local account (not LDAP/SSSD).
- A password of at most 128 bytes.
- The `authtok` PAM option. The Debian/Ubuntu package sets it; for manual setups see [PAM.md](PAM.md#module-options).
- A supported login screen: `gdm-password`, `sddm`, `lightdm`, `login`, `greetd`, `lxdm`, `ly`. The GNOME lock screen and light-locker also work. `sudo`, `su`, polkit and lock screens that run as your user (KDE, swaylock, ...) never get the password.

### Save it again when

- you change your password;
- the TPM is cleared or replaced;
- with **Lock to Secure Boot settings** on (default): Secure Boot is toggled, or a firmware/OS update changes its keys.

Until then, the login screen shows a short message and the keyring asks for its password as before. Your login itself is never affected.

### How it works

- **Saving:** the app runs `biopass-keyring` through `pkexec`. It checks the password against `/etc/shadow`, seals it with the TPM, and stores it in `/var/lib/biopass/keyring/<uid>.sealed` (root-only). The app doesn't keep it. `<uid>.mode` beside it (readable, not secret) says whether the seal is locked to Secure Boot settings, so the app can show it.
- **Login:** after a biometric match, `biopass-helper` unseals the password, checks it again, and the PAM module sets `PAM_AUTHTOK`. `pam_gnome_keyring` / `pam_kwallet` then use it as if you had typed it.
- **Lock to Secure Boot settings** binds the seal to TPM PCR 7, which records the Secure Boot configuration.

### Security

- The sealed file is useless without this computer's TPM, so a copied disk doesn't reveal the password.
- Root on the running system can unseal it (root can also read what you type).
- PCR 7 binding stops booting a system with changed Secure Boot settings, but not a signed live USB on the same machine. Full-disk encryption covers that.

### Uninstall

`apt purge biopass` deletes all saved passwords. `apt remove` and RPM uninstall keep them; delete `/var/lib/biopass` by hand.

## Fix 2: skip Biopass at the login screen

Use this without a TPM or with a network account. You type your password at the login screen, and Biopass still works elsewhere (e.g. `sudo`).

1. Find your login screen's PAM service:

   | Login screen | Service |
   | :--- | :--- |
   | GDM (GNOME) | `gdm-password` |
   | SDDM (KDE) | `sddm` |
   | LightDM | `lightdm` |
   | greetd | `greetd` |
   | LXDM | `lxdm` |
   | Ly | `ly` |
   | Text console | `login` |

   Not listed? Check `ls /etc/pam.d/`. Use the exact service name, not `common-auth` / `system-auth`.

2. Add it in **Configuration** → **Strategy Settings** → **Ignored PAM Services** and click **Save**, e.g. `polkit-1, pkexec, gdm-password`. Or in `~/.config/com.ticklab.biopass/config.yaml`:

   ```yaml
   strategy:
     ignore_services:
       - polkit-1
       - pkexec
       - gdm-password
   ```

   This list replaces the default (`polkit-1`, `pkexec`), so keep those entries.

3. Log out and back in. The login screen asks for your password; `sudo true` still uses Biopass.

**Note:** on GNOME the lock screen also uses `gdm-password`, so this disables Biopass there too. The same goes for LightDM with light-locker. KDE's lock screen uses its own `kde` service and keeps working.

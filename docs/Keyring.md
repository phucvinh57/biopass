# Keyring / KWallet Unlock After Login

## Symptom

You sign in at the login screen (GDM, SDDM, LightDM, ...) with your face or fingerprint. After the desktop loads, you get a prompt like *"The login keyring did not get unlocked when you logged into your computer"*. KWallet asks for its password the same way. Apps that store secrets there (browsers, Wi-Fi, mail, VS Code, ...) keep asking for it.

## Why it happens

The login keyring (GNOME Keyring) and KWallet are encrypted with your **password**. At login, `pam_gnome_keyring.so` / `pam_kwallet5.so` / `pam_kwallet6.so` read the password you typed (the `PAM_AUTHTOK` item that `pam_unix.so` sets) and use it to unlock them. With a face or fingerprint login you never type it.

## Fix: save your password in Biopass

Biopass can keep a copy of your login password, sealed by your computer's TPM, and hand it to the keyring modules after a face or fingerprint match.

1. Open **Configuration** → **Keyring Unlock** in the Biopass app.
2. Type your login password and click **Save password**. Your desktop asks you to authenticate (a polkit prompt).
3. Log out and log back in with your face or fingerprint. The keyring should be unlocked with no extra prompt.

**Remove** in the same card deletes the saved copy. Uninstalling with `apt purge biopass` deletes all saved copies (`/var/lib/biopass`); `apt remove` and RPM uninstall keep them, delete that directory by hand.

### Requirements

- A TPM 2.0 (`/dev/tpmrm0`). Most computers since about 2016 have one; it may need enabling in the firmware settings.
- A local account: the password is checked against `/etc/shadow`. LDAP, SSSD and other network accounts are not supported.
- A password of at most 128 bytes (the TPM's limit for one sealed secret).
- The `authtok` option on the Biopass PAM line. The Debian/Ubuntu profile (`pam-auth-update`) sets it; for manual setups see [PAM.md](PAM.md#module-options).
- A login screen whose PAM service is one of: `gdm-password`, `sddm`, `lightdm`, `login`, `greetd`, `lxdm`, `ly`. `sudo`, `su` and polkit never receive the password. Lock screens that unlock through one of these services as root also receive it: the GNOME lock screen (`gdm-password`) and light-locker (`lightdm`). Lock screens that run PAM as your user (KDE's, swaylock, ...) can't read the sealed file.

### How it works

- **Saving.** The app runs `pkexec /usr/libexec/biopass/biopass-keyring seal` and writes the password to its stdin. polkit asks you to authenticate as yourself (action `com.ticklab.biopass.keyring`). `biopass-keyring` runs as root, saves only for the user who started pkexec, checks the password against `/etc/shadow`, seals it with the TPM, and stores the result in `/var/lib/biopass/keyring/<uid>.sealed` (directory `0711`, file `0600`, both root-owned). The app never stores the password.
- **Login.** After a face or fingerprint match, `biopass-helper` (running as root under the login screen) unseals the password (giving up if the TPM doesn't answer in time), checks it against `/etc/shadow` again, and passes it to the PAM module through a private pipe. The module sets `PAM_AUTHTOK`, and the keyring modules after it use it as if you had typed it.
- **Bind to Secure Boot state (PCR 7)**, on by default, makes the TPM release the password only while PCR 7, which records the Secure Boot configuration, has the value it had when you saved.

Biometrics alone decide whether you are logged in. If anything in the keyring step fails, you are still logged in, and the keyring just asks for its password as before.

### When you need to save it again

The login screen shows a short message and the keyring stays locked when:

- you changed your password (`passwd`, Settings, ...);
- PCR 7 changed, with **Bind to Secure Boot state** on: Secure Boot turned on or off, keys or the revocation list (dbx) updated by a firmware or OS update;
- the TPM was cleared or replaced.

Save the password again in the app to fix it.

### Security notes

- The sealed file is useless without this computer's TPM, so a copy of the disk alone does not reveal your password.
- Root on the running system can always unseal it, as root can also read what you type.
- PCR 7 binding stops a thief who changes the Secure Boot configuration to boot another system. It does not stop one who boots another Secure Boot–signed system (for example a live USB) on the same computer with the same configuration. With full-disk encryption, the disk must be unlocked first, which covers this.
- An attacker with physical access to the TPM bus who can also tamper with it is out of scope; the password crosses the bus encrypted.

## Alternative: skip Biopass at the login screen

If you can't or don't want to save your password (no TPM, network account), make Biopass step aside at the login screen. You type your password there as usual, so the keyring gets unlocked, and Biopass still works everywhere else, such as `sudo`.

### 1. Find your PAM service name

| Login screen | PAM service to ignore |
| :--- | :--- |
| GDM (GNOME) | `gdm-password` |
| SDDM (KDE) | `sddm` |
| LightDM | `lightdm` |
| greetd (tuigreet, ReGreet, ...) | `greetd` |
| LXDM | `lxdm` |
| Ly | `ly` |
| Text console (TTY) | `login` |

Notes:
- The name is the file name in `/etc/pam.d/` that the login screen uses. If yours is not listed, check `ls /etc/pam.d/`.
- Matching is exact and case-sensitive. Use the service name (e.g. `gdm-password`), not an included file such as `common-auth` or `system-auth`.
- GDM: only `gdm-password` matters. `gdm-autologin` asks for nothing, so there is nothing to unlock, and `gdm-fingerprint` does not load Biopass. If you use smart cards, add `gdm-smartcard` too.
- `login` is only needed if you log in on a TTY and start your desktop from there.

### 2. Add it to the ignore list

**From the app:** open **Configuration** → **Strategy Settings** → **Ignored PAM Services**. Append the service name, separated by commas, then click **Save**. For example, on GNOME:

```
polkit-1, pkexec, gdm-password
```

**From the config file:** edit `~/.config/com.ticklab.biopass/config.yaml` and add the service under `strategy.ignore_services`:

```yaml
strategy:
  ignore_services:
    - polkit-1
    - pkexec
    - gdm-password # or sddm, lightdm, greetd, ...
```

This list replaces the default (`polkit-1`, `pkexec`), so keep those entries if you still want them ignored. No restart is needed. The list is read on every authentication.

### 3. Test

Log out and log back in. The login screen should ask for your password, and the keyring should be unlocked with no extra prompt. `sudo true` in a terminal should still use Biopass.

### Trade-offs

- Face and fingerprint login are disabled at the login screen for that service. You type your password once per boot or login. After that, the keyring is already unlocked.
- **GNOME:** the lock screen also uses `gdm-password`, so ignoring it disables Biopass on the GNOME lock screen too. This is a GNOME limitation; there is no separate lock screen service to keep.
- **KDE:** the lock screen uses its own `kde` service, so ignoring `sddm` keeps Biopass on the lock screen.
- **LightDM with light-locker:** light-locker unlocks through the LightDM greeter, so ignoring `lightdm` also affects unlocking.
- Lock screens with their own service (`kde`, `cinnamon-screensaver`, `mate-screensaver`, `xfce4-screensaver`, `swaylock`, `hyprlock`, ...) can keep using Biopass. Unlocking the screen does not need the keyring, which is already unlocked from login.

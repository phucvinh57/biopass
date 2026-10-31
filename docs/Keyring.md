# Keyring / KWallet Not Unlocked After Login

## Symptom

You sign in at the login screen (GDM, SDDM, LightDM, ...) with your face or fingerprint. After the desktop loads, you get a prompt like *"The login keyring did not get unlocked when you logged into your computer"*. KWallet asks for its password the same way. Apps that store secrets there (browsers, Wi-Fi, mail, VS Code, ...) keep asking for it.

## Why it happens

The login keyring (GNOME Keyring) and KWallet are encrypted with your **password**. At login, `pam_gnome_keyring.so` / `pam_kwallet5.so` / `pam_kwallet6.so` read the password you typed (the `PAM_AUTHTOK` item that `pam_unix.so` sets) and use it to unlock them.

When Biopass signs you in, you never type a password. Biopass cannot set `PAM_AUTHTOK`, so the keyring stays locked. A proper fix is tracked in [#87](https://github.com/phucvinh57/biopass/issues/87) and [#46](https://github.com/phucvinh57/biopass/issues/46).

## Workaround: skip Biopass at the login screen

Add the login screen's PAM service to Biopass's ignored services. For that service, Biopass steps aside (`PAM_IGNORE`) and you type your password as usual, so the keyring gets unlocked. Biopass still works everywhere else, such as `sudo`.

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

## Trade-offs

- Face and fingerprint login are disabled at the login screen for that service. You type your password once per boot or login. After that, the keyring is already unlocked.
- **GNOME:** the lock screen also uses `gdm-password`, so ignoring it disables Biopass on the GNOME lock screen too. This is a GNOME limitation; there is no separate lock screen service to keep.
- **KDE:** the lock screen uses its own `kde` service, so ignoring `sddm` keeps Biopass on the lock screen.
- **LightDM with light-locker:** light-locker unlocks through the LightDM greeter, so ignoring `lightdm` also affects unlocking.
- Lock screens with their own service (`kde`, `cinnamon-screensaver`, `mate-screensaver`, `xfce4-screensaver`, `swaylock`, `hyprlock`, ...) can keep using Biopass. Unlocking the screen does not need the keyring, which is already unlocked from login.

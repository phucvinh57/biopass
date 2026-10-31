# Polkit Integration Guide

Desktop environments such as GNOME and KDE ask for your password or fingerprint in a dialog before changing protected settings.

![Interactive Auth](./images/interactive-auth.png)

This dialog is handled by [Polkit](https://github.com/polkit-org/polkit). Its strict device-access policy can stop Biopass from running there. To fix it:

1. Create a systemd override for `polkit-agent-helper`
    ```bash
    sudo systemctl edit 'polkit-agent-helper@.service'
    ```
2. Add this override, then save:
    ```ini
    [Service]
    # To allow camera use
    PrivateDevices=no
    DevicePolicy=auto
    DeviceAllow=char-video4linux rw
    DeviceAllow=char-media rw
    DeviceAllow=char-drm rw
    DeviceAllow=/dev/uinput rw

    # To write logs to `~/.local/share/com.ticklab.biopass/logs/`
    ProtectHome=no
    ```
3. Reload systemd and restart polkit
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl restart polkit.service
    ```
4. In Biopass's UI, remove `pkexec` and `polkit-1` from the ignored services list. ![Ignore services](images/ignore-services.png)
5. Run `pkexec id`. If the override works, the camera opens and the polkit password window does not appear.


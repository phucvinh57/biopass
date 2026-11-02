# IR Camera Guide

Biopass can use an infrared (IR) camera for face anti-spoofing, in addition to the RGB anti-spoofing model. It is configured in the app as a video device path such as `/dev/video2`.

## Requirements

- A Linux system where the IR sensor is exposed as a `/dev/video*` device.
- A working face setup in Biopass.
- Permission to access the camera device.

Biopass only reads from the configured IR video device. It does not manage the hardware IR emitter for your laptop or webcam.

## 1. Find the IR Camera Device

List video devices:

```bash
ls -l /dev/video*
```

`v4l2-ctl` makes it easier to identify:

```bash
v4l2-ctl --list-devices
```

Look for the device node that belongs to your IR sensor, for example `/dev/video2`.

Biopass captures through libcamera, so `cam -l` (from `libcamera-tools`) or the bundled `camera_capture_test --list-devices` / `--list-formats /dev/videoN` debug tool also work.

A device that libcamera does not list (for example a custom DKMS driver or `v4l2loopback`) is opened directly through V4L2 when you select it by its `/dev/videoN` path. `camera_capture_test --list-formats /dev/videoN` also works for it, but `--list-devices` does not show it.

## 2. Enable It In Biopass

In the app's face settings, anti-spoofing section:

1. Enable face anti-spoofing if you also want the AI model.
2. Set `IR Camera` to the correct `/dev/video*` device.
3. Save your configuration.

If you only want IR-based anti-spoofing, selecting the `IR Camera` device is enough.

To use the IR sensor as the main camera too (enrol and sign in on IR), set both `Camera Device` and `IR Camera` to the same IR node explicitly: an unset camera that defaults to the IR node is not treated as the IR camera, and a main camera that delivers colour frames fails the IR check. That colour check only catches picking an RGB webcam by mistake: an RGB camera filming a greyscale scene can pass it, so only use this setup with a real IR node. If a real IR node is rejected, the debug log shows its `chroma` value; please report it.

## 3. If The IR Emitter Stays Off On Linux

If the IR camera is detected but its emitter does not light up, use [`linux-enable-ir-emitter`](https://github.com/EmixamPP/linux-enable-ir-emitter):

```bash
VERSION=6.1.2
DIST=linux-enable-ir-emitter-$VERSION-release.systemd.x86-64.tar.gz
wget https://github.com/EmixamPP/linux-enable-ir-emitter/releases/download/$VERSION/$DIST
sudo tar -C / --no-same-owner -m -h -vxzf $DIST
```

Configure the emitter:

```bash
sudo linux-enable-ir-emitter configure
```

Once it works, enable it at boot:

```bash
sudo systemctl enable --now linux-enable-ir-emitter
```

## 4. If The IR Presence Check Fails Intermittently

Some IR emitters blink, so a single frame can be all-white or all-dark with no detectable face. Biopass retries the IR presence check (capture + detection) for up to `anti_spoofing.ir_presence_timeout_ms` (default `1500`) in `config.yaml`. If failures persist, increase it. Values above `10000` are capped to `10000`, and `anti_spoofing.ir_warmup_delay_ms` is capped to `5000`. A whole Biopass login (all methods and retries) also stops after 40 seconds, and the next PAM step (usually the password prompt) takes over.

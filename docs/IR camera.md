# IR Camera Guide

Biopass uses infrared (IR) camera for face anti-spoofing, rather than relying only on the RGB anti-spoofing AI model. This is usually configured as a Linux video device path such as `/dev/video2`. If your devices supports IR camera, you can turn on this option by using the configuration UI.

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

If `v4l2-ctl` is available, it is usually easier to identify the correct device with:

```bash
v4l2-ctl --list-devices
```

Look for the device node that belongs to your IR sensor, for example `/dev/video2`.

Biopass captures through libcamera; you can also identify devices with `cam -l` (from `libcamera-tools`) or the bundled `camera_capture_test --list-devices` / `--list-formats /dev/videoN` debug tool.

## 2. Enable It In Biopass

Open the Biopass desktop app and go to the face settings.

In the anti-spoofing section:

1. Enable face anti-spoofing if you want to use the AI anti-spoofing model too.
2. Set `IR Camera` to the correct `/dev/video*` device.
3. Save your configuration.

If you only want IR-based anti-spoofing, selecting the `IR Camera` device is enough.

## 3. Optional: Use The IR Sensor As The Main Camera

You can also select the IR device as the main **Camera Device** (the same
`/dev/video*` path you set for `IR Camera`), not just for anti-spoofing. Biopass
then enrols and recognises on the IR image, the way Windows Hello does. The main
advantage is that face login no longer depends on room lighting — it works in the
dark, since the scene is lit by the IR emitter.

To do this, in the face settings set **Camera Device** and **IR Camera** to the
same IR `/dev/video*` device, then save.

Notes:

- **Re-enrol after switching.** Enrolments are captured with, and tagged by, the
  sensor they came from. RGB and IR images of the same face do not match reliably,
  so after selecting the IR device as the main camera you should capture new faces;
  enrolments made with a different camera are ignored at login.
- The IR emitter must actually turn on — see the next section.

## 4. If The IR Emitter Stays Off On Linux

On some Linux systems, the IR camera is detected but the IR light emitter does not turn on automatically. In that case, use [`linux-enable-ir-emitter`](https://github.com/EmixamPP/linux-enable-ir-emitter).

To install it:

```bash
VERSION=6.1.2
DIST=linux-enable-ir-emitter-$VERSION-release.systemd.x86-64.tar.gz
wget https://github.com/EmixamPP/linux-enable-ir-emitter/releases/download/$VERSION/$DIST
sudo tar -C / --no-same-owner -m -h -vxzf $DIST
```

Then, configure your IR emitter: 

```bash
sudo linux-enable-ir-emitter configure
```

After successfully triggering your IR emitter, run:

```bash
sudo systemctl enable --now linux-enable-ir-emitter
```

## 5. If The IR Presence Check Fails Intermittently

Some IR emitters blink rapidly, so an individual captured frame can be over-exposed
(all-white) or under-exposed (all-dark) and yield no detectable face. Biopass retries
the IR presence check (capture + detection) for up to `anti_spoofing.ir_presence_timeout_ms`
milliseconds (default `1500`) in `config.yaml` before giving up, so a single bad frame
should no longer fail the check. If failures persist, try increasing this value.

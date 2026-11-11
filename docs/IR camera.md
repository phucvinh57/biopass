# IR Camera Guide

Biopass can use an infrared (IR) camera for face anti-spoofing, in addition to the RGB anti-spoofing model. It is configured in the app as a video device. Once you save your settings, the app stores a stable `/dev/v4l/by-id/…` or `/dev/v4l/by-path/…` link to it, since `/dev/videoN` numbers can change across boots and docking. A `/dev/videoN` path from an older config still works. The face settings list such paths with the device that now holds each number; check them, click `Use stable paths`, and save. They are never switched silently, since a number may belong to another device after docking.

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

### Logitech BRIO (`046d:085e`): separate RGB and IR nodes

Linux exposes separate RGB and IR nodes, but libcamera can represent both as one RGB camera. Its device/format listing can therefore show RGB formats even for the IR path. Check the nodes directly with V4L2 instead:

```bash
for device in /dev/v4l/by-id/*BRIO*; do
  echo "=== $device ==="
  v4l2-ctl --device="$device" --list-formats
done
```

Keep the RGB node as the main camera and select the separate node offering native `GREY` as `IR Camera` in the same camera profile. On the tested BRIO, `video-index0` offered `MJPG`/`YUYV` and `video-index2` offered `GREY`; verify the formats rather than assuming these indices on every device.

When both paths resolve to the same libcamera camera, Biopass captures RGB, releases that session, then opens the configured IR node directly through V4L2. This path requires native `GREY`, including after driver negotiation. If opening or format negotiation fails, face authentication fails; it never substitutes the RGB stream. The debug log reports `Using direct V4L2 for IR node` and the negotiated `GREY` format. A passed IR presence check confirms a face is visible, not liveness or identity.

The app lists capture nodes using udev capabilities, hides metadata nodes, and shows each node and its stable path so RGB and IR can be distinguished. If udev capability data is unavailable, it retains the previous index-0 filter; a secondary IR node may then be missing. In that case, set its stable path in the relevant `camera_profiles[].ir_camera` field in `config.yaml` with the app closed.

### Camera profiles (docked / undocked)

If your cameras change (a dock camera, a closed lid), add `Camera Profiles` under the face settings: ordered camera + IR camera pairs. The first profile you add starts from your current camera and IR camera, when a camera is set. While profiles exist, they override `Camera Device`, and the anti-spoofing IR picker becomes the `Fallback IR Camera`.

At each authentication Biopass uses the first profile whose camera is connected. A profile with `Auto-select` as its camera always matches.

- The profile's own IR camera is used if it has one.
- Otherwise the **fallback IR camera** is used: `Fallback IR Camera` if set, else the first IR camera set in any profile.
- If no profile's camera is connected, Biopass auto-selects the first camera, with the fallback IR camera.
- If the IR camera is missing, the IR check fails and face authentication fails (PAM falls through to the next method). Connecting a device never turns the IR check off by itself.

Profiles below an `Auto-select` profile are never used.

`Allow without IR` (only for a profile without its own IR camera) runs that profile with no IR check. On an `Auto-select` profile that means no IR check whenever that profile is reached. Use it with care: a profile matches whenever a device appears at its camera path, so anyone with physical access who plugs a USB camera or gadget into that port (or a device with the same ID) skips the IR check.

With no profiles, the `Camera Device` and `IR Camera` settings are used, as before.

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

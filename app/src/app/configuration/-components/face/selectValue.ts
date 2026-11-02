import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";

/** Select value for "off": auto-select camera, or a disabled feature. */
export const DISABLED_OPTION = "__disabled__";

/**
 * The value a config-backed <Select> shows. `selected` is null when the
 * setting is off; a selected value missing from `options` shows as
 * `unavailableOption` so the user sees it is gone instead of a blank.
 */
export function selectValueFor(
  selected: string | null,
  options: readonly string[],
  unavailableOption: string,
): string {
  if (selected === null) return DISABLED_OPTION;
  return options.includes(selected) ? selected : unavailableOption;
}

/**
 * The listed device path for a saved camera value. A config saved before
 * stable IDs holds /dev/videoN (or another /dev/v4l link); it matches the
 * device that lists it in `aliases`. Unmatched values come back unchanged.
 */
export function devicePathFor(
  selected: string | null,
  devices: readonly VideoDeviceInfo[],
): string | null {
  if (selected === null) return null;
  const device = devices.find(
    (d) => d.path === selected || d.aliases.includes(selected),
  );
  return device ? device.path : selected;
}

type FaceConfig = BiopassConfig["methods"]["face"];
type CameraProfile = FaceConfig["camera_profiles"][number];

/**
 * `config` with the pre-profile camera settings moved into profiles, as the
 * camera UI edits profiles only. Auth picks the same cameras before and
 * after (pickCameraProfile, auth/core/camera_profile.h):
 * - no profiles: one profile from `camera` + `anti_spoofing.ir_camera`;
 * - else `camera` was unused, and each profile without an IR camera (and
 *   not allowed without one) gets the fallback IR it used:
 *   `anti_spoofing.ir_camera`, else the first profile IR.
 * Both legacy fields are then cleared. A profile still without an IR camera
 * and not allowed without one is left for the user to choose (see
 * hasIrChoice).
 */
export function migrateCameraProfiles(config: BiopassConfig): BiopassConfig {
  const face = config.methods.face;
  const camera = face.camera || null;
  const legacyIr = face.anti_spoofing.ir_camera || null;
  let profiles = face.camera_profiles;
  if (profiles.length === 0) {
    if (camera || legacyIr) {
      profiles = [{ camera, ir_camera: legacyIr, allow_without_ir: false }];
    }
  } else {
    const fallbackIr =
      legacyIr ?? profiles.find((p) => p.ir_camera)?.ir_camera ?? null;
    if (fallbackIr) {
      profiles = profiles.map((p) =>
        hasIrChoice(p) ? p : { ...p, ir_camera: fallbackIr },
      );
    }
  }
  return {
    ...config,
    methods: {
      ...config.methods,
      face: {
        ...face,
        camera: null,
        camera_profiles: profiles,
        anti_spoofing: { ...face.anti_spoofing, ir_camera: null },
      },
    },
  };
}

function isListed(path: string, devices: readonly VideoDeviceInfo[]) {
  return devices.some((d) => d.path === path || d.aliases.includes(path));
}

/**
 * Whether the user chose the profile's IR camera: a device, or none
 * (allow_without_ir). Without a choice auth falls back to another
 * profile's IR camera, which the UI doesn't offer.
 */
export function hasIrChoice(profile: CameraProfile): boolean {
  return Boolean(profile.ir_camera) || profile.allow_without_ir;
}

/**
 * The camera + IR camera auth would use. Mirrors pickCameraProfile
 * (auth/core/camera_profile.h) with the legacy fields cleared (see
 * migrateCameraProfiles), and the device list in place of stat():
 * - the first profile whose camera is listed (no camera = auto-select),
 *   with its IR camera, else the fallback IR unless allow_without_ir;
 * - none listed, or no profiles: auto-select with the fallback IR.
 */
export function activeCameraProfile(
  profiles: readonly CameraProfile[],
  devices: readonly VideoDeviceInfo[],
): { camera: string | null; ir_camera: string | null } {
  const fallbackIr = profiles.find((p) => p.ir_camera)?.ir_camera ?? null;
  for (const profile of profiles) {
    if (profile.camera && !isListed(profile.camera, devices)) continue;
    return {
      camera: profile.camera || null,
      ir_camera:
        profile.ir_camera || (profile.allow_without_ir ? null : fallbackIr),
    };
  }
  return { camera: null, ir_camera: fallbackIr };
}

const RAW_NODE = /^\/dev\/video\d+$/;

/**
 * `config` with each profile camera saved as a raw /dev/videoN (from before
 * stable IDs) pinned to the stable path of the device at that number now:
 * the device the pickers show, and the one auth opens today.
 */
export function withStablePaths(
  config: BiopassConfig,
  devices: readonly VideoDeviceInfo[],
): BiopassConfig {
  const stable = (saved: string | null) => {
    if (!saved || !RAW_NODE.test(saved)) return saved;
    return devices.find((d) => d.node === saved)?.path ?? saved;
  };
  const face = config.methods.face;
  return {
    ...config,
    methods: {
      ...config.methods,
      face: {
        ...face,
        camera_profiles: face.camera_profiles.map((p) => ({
          ...p,
          camera: stable(p.camera),
          ir_camera: stable(p.ir_camera),
        })),
      },
    },
  };
}

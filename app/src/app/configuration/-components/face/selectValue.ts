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

function isListed(path: string, devices: readonly VideoDeviceInfo[]) {
  return devices.some((d) => d.path === path || d.aliases.includes(path));
}

/**
 * The camera + IR camera auth would use. Mirrors pickCameraProfile
 * (auth/core/camera_profile.h), with the device list in place of stat():
 * - no profiles: `camera` + `anti_spoofing.ir_camera`;
 * - else the first profile whose camera is listed (no camera = auto-select),
 *   with its IR camera, else the fallback IR unless allow_without_ir;
 * - none listed: auto-select with the fallback IR.
 * The fallback IR is `anti_spoofing.ir_camera`, else the first profile IR.
 */
export function activeCameraProfile(
  face: Pick<FaceConfig, "camera" | "camera_profiles"> & {
    anti_spoofing: Pick<FaceConfig["anti_spoofing"], "ir_camera">;
  },
  devices: readonly VideoDeviceInfo[],
): { camera: string | null; ir_camera: string | null } {
  const legacyIr = face.anti_spoofing.ir_camera || null;
  if (face.camera_profiles.length === 0) {
    return { camera: face.camera ?? null, ir_camera: legacyIr };
  }
  const fallbackIr =
    legacyIr ??
    face.camera_profiles.find((p) => p.ir_camera)?.ir_camera ??
    null;
  for (const profile of face.camera_profiles) {
    if (profile.camera && !isListed(profile.camera, devices)) continue;
    return {
      camera: profile.camera || null,
      ir_camera:
        profile.ir_camera || (profile.allow_without_ir ? null : fallbackIr),
    };
  }
  return { camera: null, ir_camera: fallbackIr };
}

type CameraField =
  | "methods.face.camera"
  | "methods.face.anti_spoofing.ir_camera"
  | `methods.face.camera_profiles.${number}.camera`
  | `methods.face.camera_profiles.${number}.ir_camera`;

export interface StablePathUpdate {
  name: CameraField;
  /** The setting, e.g. "Profile 2 IR camera". */
  label: string;
  /** The saved /dev/videoN. */
  from: string;
  /** The stable path of the device that holds that number now. */
  value: string;
  deviceName: string;
}

const RAW_NODE = /^\/dev\/video\d+$/;

/**
 * Saved camera values that are a raw /dev/videoN (from before stable IDs)
 * of a listed device with a stable path. The number may now belong to
 * another device (e.g. after docking), so the UI only offers these for the
 * user to confirm; nothing is rewritten silently.
 */
export function stablePathUpdates(
  face: Pick<FaceConfig, "camera" | "camera_profiles"> & {
    anti_spoofing: Pick<FaceConfig["anti_spoofing"], "ir_camera">;
  },
  devices: readonly VideoDeviceInfo[],
): StablePathUpdate[] {
  const hasProfiles = face.camera_profiles.length > 0;
  const fields: [CameraField, string, string | null | undefined][] = [
    ["methods.face.camera", "Camera", face.camera],
    [
      "methods.face.anti_spoofing.ir_camera",
      hasProfiles ? "Fallback IR camera" : "IR camera",
      face.anti_spoofing.ir_camera,
    ],
    ...face.camera_profiles.flatMap(
      (p, i): [CameraField, string, string | null | undefined][] => [
        [
          `methods.face.camera_profiles.${i}.camera`,
          `Profile ${i + 1} camera`,
          p.camera,
        ],
        [
          `methods.face.camera_profiles.${i}.ir_camera`,
          `Profile ${i + 1} IR camera`,
          p.ir_camera,
        ],
      ],
    ),
  ];
  return fields.flatMap(([name, label, saved]) => {
    if (!saved || !RAW_NODE.test(saved)) return [];
    const device = devices.find((d) => d.node === saved);
    if (!device || device.path === saved) return [];
    return [
      {
        name,
        label,
        from: saved,
        value: device.path,
        deviceName: device.name || device.node,
      },
    ];
  });
}

import { describe, expect, test } from "bun:test";
import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";
import {
  activeCameraProfile,
  DISABLED_OPTION,
  devicePathFor,
  hasIrChoice,
  migrateCameraProfiles,
  selectValueFor,
  withStablePaths,
} from "./selectValue";

const UNAVAILABLE = "__unavailable__";
const options = ["/dev/video0", "/dev/video2"];

describe("selectValueFor", () => {
  test("null (setting off) selects the disabled option", () => {
    expect(selectValueFor(null, options, UNAVAILABLE)).toBe(DISABLED_OPTION);
    expect(selectValueFor(null, [], UNAVAILABLE)).toBe(DISABLED_OPTION);
  });

  test("a value among the options selects itself", () => {
    expect(selectValueFor("/dev/video2", options, UNAVAILABLE)).toBe(
      "/dev/video2",
    );
  });

  test("a value missing from the options shows as unavailable", () => {
    expect(selectValueFor("/dev/video9", options, UNAVAILABLE)).toBe(
      UNAVAILABLE,
    );
    expect(selectValueFor("", options, UNAVAILABLE)).toBe(UNAVAILABLE);
    expect(selectValueFor(DISABLED_OPTION, options, UNAVAILABLE)).toBe(
      UNAVAILABLE,
    );
  });
});

const LID = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:8:1.0-video-index0";
const DOCK = "/dev/v4l/by-id/usb-Dock_Cam-video-index0";

function device(path: string, node: string): VideoDeviceInfo {
  return {
    path,
    node,
    aliases: [node, path],
    name: "",
    label: "Camera",
    details: "",
  };
}

const lidOnly = [device(LID, "/dev/video0")];
const docked = [device(LID, "/dev/video2"), device(DOCK, "/dev/video0")];

describe("devicePathFor", () => {
  test("a saved /dev/videoN maps to the device's stable path", () => {
    expect(devicePathFor("/dev/video0", lidOnly)).toBe(LID);
    expect(devicePathFor(LID, lidOnly)).toBe(LID);
  });

  test("null and unlisted values come back unchanged", () => {
    expect(devicePathFor(null, lidOnly)).toBeNull();
    expect(devicePathFor("/dev/video9", lidOnly)).toBe("/dev/video9");
    expect(devicePathFor(DOCK, lidOnly)).toBe(DOCK);
  });
});

const LID_IR = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:8:1.2-video-index0";
const LEGACY_IR = "/dev/video4";

function profile(
  camera: string | null,
  ir_camera: string | null,
  allow_without_ir = false,
) {
  return { camera, ir_camera, allow_without_ir };
}

/** A config with only the camera fields set; the rest is not read. */
function config(
  camera_profiles: ReturnType<typeof profile>[],
  ir_camera: string | null = null,
  camera: string | null = null,
) {
  return {
    methods: {
      face: { camera, camera_profiles, anti_spoofing: { ir_camera } },
    },
  } as unknown as BiopassConfig;
}

function migrated(...args: Parameters<typeof config>) {
  const { face } = migrateCameraProfiles(config(...args)).methods;
  expect(face.camera).toBeNull();
  expect(face.anti_spoofing.ir_camera).toBeNull();
  return face.camera_profiles;
}

describe("migrateCameraProfiles", () => {
  test("nothing set stays without profiles", () => {
    expect(migrated([])).toEqual([]);
  });

  test("legacy camera and IR camera become the first profile", () => {
    expect(migrated([], LEGACY_IR, "/dev/video0")).toEqual([
      profile("/dev/video0", LEGACY_IR),
    ]);
    expect(migrated([], null, "/dev/video0")).toEqual([
      profile("/dev/video0", null),
    ]);
    expect(migrated([], LEGACY_IR)).toEqual([profile(null, LEGACY_IR)]);
  });

  test("with profiles, the legacy IR fills profiles that used the fallback", () => {
    const profiles = [
      profile(DOCK, null),
      profile(DOCK, null, true),
      profile(LID, LID_IR),
    ];
    expect(migrated(profiles, LEGACY_IR, "/dev/video9")).toEqual([
      profile(DOCK, LEGACY_IR),
      profile(DOCK, null, true),
      profile(LID, LID_IR),
    ]);
    // Without a legacy IR, the fallback is the first profile IR.
    expect(migrated(profiles, null, "/dev/video9")).toEqual([
      profile(DOCK, LID_IR),
      profile(DOCK, null, true),
      profile(LID, LID_IR),
    ]);
  });

  test("with no IR camera anywhere, the IR choice is left to the user", () => {
    const profiles = [profile(DOCK, null), profile(LID, null)];
    const after = migrated(profiles);
    expect(after).toEqual(profiles);
    expect(after.some(hasIrChoice)).toBe(false);
  });

  test("auth picks the same cameras before and after", () => {
    const before = [profile(DOCK, null), profile(LID, null)];
    const after = migrated(before, LEGACY_IR);
    for (const devices of [lidOnly, docked, []]) {
      expect(activeCameraProfile(after, devices).ir_camera).toBe(LEGACY_IR);
    }
    const withIr = [profile(DOCK, null), profile(LID, LID_IR)];
    for (const devices of [lidOnly, docked, []]) {
      expect(activeCameraProfile(migrated(withIr), devices)).toEqual(
        activeCameraProfile(withIr, devices),
      );
    }
  });
});

describe("activeCameraProfile", () => {
  // Dock camera without its own IR first, then the lid camera with its IR.
  const dockThenLid = [profile(DOCK, null), profile(LID, LID_IR)];

  test("no profiles: auto-select without IR", () => {
    expect(activeCameraProfile([], lidOnly)).toEqual({
      camera: null,
      ir_camera: null,
    });
  });

  test("the first profile whose camera is listed, by path or alias", () => {
    expect(activeCameraProfile(dockThenLid, lidOnly)).toEqual({
      camera: LID,
      ir_camera: LID_IR,
    });
    const byAlias = [profile("/dev/video0", LID_IR)];
    expect(activeCameraProfile(byAlias, lidOnly).camera).toBe("/dev/video0");
  });

  test("a profile without IR inherits the first profile IR", () => {
    expect(activeCameraProfile(dockThenLid, docked)).toEqual({
      camera: DOCK,
      ir_camera: LID_IR,
    });
  });

  test("allow_without_ir drops IR for that profile only", () => {
    const profiles = [profile(DOCK, null, true), profile(LID, LID_IR)];
    expect(activeCameraProfile(profiles, docked)).toEqual({
      camera: DOCK,
      ir_camera: null,
    });
    expect(activeCameraProfile(profiles, [])).toEqual({
      camera: null,
      ir_camera: LID_IR,
    });
  });

  test("none listed, or a profile without a camera: auto-select with the fallback IR", () => {
    expect(activeCameraProfile(dockThenLid, [])).toEqual({
      camera: null,
      ir_camera: LID_IR,
    });
    const catchAll = [profile(null, null), ...dockThenLid];
    expect(activeCameraProfile(catchAll, docked)).toEqual({
      camera: null,
      ir_camera: LID_IR,
    });
  });
});

describe("withStablePaths", () => {
  const pinned =
    (...args: Parameters<typeof config>) =>
    (devices: VideoDeviceInfo[]) =>
      withStablePaths(config(...args), devices).methods.face.camera_profiles;

  test("raw /dev/videoN values become the device now at that number", () => {
    expect(pinned([profile("/dev/video0", "/dev/video0")])(lidOnly)).toEqual([
      profile(LID, LID),
    ]);
    expect(pinned([profile("/dev/video0", "/dev/video2")])(docked)).toEqual([
      profile(DOCK, LID),
    ]);
  });

  test("stable paths, other links, unlisted and unset values are kept", () => {
    const otherLink =
      "/dev/v4l/by-path/pci-0000:00:14.0-usbv2-0:8:1.0-video-index0";
    for (const p of [
      profile(LID, null),
      profile(otherLink, null),
      profile("/dev/video9", null, true),
    ]) {
      expect(pinned([p])(lidOnly)).toEqual([p]);
    }
    expect(pinned([profile("/dev/video0", null)])([])).toEqual([
      profile("/dev/video0", null),
    ]);
  });

  test("a node without a stable path is kept", () => {
    const bare = [device("/dev/video0", "/dev/video0")];
    expect(pinned([profile("/dev/video0", null)])(bare)).toEqual([
      profile("/dev/video0", null),
    ]);
  });
});

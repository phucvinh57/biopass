import { describe, expect, test } from "bun:test";
import type { VideoDeviceInfo } from "@/types/config";
import {
  activeCameraProfile,
  DISABLED_OPTION,
  devicePathFor,
  selectValueFor,
  stablePathUpdates,
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
    display_name: node,
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

function face(
  camera_profiles: ReturnType<typeof profile>[],
  ir_camera: string | null = null,
  camera: string | null = "/dev/video9",
) {
  return { camera, camera_profiles, anti_spoofing: { ir_camera } };
}

describe("activeCameraProfile", () => {
  // Dock camera without its own IR first, then the lid camera with its IR.
  const dockThenLid = [profile(DOCK, null), profile(LID, LID_IR)];

  test("no profiles: the legacy camera and IR camera", () => {
    expect(activeCameraProfile(face([], LEGACY_IR, "/dev/video0"), [])).toEqual(
      { camera: "/dev/video0", ir_camera: LEGACY_IR },
    );
    expect(activeCameraProfile(face([], null, null), [])).toEqual({
      camera: null,
      ir_camera: null,
    });
  });

  test("the first profile whose camera is listed, by path or alias", () => {
    expect(activeCameraProfile(face(dockThenLid), lidOnly)).toEqual({
      camera: LID,
      ir_camera: LID_IR,
    });
    const byAlias = face([profile("/dev/video0", LID_IR)]);
    expect(activeCameraProfile(byAlias, lidOnly).camera).toBe("/dev/video0");
  });

  test("a profile without IR inherits the fallback IR", () => {
    expect(activeCameraProfile(face(dockThenLid), docked).ir_camera).toBe(
      LID_IR,
    );
    expect(
      activeCameraProfile(face(dockThenLid, LEGACY_IR), docked).ir_camera,
    ).toBe(LEGACY_IR);
  });

  test("allow_without_ir drops IR for that profile only", () => {
    const profiles = [profile(DOCK, null, true), profile(LID, LID_IR)];
    expect(activeCameraProfile(face(profiles, LEGACY_IR), docked)).toEqual({
      camera: DOCK,
      ir_camera: null,
    });
    expect(activeCameraProfile(face(profiles, LEGACY_IR), [])).toEqual({
      camera: null,
      ir_camera: LEGACY_IR,
    });
  });

  test("none listed, or a profile without a camera: auto-select with the fallback IR", () => {
    expect(activeCameraProfile(face(dockThenLid), [])).toEqual({
      camera: null,
      ir_camera: LID_IR,
    });
    const catchAll = [profile(null, null), ...dockThenLid];
    expect(activeCameraProfile(face(catchAll, LEGACY_IR), docked)).toEqual({
      camera: null,
      ir_camera: LEGACY_IR,
    });
  });
});

describe("stablePathUpdates", () => {
  test("raw /dev/videoN values map to the device now at that number", () => {
    const saved = face(
      [profile("/dev/video0", "/dev/video0")],
      "/dev/video0",
      "/dev/video0",
    );
    const lid = { from: "/dev/video0", value: LID, deviceName: "/dev/video0" };
    expect(stablePathUpdates(saved, lidOnly)).toEqual([
      { name: "methods.face.camera", label: "Camera", ...lid },
      {
        name: "methods.face.anti_spoofing.ir_camera",
        label: "Fallback IR camera",
        ...lid,
      },
      {
        name: "methods.face.camera_profiles.0.camera",
        label: "Profile 1 camera",
        ...lid,
      },
      {
        name: "methods.face.camera_profiles.0.ir_camera",
        label: "Profile 1 IR camera",
        ...lid,
      },
    ]);
  });

  test("the device name is shown when known; IR label without profiles", () => {
    const named = [{ ...device(LID, "/dev/video0"), name: "Integrated C" }];
    expect(stablePathUpdates(face([], "/dev/video0", null), named)).toEqual([
      {
        name: "methods.face.anti_spoofing.ir_camera",
        label: "IR camera",
        from: "/dev/video0",
        value: LID,
        deviceName: "Integrated C",
      },
    ]);
  });

  test("stable paths, other links, unlisted and unset values are left alone", () => {
    expect(
      stablePathUpdates(face([profile(LID, null)], null, null), lidOnly),
    ).toEqual([]);
    const otherLink =
      "/dev/v4l/by-path/pci-0000:00:14.0-usbv2-0:8:1.0-video-index0";
    expect(
      stablePathUpdates(face([], null, otherLink), [
        {
          ...device(LID, "/dev/video0"),
          aliases: ["/dev/video0", LID, otherLink],
        },
      ]),
    ).toEqual([]);
    expect(stablePathUpdates(face([], null, "/dev/video9"), lidOnly)).toEqual(
      [],
    );
    expect(stablePathUpdates(face([], null, "/dev/video0"), [])).toEqual([]);
  });

  test("a node without a stable path is left alone", () => {
    const bare = [device("/dev/video0", "/dev/video0")];
    expect(stablePathUpdates(face([], null, "/dev/video0"), bare)).toEqual([]);
  });
});

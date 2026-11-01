import { describe, expect, test } from "bun:test";
import type { BiopassConfig } from "@/types/config";
import { referencedModelIds } from "./models";

// The app's default config (auth/test/config/defaults.yaml).
function makeConfig(
  face: { enable?: boolean; antiSpoofing?: boolean } = {},
  ids: { detection?: string; recognition?: string; antiSpoofing?: string } = {},
): BiopassConfig {
  return {
    schema_version: 2,
    strategy: {
      debug: false,
      execution_mode: "parallel",
      order: ["face", "fingerprint"],
      ignore_services: ["polkit-1", "pkexec"],
    },
    methods: {
      face: {
        enable: face.enable ?? true,
        retries: 5,
        retry_delay: 200,
        camera: null,
        detection: {
          model_id: ids.detection ?? "yolov8n-face",
          threshold: 0.5,
        },
        recognition: {
          model_id: ids.recognition ?? "edgeface-s-gamma-05",
          threshold: 0.5,
        },
        anti_spoofing: {
          enable: face.antiSpoofing ?? false,
          model: {
            model_id: ids.antiSpoofing ?? "mobilenetv3-antispoof",
            threshold: 0.8,
          },
          ir_camera: null,
          ir_warmup_delay_ms: 300,
          ir_presence_timeout_ms: 1500,
        },
      },
      fingerprint: { enable: false, retries: 1, timeout: 5000 },
    },
    appearance: "system",
  };
}

const all = ["yolov8n-face", "edgeface-s-gamma-05", "mobilenetv3-antispoof"];
const used = { includeDisabled: false };
const named = { includeDisabled: true };

describe("referencedModelIds", () => {
  test("default config: anti-spoofing is off, so only detection and recognition load", () => {
    expect(referencedModelIds(makeConfig(), used)).toEqual(all.slice(0, 2));
    expect(referencedModelIds(makeConfig(), named)).toEqual(all);
  });

  test("anti-spoofing on loads its model too", () => {
    const config = makeConfig({ antiSpoofing: true });
    expect(referencedModelIds(config, used)).toEqual(all);
  });

  test("face off loads nothing, but every id still counts as named", () => {
    for (const antiSpoofing of [false, true]) {
      const config = makeConfig({ enable: false, antiSpoofing });
      expect(referencedModelIds(config, used)).toEqual([]);
      expect(referencedModelIds(config, named)).toEqual(all);
    }
  });

  test("empty ids are dropped and repeats kept once, in config order", () => {
    const config = makeConfig(
      { antiSpoofing: true },
      { detection: "", recognition: "m", antiSpoofing: "m" },
    );
    expect(referencedModelIds(config, used)).toEqual(["m"]);
    expect(referencedModelIds(config, named)).toEqual(["m"]);
  });
});

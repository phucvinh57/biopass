// Types from the Rust side, generated into @/bindings (see
// app/src-tauri/src/bindings.rs). The `_Serialize` shapes are what commands
// return: every field present.
import type {
  BiopassConfig_Serialize,
  DownloadProgress_Serialize,
  Model_Serialize,
} from "@/bindings";

export type {
  ExecutionMode,
  ModelTask,
  VideoDeviceInfo,
} from "@/bindings";

export type BiopassConfig = BiopassConfig_Serialize;
export type Model = Model_Serialize;
export type ModelDownloadProgress = DownloadProgress_Serialize;

// A `strategy.order` entry the helper knows (MethodKind in
// auth/core/auth_config.h). `order` stays string[]: the app keeps whatever
// config.yaml holds, and the helper drops unknown names.
export type MethodKind = "face" | "fingerprint";

// Anti-spoofing only; auth picks preprocessing and output decoding from it.
// The app stores it as a string (Model.architecture).
export type AntiSpoofArchitecture = "mobilenetv3" | "minifasv2";

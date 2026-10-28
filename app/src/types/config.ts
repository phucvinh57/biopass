export interface BiopassConfig {
  schema_version: number;
  strategy: StrategyConfig;
  methods: MethodsConfig;
  appearance: string;
}

export interface StrategyConfig {
  debug: boolean;
  execution_mode: "sequential" | "parallel";
  order: string[];
  ignore_services: string[];
}

export interface MethodsConfig {
  face: FaceMethodConfig;
  fingerprint: FingerprintMethodConfig;
}

export interface VideoDeviceInfo {
  path: string;
  name: string;
  display_name: string;
}

export interface FaceMethodConfig {
  enable: boolean;
  retries: number;
  retry_delay: number;
  camera: string | null;
  detection: {
    model_id: string;
    threshold: number;
  };
  recognition: {
    model_id: string;
    threshold: number;
  };
  anti_spoofing: {
    enable: boolean;
    model: {
      model_id: string;
      threshold: number;
    };
    ir_camera: string | null;
    ir_warmup_delay_ms: number;
    ir_presence_timeout_ms: number;
  };
}

export interface FingerprintMethodConfig {
  enable: boolean;
  retries: number;
  timeout: number;
}

export type ModelTask = "detection" | "recognition" | "anti_spoofing";

// Network architecture of an anti-spoofing model. The auth side picks
// preprocessing and output decoding from it. Other tasks have none.
export type AntiSpoofArchitecture = "mobilenetv3" | "minifasv2";

// A row from the SQLite `models` table (see app/src-tauri/src/db.rs), listed
// via the list_models command rather than embedded in config.yaml.
export interface Model {
  id: string;
  name: string;
  task: ModelTask;
  architecture: AntiSpoofArchitecture | null;
  path: string;
  version: string | null;
  checksum: string | null;
  source: string;
  installed_at: number;
}

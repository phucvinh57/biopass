use serde::{Deserialize, Serialize};
use std::fs;
use std::io::Write;
use tauri::AppHandle;

use crate::paths::{get_config_dir, get_config_path};

/// Bump on incompatible config.yaml changes. Keep in sync with
/// CURRENT_SCHEMA_VERSION in auth/core/auth_config.h.
pub const CURRENT_SCHEMA_VERSION: u32 = 2;

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct BiopassConfig {
    pub schema_version: u32,
    pub strategy: StrategyConfig,
    pub methods: MethodsConfig,
    pub appearance: String,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct StrategyConfig {
    pub debug: bool,
    pub execution_mode: String,
    pub order: Vec<String>,
    pub ignore_services: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct MethodsConfig {
    pub face: FaceMethodConfig,
    pub fingerprint: FingerprintMethodConfig,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct FaceMethodConfig {
    pub enable: bool,
    pub retries: u32,
    pub retry_delay: u32,
    pub camera: Option<String>,
    pub detection: DetectionConfig,
    pub recognition: RecognitionConfig,
    pub anti_spoofing: AntiSpoofingConfig,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct DetectionConfig {
    pub model_id: String,
    pub threshold: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct RecognitionConfig {
    pub model_id: String,
    pub threshold: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct AntiSpoofingModelConfig {
    pub model_id: String,
    pub threshold: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct AntiSpoofingConfig {
    pub enable: bool,
    pub model: AntiSpoofingModelConfig,
    pub ir_camera: Option<String>,
    pub ir_warmup_delay_ms: i32,
    pub ir_presence_timeout_ms: i32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct FingerprintMethodConfig {
    pub enable: bool,
    pub retries: u32,
    pub timeout: u32,
}

// Mirrors the defaults in auth/core/auth_config.h's AntiSpoofingConfig.
const DEFAULT_IR_WARMUP_DELAY_MS: i32 = 300;
const DEFAULT_IR_PRESENCE_TIMEOUT_MS: i32 = 1500;

fn default_ignored_services() -> Vec<String> {
    vec!["polkit-1".to_string(), "pkexec".to_string()]
}

fn get_default_config() -> BiopassConfig {
    BiopassConfig {
        schema_version: CURRENT_SCHEMA_VERSION,
        strategy: StrategyConfig {
            debug: false,
            execution_mode: "parallel".to_string(),
            order: vec!["face".to_string(), "fingerprint".to_string()],
            ignore_services: default_ignored_services(),
        },
        methods: MethodsConfig {
            face: FaceMethodConfig {
                enable: true,
                retries: 5,
                retry_delay: 200,
                camera: None,
                detection: DetectionConfig {
                    model_id: "yolov8n-face".to_string(),
                    threshold: 0.5,
                },
                recognition: RecognitionConfig {
                    model_id: "edgeface-s-gamma-05".to_string(),
                    threshold: 0.5,
                },
                anti_spoofing: AntiSpoofingConfig {
                    // Off: the bundled checkpoints reject real faces (#76). Keep in sync
                    // with AntiSpoofingConfig::enable in auth_config.h.
                    enable: false,
                    model: AntiSpoofingModelConfig {
                        model_id: "mobilenetv3-antispoof".to_string(),
                        threshold: 0.8,
                    },
                    ir_camera: None,
                    ir_warmup_delay_ms: DEFAULT_IR_WARMUP_DELAY_MS,
                    ir_presence_timeout_ms: DEFAULT_IR_PRESENCE_TIMEOUT_MS,
                },
            },
            fingerprint: FingerprintMethodConfig {
                enable: false,
                retries: 1,
                timeout: 5000,
            },
        },
        appearance: "system".to_string(),
    }
}

// Mirror the kMax* clamps in auth/core/auth_config.h and the maxima in
// app/src/app/configuration/-components/validation.ts.
const MAX_FACE_RETRIES: u32 = 10;
const MAX_RETRY_DELAY_MS: u32 = 5000;
const MAX_IR_WARMUP_DELAY_MS: i32 = 5000;
const MAX_IR_PRESENCE_TIMEOUT_MS: i32 = 10000;
const MAX_FINGERPRINT_RETRIES: u32 = 5;
const MAX_FINGERPRINT_TIMEOUT_MS: u32 = 10000;

/// Brings retry, delay and timeout values into the ranges the helper uses,
/// so a hand-edited config.yaml shows (and saves) what actually applies
/// instead of failing form validation on fields the form may not even show.
fn clamp_limits(methods: &mut MethodsConfig) {
    let face = &mut methods.face;
    face.retries = face.retries.clamp(1, MAX_FACE_RETRIES);
    face.retry_delay = face.retry_delay.min(MAX_RETRY_DELAY_MS);
    let ir = &mut face.anti_spoofing;
    ir.ir_warmup_delay_ms = ir.ir_warmup_delay_ms.clamp(0, MAX_IR_WARMUP_DELAY_MS);
    ir.ir_presence_timeout_ms = ir
        .ir_presence_timeout_ms
        .clamp(0, MAX_IR_PRESENCE_TIMEOUT_MS);

    let fp = &mut methods.fingerprint;
    fp.retries = fp.retries.clamp(1, MAX_FINGERPRINT_RETRIES);
    // 0 used to mean "wait forever"; the helper now treats it as the maximum.
    if fp.timeout == 0 || fp.timeout > MAX_FINGERPRINT_TIMEOUT_MS {
        fp.timeout = MAX_FINGERPRINT_TIMEOUT_MS;
    }
}

/// Falls back to defaults on a parse error or schema_version mismatch, like
/// readConfig() in auth/core/auth_config.cc.
fn parse_config(content: &str) -> BiopassConfig {
    match serde_yaml::from_str::<BiopassConfig>(content) {
        Ok(mut config) if config.schema_version == CURRENT_SCHEMA_VERSION => {
            clamp_limits(&mut config.methods);
            config
        }
        Ok(config) => {
            eprintln!(
                "Warning: config.yaml schema_version {} does not match expected {}; using defaults",
                config.schema_version, CURRENT_SCHEMA_VERSION
            );
            get_default_config()
        }
        Err(e) => {
            eprintln!(
                "Warning: failed to parse config.yaml ({}); using defaults",
                e
            );
            get_default_config()
        }
    }
}

#[tauri::command]
pub fn load_config(app: AppHandle) -> Result<BiopassConfig, String> {
    let config_path = get_config_path(&app)?;

    if !config_path.exists() {
        return Ok(get_default_config());
    }

    let content = fs::read_to_string(&config_path)
        .map_err(|e| format!("Failed to read config file: {}", e))?;

    Ok(parse_config(&content))
}

#[tauri::command]
pub fn save_config(app: AppHandle, config: BiopassConfig) -> Result<(), String> {
    let config_dir = get_config_dir(&app)?;
    let config_path = get_config_path(&app)?;

    if !config_dir.exists() {
        fs::create_dir_all(&config_dir)
            .map_err(|e| format!("Failed to create config directory: {}", e))?;
    }

    let yaml_content =
        serde_yaml::to_string(&config).map_err(|e| format!("Failed to serialize config: {}", e))?;

    // Temp file in the same dir so the rename is atomic: a crash mid-write
    // can't truncate config.yaml.
    let mut tmp_file = tempfile::NamedTempFile::new_in(&config_dir)
        .map_err(|e| format!("Failed to create temporary config file: {}", e))?;
    tmp_file
        .write_all(yaml_content.as_bytes())
        .map_err(|e| format!("Failed to write temporary config file: {}", e))?;
    tmp_file
        .as_file()
        .sync_all()
        .map_err(|e| format!("Failed to sync temporary config file: {}", e))?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        tmp_file
            .as_file()
            .set_permissions(std::fs::Permissions::from_mode(0o600))
            .map_err(|e| format!("Failed to set config file permissions: {}", e))?;
    }

    tmp_file
        .persist(&config_path)
        .map_err(|e| format!("Failed to save config file: {}", e))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::{KnownModel, KNOWN_MODELS};

    fn known_model(slug: &str) -> &'static KnownModel {
        KNOWN_MODELS
            .iter()
            .find(|m| m.slug == slug)
            .unwrap_or_else(|| panic!("default model '{slug}' is not a built-in model"))
    }

    #[test]
    fn out_of_range_limits_are_clamped_on_load() {
        let mut config = get_default_config();
        config.methods.face.retries = 50;
        config.methods.face.retry_delay = 60000;
        config.methods.face.anti_spoofing.ir_warmup_delay_ms = 20000;
        config.methods.face.anti_spoofing.ir_presence_timeout_ms = 15000;
        config.methods.fingerprint.retries = 0;
        config.methods.fingerprint.timeout = 0;
        let yaml = serde_yaml::to_string(&config).unwrap();

        let loaded = parse_config(&yaml);
        let face = &loaded.methods.face;
        assert_eq!(face.retries, 10);
        assert_eq!(face.retry_delay, 5000);
        assert_eq!(face.anti_spoofing.ir_warmup_delay_ms, 5000);
        assert_eq!(face.anti_spoofing.ir_presence_timeout_ms, 10000);
        assert_eq!(loaded.methods.fingerprint.retries, 1);
        // 0 used to mean "wait forever"; the helper treats it as the maximum.
        assert_eq!(loaded.methods.fingerprint.timeout, 10000);
    }

    #[test]
    fn clamping_touches_nothing_else() {
        let mut config = get_default_config();
        config.methods.face.anti_spoofing.ir_warmup_delay_ms = -5;
        let yaml = serde_yaml::to_string(&config).unwrap();
        let loaded = parse_config(&yaml);
        assert_eq!(loaded.methods.face.anti_spoofing.ir_warmup_delay_ms, 0);
        assert_eq!(
            loaded.methods.face,
            FaceMethodConfig {
                anti_spoofing: AntiSpoofingConfig {
                    ir_warmup_delay_ms: 0,
                    ..config.methods.face.anti_spoofing.clone()
                },
                ..config.methods.face.clone()
            }
        );
        assert_eq!(loaded.methods.fingerprint, config.methods.fingerprint);
    }

    #[test]
    fn default_disables_ai_anti_spoofing() {
        let anti_spoofing = get_default_config().methods.face.anti_spoofing;
        assert!(!anti_spoofing.enable);
        // Preselected model if the user turns it on in the UI.
        assert_eq!(anti_spoofing.model.model_id, "mobilenetv3-antispoof");
        assert_eq!(anti_spoofing.model.threshold, 0.8);
        assert_eq!(anti_spoofing.ir_camera, None);
    }

    #[test]
    fn default_models_are_builtin_with_matching_task() {
        let face = get_default_config().methods.face;
        assert_eq!(known_model(&face.detection.model_id).task, "detection");
        assert_eq!(known_model(&face.recognition.model_id).task, "recognition");
        assert_eq!(
            known_model(&face.anti_spoofing.model.model_id).task,
            "anti_spoofing"
        );
    }

    #[test]
    fn saved_anti_spoofing_choice_is_preserved() {
        let mut config = get_default_config();
        config.methods.face.anti_spoofing.enable = true;
        config.methods.face.anti_spoofing.model.model_id = "minifas-v2".to_string();
        config.methods.face.anti_spoofing.model.threshold = 0.6;
        let yaml = serde_yaml::to_string(&config).unwrap();

        assert_eq!(parse_config(&yaml), config);
    }

    #[test]
    fn unparseable_config_falls_back_to_defaults() {
        assert_eq!(parse_config("not: [valid"), get_default_config());
    }
}

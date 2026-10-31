use serde::{Deserialize, Serialize};
use std::fs;
use std::io::Write;
use tauri::AppHandle;

use crate::paths::{get_config_dir, get_config_path};

/// Bumped whenever the on-disk config.yaml shape changes in a way that isn't
/// forward/backward compatible. Mirrors CURRENT_SCHEMA_VERSION in
/// auth/core/auth_config.h - keep both in sync.
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
                    // Off until a model is validated on real camera crops:
                    // the bundled checkpoints reject real faces (#76).
                    // Mirrors AntiSpoofingConfig::enable in auth_config.h.
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

/// Parses config.yaml content, falling back to defaults on a parse error or a
/// schema_version that doesn't match CURRENT_SCHEMA_VERSION -- mirrors the
/// defaults+warn fallback in auth/core/auth_config.cc's readConfig().
fn parse_config(content: &str) -> BiopassConfig {
    match serde_yaml::from_str::<BiopassConfig>(content) {
        Ok(config) if config.schema_version == CURRENT_SCHEMA_VERSION => config,
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

    // Write to a temp file in the same directory (so the rename below is an
    // atomic same-filesystem operation) then persist over the real path, so a
    // crash mid-write can never leave config.yaml truncated/corrupt.
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

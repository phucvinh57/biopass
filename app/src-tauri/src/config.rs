use serde::{Deserialize, Serialize};
use std::fs;
use std::io::Write;
use std::path::Path;
use tauri::AppHandle;

use crate::paths::{ensure_private_dir, get_config_dir, get_config_path};

/// Bump on incompatible config.yaml changes. Keep in sync with
/// CURRENT_SCHEMA_VERSION in auth/core/auth_config.h.
pub const CURRENT_SCHEMA_VERSION: u32 = 2;

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
pub struct BiopassConfig {
    pub schema_version: u32,
    pub strategy: StrategyConfig,
    pub methods: MethodsConfig,
    pub appearance: String,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
pub struct StrategyConfig {
    pub debug: bool,
    pub execution_mode: String,
    pub order: Vec<String>,
    pub ignore_services: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
pub struct MethodsConfig {
    pub face: FaceMethodConfig,
    pub fingerprint: FingerprintMethodConfig,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
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
#[serde(default)]
pub struct DetectionConfig {
    pub model_id: String,
    pub threshold: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
pub struct RecognitionConfig {
    pub model_id: String,
    pub threshold: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
pub struct AntiSpoofingModelConfig {
    pub model_id: String,
    pub threshold: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
pub struct AntiSpoofingConfig {
    pub enable: bool,
    pub model: AntiSpoofingModelConfig,
    pub ir_camera: Option<String>,
    pub ir_warmup_delay_ms: i32,
    pub ir_presence_timeout_ms: i32,
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
#[serde(default)]
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

// Missing fields take these defaults (#[serde(default)] on each struct), so a
// config that lacks a field keeps the rest of the user's values. Unknown
// fields are ignored on load and dropped by save_config.
impl Default for BiopassConfig {
    fn default() -> Self {
        get_default_config()
    }
}

macro_rules! default_from_config {
    ($($ty:ty => $($field:ident).+;)*) => {
        $(impl Default for $ty {
            fn default() -> Self {
                get_default_config().$($field).+
            }
        })*
    };
}

default_from_config! {
    StrategyConfig => strategy;
    MethodsConfig => methods;
    FaceMethodConfig => methods.face;
    DetectionConfig => methods.face.detection;
    RecognitionConfig => methods.face.recognition;
    AntiSpoofingConfig => methods.face.anti_spoofing;
    AntiSpoofingModelConfig => methods.face.anti_spoofing.model;
    FingerprintMethodConfig => methods.fingerprint;
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

/// Parses config.yaml. A config the app can't read is an error, never
/// defaults: the UI must not show (and then save) defaults over it.
///
/// - YAML that doesn't parse, isn't a mapping, or has a value of the wrong
///   type: error.
/// - schema_version newer than ours: error, so an older app can't downgrade
///   a config written by a newer helper.
/// - schema_version older than ours or missing (pre-v2): defaults. The helper
///   ignores such a config ("only the app migrates"), and the v2 migration
///   drops the old settings. Nothing is written until the user saves.
fn parse_config(content: &str) -> Result<BiopassConfig, String> {
    let value: serde_yaml::Value = serde_yaml::from_str(content)
        .map_err(|e| format!("config.yaml is not valid YAML: {}", e))?;
    if value.is_null() {
        return Ok(get_default_config());
    }
    if !value.is_mapping() {
        return Err("config.yaml is not a YAML mapping".to_string());
    }

    let schema_version = match value.get("schema_version") {
        None => 0,
        Some(v) => v
            .as_u64()
            .ok_or_else(|| "config.yaml schema_version is not a number".to_string())?,
    };
    let current = u64::from(CURRENT_SCHEMA_VERSION);
    if schema_version > current {
        return Err(format!(
            "config.yaml has schema_version {}, newer than this app's {}; update Biopass",
            schema_version, current
        ));
    }
    if schema_version < current {
        eprintln!(
            "Warning: config.yaml schema_version {} is older than {}; using defaults",
            schema_version, current
        );
        let mut config = get_default_config();
        // Kept so a theme saved by set_appearance before any config exists
        // still applies.
        if let Some(appearance) = value.get("appearance").and_then(|v| v.as_str()) {
            config.appearance = appearance.to_string();
        }
        return Ok(config);
    }

    let mut config: BiopassConfig = serde_yaml::from_value(value)
        .map_err(|e| format!("config.yaml has an invalid value: {}", e))?;
    clamp_limits(&mut config.methods);
    Ok(config)
}

#[tauri::command]
pub fn load_config(app: AppHandle) -> Result<BiopassConfig, String> {
    read_config_file(&get_config_path(&app)?)
}

/// Errors name the file, so the user knows what to fix.
fn read_config_file(path: &Path) -> Result<BiopassConfig, String> {
    if !path.exists() {
        return Ok(get_default_config());
    }

    let content = fs::read_to_string(path)
        .map_err(|e| format!("Failed to read {}: {}", path.display(), e))?;

    parse_config(&content).map_err(|e| format!("{}: {}", path.display(), e))
}

#[tauri::command]
pub fn save_config(app: AppHandle, config: BiopassConfig) -> Result<(), String> {
    let yaml_content =
        serde_yaml::to_string(&config).map_err(|e| format!("Failed to serialize config: {}", e))?;
    write_config_file(&app, &yaml_content)
}

/// Returns `content` (None: no config yet) with only `appearance` changed.
/// Works on the raw YAML so the user's other values, including fields this
/// app doesn't know, are kept. Errors instead of replacing a config it can't
/// parse.
///
/// With no config (missing, empty or only comments) it writes `appearance`
/// alone, with no schema_version: the helper keeps ignoring the file, so
/// picking a theme doesn't turn on face login with default settings.
fn with_appearance(content: Option<&str>, appearance: &str) -> Result<String, String> {
    let mut value = match content {
        Some(text) => serde_yaml::from_str::<serde_yaml::Value>(text)
            .map_err(|e| format!("config.yaml is not valid YAML: {}", e))?,
        None => serde_yaml::Value::Null,
    };
    if value.is_null() {
        value = serde_yaml::Value::Mapping(serde_yaml::Mapping::new());
    }
    let map = value
        .as_mapping_mut()
        .ok_or_else(|| "config.yaml is not a YAML mapping".to_string())?;
    map.insert("appearance".into(), appearance.into());
    serde_yaml::to_string(&value).map_err(|e| format!("Failed to serialize config: {}", e))
}

#[tauri::command]
pub fn set_appearance(app: AppHandle, appearance: String) -> Result<(), String> {
    let config_path = get_config_path(&app)?;
    let content = if config_path.exists() {
        Some(
            fs::read_to_string(&config_path)
                .map_err(|e| format!("Failed to read config file: {}", e))?,
        )
    } else {
        None
    };
    let yaml_content = with_appearance(content.as_deref(), &appearance)
        .map_err(|e| format!("{}: {}", config_path.display(), e))?;
    write_config_file(&app, &yaml_content)
}

fn write_config_file(app: &AppHandle, yaml_content: &str) -> Result<(), String> {
    let config_dir = get_config_dir(app)?;
    let config_path = get_config_path(app)?;

    ensure_private_dir(&config_dir)
        .map_err(|e| format!("Failed to create config directory: {}", e))?;

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

        let loaded = parse_config(&yaml).unwrap();
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
        let loaded = parse_config(&yaml).unwrap();
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

        assert_eq!(parse_config(&yaml).unwrap(), config);
    }

    fn custom_config() -> BiopassConfig {
        let mut config = get_default_config();
        config.strategy.debug = true;
        config.methods.face.enable = false;
        config.methods.face.retries = 3;
        config.appearance = "dark".to_string();
        config
    }

    #[test]
    fn missing_field_takes_its_default_only() {
        let mut yaml: serde_yaml::Value =
            serde_yaml::from_str(&serde_yaml::to_string(&custom_config()).unwrap()).unwrap();
        let face = yaml["methods"]["face"].as_mapping_mut().unwrap();
        face.remove("retries");
        face.remove("detection");
        let yaml = serde_yaml::to_string(&yaml).unwrap();

        let loaded = parse_config(&yaml).unwrap();
        let defaults = get_default_config();
        assert_eq!(loaded.methods.face.retries, defaults.methods.face.retries);
        assert_eq!(
            loaded.methods.face.detection,
            defaults.methods.face.detection
        );
        // Everything else keeps the user's values.
        assert!(!loaded.methods.face.enable);
        assert!(loaded.strategy.debug);
        assert_eq!(loaded.appearance, "dark");
    }

    #[test]
    fn unknown_field_does_not_reject_the_config() {
        let yaml = serde_yaml::to_string(&custom_config()).unwrap() + "added_later: 1\n";
        assert_eq!(parse_config(&yaml).unwrap(), custom_config());
    }

    #[test]
    fn unparseable_config_is_an_error() {
        assert!(parse_config("not: [valid").is_err());
        assert!(parse_config("just a string").is_err());
    }

    #[test]
    fn wrong_value_type_is_an_error() {
        let yaml = serde_yaml::to_string(&custom_config())
            .unwrap()
            .replace("retries: 3", "retries: three");
        assert!(parse_config(&yaml).is_err());
    }

    #[test]
    fn newer_schema_is_an_error() {
        let mut config = custom_config();
        config.schema_version = CURRENT_SCHEMA_VERSION + 1;
        let yaml = serde_yaml::to_string(&config).unwrap();
        assert!(parse_config(&yaml).is_err());
    }

    #[test]
    fn older_schema_is_replaced_by_defaults() {
        // Only the theme carries over.
        let mut dark_defaults = get_default_config();
        dark_defaults.appearance = "dark".to_string();

        let mut config = custom_config();
        config.schema_version = CURRENT_SCHEMA_VERSION - 1;
        let yaml = serde_yaml::to_string(&config).unwrap();
        assert_eq!(parse_config(&yaml).unwrap(), dark_defaults);

        // No schema_version: a pre-v2 config, which the helper also ignores.
        let unversioned = "appearance: dark\nmodels: []\n";
        assert_eq!(parse_config(unversioned).unwrap(), dark_defaults);
        let bad_appearance = "appearance: [1]\n";
        assert_eq!(parse_config(bad_appearance).unwrap(), get_default_config());
        assert_eq!(parse_config("").unwrap(), get_default_config());
    }

    /// The execution_mode save_config writes back after loading `value`.
    fn reloaded_execution_mode(value: &str) -> Result<String, String> {
        let yaml = format!(
            "schema_version: {CURRENT_SCHEMA_VERSION}\nstrategy:\n  execution_mode: {value}\n"
        );
        let config = parse_config(&yaml)?;
        let saved: serde_yaml::Value =
            serde_yaml::from_str(&serde_yaml::to_string(&config).unwrap()).unwrap();
        Ok(saved["strategy"]["execution_mode"]
            .as_str()
            .unwrap()
            .to_string())
    }

    #[test]
    fn execution_mode_is_saved_as_loaded() {
        for mode in ["sequential", "parallel"] {
            assert_eq!(reloaded_execution_mode(mode).unwrap(), mode);
        }
        let missing = format!("schema_version: {CURRENT_SCHEMA_VERSION}\nstrategy: {{}}\n");
        let json = serde_json::to_value(parse_config(&missing).unwrap()).unwrap();
        assert_eq!(json["strategy"]["execution_mode"], "parallel");
    }

    #[test]
    fn unknown_execution_mode_does_not_reject_the_config() {
        for mode in ["bogus", "Sequential", "''"] {
            assert!(reloaded_execution_mode(mode).is_ok(), "{mode}");
        }
    }

    #[test]
    fn non_string_execution_mode_is_an_error() {
        for mode in ["[sequential]", "~", "1"] {
            assert!(reloaded_execution_mode(mode).is_err(), "{mode}");
        }
    }

    #[test]
    fn load_error_names_the_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("config.yaml");

        assert_eq!(read_config_file(&path).unwrap(), get_default_config());

        fs::write(&path, "not: [valid").unwrap();
        let err = read_config_file(&path).unwrap_err();
        assert!(err.contains(&path.display().to_string()), "{err}");
    }

    #[test]
    fn set_appearance_changes_only_appearance() {
        let yaml = serde_yaml::to_string(&custom_config()).unwrap() + "added_later: 1\n";
        let updated = with_appearance(Some(&yaml), "light").unwrap();

        let mut expected: serde_yaml::Value = serde_yaml::from_str(&yaml).unwrap();
        expected["appearance"] = "light".into();
        assert_eq!(
            serde_yaml::from_str::<serde_yaml::Value>(&updated).unwrap(),
            expected
        );
    }

    #[test]
    fn set_appearance_leaves_an_unparseable_config_alone() {
        assert!(with_appearance(Some("not: [valid"), "dark").is_err());
        assert!(with_appearance(Some("just a string"), "dark").is_err());
    }

    /// Picking a theme must not create a config the helper would act on: with
    /// no schema_version the helper keeps ignoring it.
    #[test]
    fn set_appearance_without_a_config_writes_only_appearance() {
        let only_appearance: serde_yaml::Value = serde_yaml::from_str("appearance: dark").unwrap();
        for content in [None, Some(""), Some("# just a comment\n")] {
            let written = with_appearance(content, "dark").unwrap();
            assert_eq!(
                serde_yaml::from_str::<serde_yaml::Value>(&written).unwrap(),
                only_appearance,
                "content: {content:?}"
            );

            let mut expected = get_default_config();
            expected.appearance = "dark".to_string();
            assert_eq!(parse_config(&written).unwrap(), expected);
        }
    }
}

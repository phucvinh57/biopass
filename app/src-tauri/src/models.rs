use std::fs::File;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime};

use futures_util::StreamExt;
use rusqlite::TransactionBehavior;
use serde::Serialize;
use sha2::{Digest, Sha256};
use tauri::{AppHandle, Emitter};

use crate::config::{load_config, BiopassConfig};
use crate::db::{self, Model};
use crate::paths::{ensure_private_dir, get_data_dir};

const VALID_TASKS: [&str; 3] = ["detection", "recognition", "anti_spoofing"];
const ANTI_SPOOFING_ARCHITECTURES: [&str; 2] = ["mobilenetv3", "minifasv2"];
const PROGRESS_EVENT: &str = "model-download-progress";
const DEFAULT_MODELS_EVENT: &str = "default-models-updated";
const RELEASES_URL: &str = "https://github.com/phucvinh57/biopass/releases";
const CONNECT_TIMEOUT: Duration = Duration::from_secs(15);
const READ_TIMEOUT: Duration = Duration::from_secs(30);
const MAX_USER_MODEL_BYTES: u64 = 1 << 30;
const STALE_TEMP_AGE: Duration = Duration::from_secs(10 * 60);
const PROGRESS_THROTTLE_MS: u128 = 150;
const MAX_NAME_LEN: usize = 200;

#[tauri::command]
pub fn list_models(app: AppHandle, task: Option<String>) -> Result<Vec<Model>, String> {
    let conn = db::open(&app)?;
    db::list_models(&conn, task.as_deref())
}

#[derive(Clone, Serialize)]
struct DownloadProgress {
    id: String,
    downloaded: u64,
    total: Option<u64>,
}

fn models_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = get_data_dir(app)?.join("models");
    ensure_private_dir(&dir).map_err(|e| format!("Failed to create models directory: {}", e))?;
    Ok(dir)
}

/// Only anti-spoofing models take an architecture; auth picks preprocessing
/// and output decoding from it.
fn validate_task(task: &str, architecture: Option<&str>) -> Result<(), String> {
    if !VALID_TASKS.contains(&task) {
        return Err(format!(
            "Invalid model task '{}'; expected one of {:?}",
            task, VALID_TASKS
        ));
    }
    match (task, architecture) {
        ("anti_spoofing", Some(arch)) if ANTI_SPOOFING_ARCHITECTURES.contains(&arch) => Ok(()),
        ("anti_spoofing", _) => Err(format!(
            "Anti-spoofing models need an architecture; expected one of {:?}",
            ANTI_SPOOFING_ARCHITECTURES
        )),
        (_, None) => Ok(()),
        (_, Some(_)) => Err(format!("{} models do not take an architecture", task)),
    }
}

fn validate_name(name: &str) -> Result<String, String> {
    let name = name.trim().to_string();
    if name.is_empty() {
        return Err("Model name cannot be empty".to_string());
    }
    if name.len() > MAX_NAME_LEN {
        return Err("Model name is too long".to_string());
    }
    Ok(name)
}

fn slugify(name: &str) -> String {
    let mut slug: String = name
        .trim()
        .to_lowercase()
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '-' })
        .collect();
    while slug.contains("--") {
        slug = slug.replace("--", "-");
    }
    let slug = slug.trim_matches('-').to_string();
    if slug.is_empty() {
        "model".to_string()
    } else {
        slug
    }
}

fn model_path(dir: &Path, id: &str) -> PathBuf {
    dir.join(format!("{id}.onnx"))
}

/// Claims an id, moves `tmp` to `<id>.onnx` and inserts the row in one
/// write transaction, so concurrent adds never share an id or a file and a
/// failed move leaves no row behind.
fn register_user_model(
    app: &AppHandle,
    dir: &Path,
    tmp: tempfile::NamedTempFile,
    name: &str,
    task: &str,
    architecture: Option<&str>,
    checksum: &str,
) -> Result<Model, String> {
    let mut conn = db::open(app)?;
    let tx = conn
        .transaction_with_behavior(TransactionBehavior::Immediate)
        .map_err(|e| format!("Failed to start database transaction: {}", e))?;
    let new = db::NewModel {
        name,
        task,
        architecture,
        checksum: Some(checksum),
        source: "user",
    };
    let id = db::insert_with_unique_id(&tx, &slugify(name), &new, |id| {
        model_path(dir, id).to_string_lossy().into_owned()
    })?;
    install_file(tmp, &model_path(dir, &id))?;
    tx.commit()
        .map_err(|e| format!("Failed to save model: {}", e))?;
    db::get_model(&conn, &id)?.ok_or_else(|| "Model vanished after insert".to_string())
}

/// Heuristic, not a parse: a serialized ModelProto starts with ir_version
/// (field 1, varint), i.e. byte 0x08. Rejects HTML error pages, JSON, etc.
fn looks_like_onnx(bytes: &[u8]) -> bool {
    matches!(bytes.first(), Some(0x08))
}

fn validate_onnx_file(path: &Path) -> Result<(), String> {
    if path
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| e.to_ascii_lowercase())
        != Some("onnx".to_string())
    {
        return Err("Selected file must have a .onnx extension".to_string());
    }
    let mut file = File::open(path).map_err(|e| format!("Failed to open model file: {}", e))?;
    let mut header = [0u8; 16];
    let n = file
        .read(&mut header)
        .map_err(|e| format!("Failed to read model file: {}", e))?;
    if n == 0 || !looks_like_onnx(&header[..n]) {
        return Err("File does not look like a valid ONNX model".to_string());
    }
    Ok(())
}

fn hex_encode(bytes: impl AsRef<[u8]>) -> String {
    bytes
        .as_ref()
        .iter()
        .map(|b| format!("{:02x}", b))
        .collect()
}

fn sha256_hex(path: &Path) -> Result<String, String> {
    let mut file = File::open(path).map_err(|e| format!("Failed to open model file: {}", e))?;
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 8192];
    loop {
        let n = file
            .read(&mut buf)
            .map_err(|e| format!("Failed to hash model file: {}", e))?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(hex_encode(hasher.finalize()))
}

fn sha256_file(path: &Path) -> Result<String, String> {
    Ok(format!("sha256:{}", sha256_hex(path)?))
}

fn verify_sha256(actual_hex: &str, expected_hex: &str) -> Result<(), String> {
    if actual_hex.eq_ignore_ascii_case(expected_hex) {
        Ok(())
    } else {
        Err(format!(
            "checksum mismatch: expected sha256 {}, got {}",
            expected_hex, actual_hex
        ))
    }
}

/// A built-in model counts as installed only if it has its pinned size and
/// hash; anything else (missing, 0-byte, partial, different) is downloaded
/// again.
fn is_installed(path: &Path, expected_sha256: &str, expected_size: u64) -> bool {
    if !std::fs::metadata(path).is_ok_and(|m| m.is_file() && m.len() == expected_size) {
        return false;
    }
    sha256_hex(path).is_ok_and(|h| verify_sha256(&h, expected_sha256).is_ok())
}

/// Renames an already-synced temp file (same directory) over `dest`, then
/// fsyncs the directory so the rename survives a crash. The data sync
/// happens earlier so callers holding a DB write lock only pay for this.
fn install_file(tmp: tempfile::NamedTempFile, dest: &Path) -> Result<(), String> {
    tmp.persist(dest)
        .map_err(|e| format!("Failed to save model file: {}", e))?;
    if let Some(dir) = dest.parent() {
        File::open(dir)
            .and_then(|d| d.sync_all())
            .map_err(|e| format!("Failed to sync models directory: {}", e))?;
    }
    Ok(())
}

fn sync_temp(tmp: &tempfile::NamedTempFile) -> Result<(), String> {
    tmp.as_file()
        .sync_all()
        .map_err(|e| format!("Failed to sync model file: {}", e))
}

/// Deletes `.tmp*` files a crashed download left in the models directory.
/// Best effort. Only regular files (symlinks are not followed) untouched for
/// STALE_TEMP_AGE, so a download still in progress keeps its file.
fn sweep_stale_temps(dir: &Path, now: SystemTime) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        if !entry.file_name().to_string_lossy().starts_with(".tmp") {
            continue;
        }
        // DirEntry::file_type/metadata do not follow symlinks.
        if !entry.file_type().is_ok_and(|t| t.is_file()) {
            continue;
        }
        let stale = entry
            .metadata()
            .and_then(|m| m.modified())
            .is_ok_and(|t| now.duration_since(t).is_ok_and(|age| age >= STALE_TEMP_AGE));
        if stale {
            let _ = std::fs::remove_file(entry.path());
        }
    }
}

/// https only, redirects included, so an unpinned user model can't be
/// downgraded to plain http by a redirect. No overall timeout (big models
/// on slow links are fine), but a stalled connection fails.
fn http_client() -> Result<reqwest::Client, String> {
    reqwest::Client::builder()
        .https_only(true)
        .connect_timeout(CONNECT_TIMEOUT)
        .read_timeout(READ_TIMEOUT)
        .build()
        .map_err(|e| format!("Failed to create HTTP client: {}", e))
}

fn check_size(bytes: u64, max_bytes: u64) -> Result<(), String> {
    if bytes > max_bytes {
        return Err(format!("Model is larger than the {} byte limit", max_bytes));
    }
    Ok(())
}

enum DownloadError {
    NotFound,
    Failed(String),
}

impl From<DownloadError> for String {
    fn from(e: DownloadError) -> String {
        match e {
            DownloadError::NotFound => "Download failed with status 404 Not Found".to_string(),
            DownloadError::Failed(e) => e,
        }
    }
}

/// Streams `url` into a temp file in `dir`, hashing as it goes, and fsyncs
/// it. Rejects bodies over `max_bytes`, empty bodies and anything that does
/// not start like an ONNX model. The temp file is deleted on any error
/// (dropped), so nothing partial is left.
async fn download_to_temp(
    client: &reqwest::Client,
    url: &str,
    dir: &Path,
    max_bytes: u64,
    mut on_progress: impl FnMut(u64, Option<u64>),
) -> Result<(tempfile::NamedTempFile, String), DownloadError> {
    let fail = DownloadError::Failed;
    let response = client
        .get(url)
        .send()
        .await
        .map_err(|e| fail(format!("Failed to start download: {}", e)))?;
    if response.status() == reqwest::StatusCode::NOT_FOUND {
        return Err(DownloadError::NotFound);
    }
    if !response.status().is_success() {
        return Err(fail(format!(
            "Download failed with status {}",
            response.status()
        )));
    }
    let total = response.content_length();
    if let Some(total) = total {
        check_size(total, max_bytes).map_err(fail)?;
    }

    let mut tmp = tempfile::NamedTempFile::new_in(dir)
        .map_err(|e| fail(format!("Failed to create temp file: {}", e)))?;
    let mut hasher = Sha256::new();
    let mut downloaded: u64 = 0;
    let mut last_emit = Instant::now();
    let mut header_checked = false;
    let mut stream = response.bytes_stream();

    while let Some(chunk) = stream.next().await {
        let chunk = chunk.map_err(|e| fail(format!("Download error: {}", e)))?;
        if !header_checked && !chunk.is_empty() {
            header_checked = true;
            if !looks_like_onnx(&chunk) {
                return Err(fail(
                    "Downloaded file does not look like a valid ONNX model".to_string(),
                ));
            }
        }
        downloaded += chunk.len() as u64;
        check_size(downloaded, max_bytes).map_err(fail)?;
        tmp.write_all(&chunk)
            .map_err(|e| fail(format!("Failed to write model file: {}", e)))?;
        hasher.update(&chunk);

        if last_emit.elapsed().as_millis() >= PROGRESS_THROTTLE_MS {
            on_progress(downloaded, total);
            last_emit = Instant::now();
        }
    }
    on_progress(downloaded, total);

    if !header_checked {
        return Err(fail("Downloaded file is empty".to_string()));
    }
    sync_temp(&tmp).map_err(fail)?;
    Ok((tmp, hex_encode(hasher.finalize())))
}

/// Built-ins come from this app version's own release first, so the pins
/// always match. Dev builds (e.g. 1.4.1-116-g74b2960) have no such release
/// (404) and fall back to the latest one.
fn default_model_urls(version: &str, filename: &str) -> [String; 2] {
    [
        format!("{RELEASES_URL}/download/{version}/{filename}"),
        format!("{RELEASES_URL}/latest/download/{filename}"),
    ]
}

/// Plain http lets anyone on the path swap the model the PAM module will run.
fn require_https(url: &str) -> Result<(), String> {
    let parsed = reqwest::Url::parse(url).map_err(|e| format!("Invalid URL: {}", e))?;
    if parsed.scheme() != "https" || !parsed.has_host() {
        return Err("URL must start with https://".to_string());
    }
    Ok(())
}

#[tauri::command]
pub async fn add_model_from_url(
    app: AppHandle,
    name: String,
    task: String,
    architecture: Option<String>,
    url: String,
) -> Result<Model, String> {
    validate_task(&task, architecture.as_deref())?;
    let name = validate_name(&name)?;
    require_https(&url)?;

    let dir = models_dir(&app)?;
    // Progress only: the real id is claimed after the download.
    let id = slugify(&name);

    let client = http_client()?;
    let (tmp, sha256) = download_to_temp(
        &client,
        &url,
        &dir,
        MAX_USER_MODEL_BYTES,
        |downloaded, total| {
            let _ = app.emit(
                PROGRESS_EVENT,
                DownloadProgress {
                    id: id.clone(),
                    downloaded,
                    total,
                },
            );
        },
    )
    .await?;

    register_user_model(
        &app,
        &dir,
        tmp,
        &name,
        &task,
        architecture.as_deref(),
        &format!("sha256:{sha256}"),
    )
}

/// Downloads `known` unless it is already installed. Returns whether it
/// downloaded.
async fn ensure_builtin(
    client: &reqwest::Client,
    dir: &Path,
    known: &'static db::KnownModel,
) -> Result<bool, String> {
    let dest = dir.join(known.filename);
    let check = dest.clone();
    let installed = tauri::async_runtime::spawn_blocking(move || {
        is_installed(&check, known.sha256, known.size)
    })
    .await
    .unwrap_or(false);
    if installed {
        return Ok(false);
    }

    let fail = |e: String| format!("Failed to download {}: {}", known.display_name, e);
    let mut fetched = None;
    for url in default_model_urls(env!("CARGO_PKG_VERSION"), known.filename) {
        match download_to_temp(client, &url, dir, known.size, |_, _| {}).await {
            Ok(r) => {
                fetched = Some(r);
                break;
            }
            Err(DownloadError::NotFound) => continue,
            Err(e) => return Err(fail(e.into())),
        }
    }
    let (tmp, sha256) = fetched.ok_or_else(|| fail(DownloadError::NotFound.into()))?;
    // On mismatch `tmp` is dropped, which deletes it.
    verify_sha256(&sha256, known.sha256).map_err(fail)?;
    install_file(tmp, &dest)?;
    Ok(true)
}

/// Downloads any built-in model missing from the user's models directory.
/// db::open registers them on the next read. Returns the downloaded names;
/// if any model fails, the others are still tried and all errors returned.
#[tauri::command]
pub async fn ensure_default_models(app: AppHandle) -> Result<Vec<String>, String> {
    let dir = models_dir(&app)?;
    let sweep_dir = dir.clone();
    let _ = tauri::async_runtime::spawn_blocking(move || {
        sweep_stale_temps(&sweep_dir, SystemTime::now())
    })
    .await;

    let held_by_user: Vec<&str> = {
        let conn = db::open(&app)?;
        let mut held = Vec::new();
        for known in db::KNOWN_MODELS {
            if db::is_user_model(&conn, known.slug)? {
                held.push(known.slug);
            }
        }
        held
    };
    let client = http_client()?;
    let mut downloaded = Vec::new();
    let mut errors = Vec::new();

    for known in db::KNOWN_MODELS {
        if held_by_user.contains(&known.slug) {
            eprintln!(
                "models: skipping built-in {}: id '{}' belongs to a user model",
                known.display_name, known.slug
            );
            continue;
        }
        match ensure_builtin(&client, &dir, known).await {
            Ok(true) => {
                downloaded.push(known.display_name.to_string());
                let _ = app.emit(DEFAULT_MODELS_EVENT, ());
            }
            Ok(false) => {}
            Err(e) => errors.push(e),
        }
    }

    if errors.is_empty() {
        Ok(downloaded)
    } else {
        Err(errors.join("; "))
    }
}

#[tauri::command]
pub fn add_model_from_file(
    app: AppHandle,
    name: String,
    task: String,
    architecture: Option<String>,
    src_path: String,
) -> Result<Model, String> {
    validate_task(&task, architecture.as_deref())?;
    let name = validate_name(&name)?;

    let src = PathBuf::from(&src_path);
    validate_onnx_file(&src)?;

    let dir = models_dir(&app)?;
    let mut tmp = tempfile::NamedTempFile::new_in(&dir)
        .map_err(|e| format!("Failed to create temp file: {}", e))?;
    let mut src_file =
        File::open(&src).map_err(|e| format!("Failed to open source file: {}", e))?;
    std::io::copy(&mut src_file, tmp.as_file_mut())
        .map_err(|e| format!("Failed to copy model file: {}", e))?;
    sync_temp(&tmp)?;

    let checksum = sha256_file(tmp.path())?;
    register_user_model(
        &app,
        &dir,
        tmp,
        &name,
        &task,
        architecture.as_deref(),
        &checksum,
    )
}

/// Delete protection: a model stays referenced while a disabled method (or
/// disabled anti-spoofing) still names it, so re-enabling never resolves to a
/// deleted model. Same rule as `referencedModelIds(config, { includeDisabled:
/// true })` in app/src/lib/models.ts.
fn is_model_referenced(config: &BiopassConfig, id: &str) -> bool {
    let face = &config.methods.face;
    !id.is_empty()
        && [
            &face.detection.model_id,
            &face.recognition.model_id,
            &face.anti_spoofing.model.model_id,
        ]
        .iter()
        .any(|m| m.as_str() == id)
}

#[tauri::command]
pub fn delete_model(app: AppHandle, id: String) -> Result<(), String> {
    let conn = db::open(&app)?;
    let model = db::get_model(&conn, &id)?.ok_or_else(|| format!("Model '{}' not found", id))?;

    if model.source == "builtin" {
        return Err("Default models cannot be deleted".to_string());
    }

    let config = load_config(app.clone())?;
    if is_model_referenced(&config, &id) {
        return Err("Model is currently in use and cannot be deleted".to_string());
    }

    let dir = models_dir(&app)?;
    let path = PathBuf::from(&model.path);
    if path.starts_with(&dir) {
        match std::fs::remove_file(&path) {
            Ok(()) => {}
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => return Err(format!("Failed to delete model file: {}", e)),
        }
    }

    db::delete_model(&conn, &id)
}

#[tauri::command]
pub fn rename_model(app: AppHandle, id: String, name: String) -> Result<Model, String> {
    let name = validate_name(&name)?;
    let conn = db::open(&app)?;
    db::update_model_name(&conn, &id, &name)?;
    db::get_model(&conn, &id)?.ok_or_else(|| format!("Model '{}' not found", id))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn delete_protection_counts_disabled_methods() {
        let mut config = crate::config::BiopassConfig::default();
        config.methods.face.enable = false;
        config.methods.face.anti_spoofing.enable = false;
        config.methods.face.detection.model_id = "det".into();
        config.methods.face.recognition.model_id = "rec".into();
        config.methods.face.anti_spoofing.model.model_id = "spoof".into();
        for id in ["det", "rec", "spoof"] {
            assert!(is_model_referenced(&config, id), "{id}");
        }
        assert!(!is_model_referenced(&config, "other"));
        config.methods.face.anti_spoofing.model.model_id = String::new();
        assert!(!is_model_referenced(&config, ""));
    }

    #[test]
    fn model_urls_must_be_https() {
        assert!(require_https("https://example.com/m.onnx").is_ok());
        assert!(require_https("HTTPS://example.com/m.onnx").is_ok());
        assert!(require_https("http://example.com/m.onnx").is_err());
        assert!(require_https("ftp://example.com/m.onnx").is_err());
        assert!(require_https("file:///tmp/m.onnx").is_err());
        assert!(require_https("example.com/m.onnx").is_err());
        assert!(require_https("https://").is_err());
    }

    const ABC_SHA256: &str = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    #[test]
    fn sha256_must_match_pin() {
        assert!(verify_sha256(ABC_SHA256, ABC_SHA256).is_ok());
        assert!(verify_sha256(&ABC_SHA256.to_uppercase(), ABC_SHA256).is_ok());
        assert!(verify_sha256(&"0".repeat(64), ABC_SHA256).is_err());
        assert!(verify_sha256("", ABC_SHA256).is_err());
    }

    #[test]
    fn installed_requires_pinned_size_and_hash() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("m.onnx");
        let installed = |p: &Path| is_installed(p, ABC_SHA256, 3);
        assert!(!installed(&path), "missing file");

        std::fs::write(&path, b"").unwrap();
        assert!(!installed(&path), "0-byte file");

        std::fs::write(&path, b"ab").unwrap();
        assert!(!installed(&path), "partial file");

        std::fs::write(&path, b"abd").unwrap();
        assert!(!installed(&path), "same size, other content");

        std::fs::write(&path, b"abc").unwrap();
        assert!(installed(&path));

        assert!(!installed(dir.path()), "directory");
    }

    #[test]
    fn size_cap_allows_up_to_the_limit() {
        assert!(check_size(0, 3).is_ok());
        assert!(check_size(3, 3).is_ok());
        assert!(check_size(4, 3).is_err());
        assert!(check_size(MAX_USER_MODEL_BYTES, MAX_USER_MODEL_BYTES).is_ok());
        assert!(check_size(MAX_USER_MODEL_BYTES + 1, MAX_USER_MODEL_BYTES).is_err());
    }

    #[test]
    fn default_model_urls_try_own_release_then_latest() {
        assert_eq!(
            default_model_urls("1.4.2", "minifas_v2.onnx"),
            [
                "https://github.com/phucvinh57/biopass/releases/download/1.4.2/minifas_v2.onnx",
                "https://github.com/phucvinh57/biopass/releases/latest/download/minifas_v2.onnx",
            ]
        );
        for url in default_model_urls(env!("CARGO_PKG_VERSION"), "m.onnx") {
            assert!(require_https(&url).is_ok(), "{url}");
        }
    }

    #[test]
    fn sweep_removes_only_stale_regular_temp_files() {
        let dir = tempfile::tempdir().unwrap();
        let outside = tempfile::tempdir().unwrap();
        let old = SystemTime::now() - STALE_TEMP_AGE - Duration::from_secs(60);
        let make = |name: &str, mtime: Option<SystemTime>| {
            let path = dir.path().join(name);
            let f = File::create(&path).unwrap();
            if let Some(t) = mtime {
                f.set_modified(t).unwrap();
            }
            path
        };
        let stale = make(".tmpAbC123", Some(old));
        let fresh = make(".tmpXyZ789", None);
        let model = make("old-model.onnx", Some(old));
        let target = outside.path().join("target");
        File::create(&target).unwrap().set_modified(old).unwrap();
        let link = dir.path().join(".tmpLink00");
        std::os::unix::fs::symlink(&target, &link).unwrap();
        std::fs::create_dir(dir.path().join(".tmpDir000")).unwrap();

        sweep_stale_temps(dir.path(), SystemTime::now());

        assert!(!stale.exists(), "stale temp file");
        assert!(fresh.exists(), "in-progress temp file");
        assert!(model.exists(), "model file");
        assert!(link.symlink_metadata().is_ok(), "symlink");
        assert!(target.exists(), "symlink target");
        assert!(dir.path().join(".tmpDir000").is_dir(), "directory");
    }

    #[test]
    fn install_file_replaces_dest_and_leaves_no_temp() {
        let dir = tempfile::tempdir().unwrap();
        let dest = dir.path().join("m.onnx");
        std::fs::write(&dest, b"").unwrap();

        let mut tmp = tempfile::NamedTempFile::new_in(dir.path()).unwrap();
        tmp.write_all(b"abc").unwrap();
        install_file(tmp, &dest).unwrap();

        assert_eq!(std::fs::read(&dest).unwrap(), b"abc");
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
    }

    #[test]
    fn builtin_pins_are_sha256_hex() {
        for known in db::KNOWN_MODELS {
            assert_eq!(known.sha256.len(), 64, "{}", known.filename);
            assert!(
                known
                    .sha256
                    .chars()
                    .all(|c| c.is_ascii_digit() || ('a'..='f').contains(&c)),
                "{}",
                known.filename
            );
        }
    }

    /// The release ships auth/face/models/*.onnx; when the real files are
    /// checked out (not LFS pointers), the pins must match them.
    #[test]
    fn builtin_pins_match_repo_models() {
        let repo_models = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../auth/face/models");
        for known in db::KNOWN_MODELS {
            let path = repo_models.join(known.filename);
            let mut head = [0u8; 1];
            if File::open(&path)
                .and_then(|mut f| f.read_exact(&mut head))
                .is_err()
            {
                continue;
            }
            if !looks_like_onnx(&head) {
                continue; // LFS pointer
            }
            assert_eq!(
                sha256_hex(&path).unwrap(),
                known.sha256,
                "{}",
                known.filename
            );
            assert_eq!(
                std::fs::metadata(&path).unwrap().len(),
                known.size,
                "{}",
                known.filename
            );
        }
    }
}

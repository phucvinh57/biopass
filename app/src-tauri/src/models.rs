use std::fs::File;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::time::Instant;

use futures_util::StreamExt;
use rusqlite::TransactionBehavior;
use serde::Serialize;
use sha2::{Digest, Sha256};
use tauri::{AppHandle, Emitter};

use crate::config::load_config;
use crate::db::{self, Model};
use crate::paths::get_data_dir;

const VALID_TASKS: [&str; 3] = ["detection", "recognition", "anti_spoofing"];
const ANTI_SPOOFING_ARCHITECTURES: [&str; 2] = ["mobilenetv3", "minifasv2"];
const PROGRESS_EVENT: &str = "model-download-progress";
const DEFAULT_MODELS_EVENT: &str = "default-models-updated";
const DEFAULT_MODELS_URL: &str = "https://github.com/phucvinh57/biopass/releases/latest/download";
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
    std::fs::create_dir_all(&dir)
        .map_err(|e| format!("Failed to create models directory: {}", e))?;
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

/// A built-in model counts as installed only if it is non-empty and matches
/// its pinned hash; anything else gets downloaded again.
fn is_installed(path: &Path, expected_sha256: &str) -> bool {
    if !std::fs::metadata(path).is_ok_and(|m| m.is_file() && m.len() > 0) {
        return false;
    }
    sha256_hex(path).is_ok_and(|h| verify_sha256(&h, expected_sha256).is_ok())
}

/// Moves a fully written temp file (same directory) over `dest`: fsync the
/// data, rename, then fsync the directory so the rename survives a crash.
fn install_file(tmp: tempfile::NamedTempFile, dest: &Path) -> Result<(), String> {
    tmp.as_file()
        .sync_all()
        .map_err(|e| format!("Failed to sync model file: {}", e))?;
    tmp.persist(dest)
        .map_err(|e| format!("Failed to save model file: {}", e))?;
    if let Some(dir) = dest.parent() {
        File::open(dir)
            .and_then(|d| d.sync_all())
            .map_err(|e| format!("Failed to sync models directory: {}", e))?;
    }
    Ok(())
}

/// Streams `url` into a temp file in `dir`, hashing as it goes. Rejects
/// empty bodies and anything that does not start like an ONNX model. The
/// temp file is deleted on any error (dropped), so nothing partial is left.
async fn download_to_temp(
    url: &str,
    dir: &Path,
    mut on_progress: impl FnMut(u64, Option<u64>),
) -> Result<(tempfile::NamedTempFile, String), String> {
    let response = reqwest::get(url)
        .await
        .map_err(|e| format!("Failed to start download: {}", e))?;
    if !response.status().is_success() {
        return Err(format!("Download failed with status {}", response.status()));
    }
    let total = response.content_length();

    let mut tmp = tempfile::NamedTempFile::new_in(dir)
        .map_err(|e| format!("Failed to create temp file: {}", e))?;
    let mut hasher = Sha256::new();
    let mut downloaded: u64 = 0;
    let mut last_emit = Instant::now();
    let mut header_checked = false;
    let mut stream = response.bytes_stream();

    while let Some(chunk) = stream.next().await {
        let chunk = chunk.map_err(|e| format!("Download error: {}", e))?;
        if !header_checked && !chunk.is_empty() {
            header_checked = true;
            if !looks_like_onnx(&chunk) {
                return Err("Downloaded file does not look like a valid ONNX model".to_string());
            }
        }
        tmp.write_all(&chunk)
            .map_err(|e| format!("Failed to write model file: {}", e))?;
        hasher.update(&chunk);
        downloaded += chunk.len() as u64;

        if last_emit.elapsed().as_millis() >= PROGRESS_THROTTLE_MS {
            on_progress(downloaded, total);
            last_emit = Instant::now();
        }
    }
    on_progress(downloaded, total);

    if !header_checked {
        return Err("Downloaded file is empty".to_string());
    }
    Ok((tmp, hex_encode(hasher.finalize())))
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

    let (tmp, sha256) = download_to_temp(&url, &dir, |downloaded, total| {
        let _ = app.emit(
            PROGRESS_EVENT,
            DownloadProgress {
                id: id.clone(),
                downloaded,
                total,
            },
        );
    })
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

/// Downloads any built-in model missing from the user's models directory.
/// db::open registers them on the next read. Returns the downloaded names.
#[tauri::command]
pub async fn ensure_default_models(app: AppHandle) -> Result<Vec<String>, String> {
    let dir = models_dir(&app)?;
    let mut downloaded = Vec::new();

    for known in db::KNOWN_MODELS {
        let dest = dir.join(known.filename);
        let check = dest.clone();
        let installed =
            tauri::async_runtime::spawn_blocking(move || is_installed(&check, known.sha256))
                .await
                .unwrap_or(false);
        if installed {
            continue;
        }
        let url = format!("{DEFAULT_MODELS_URL}/{}", known.filename);
        let fail = |e: String| format!("Failed to download {}: {}", known.display_name, e);
        let (tmp, sha256) = download_to_temp(&url, &dir, |_, _| {})
            .await
            .map_err(fail)?;
        // On mismatch `tmp` is dropped, which deletes it.
        verify_sha256(&sha256, known.sha256).map_err(fail)?;
        install_file(tmp, &dest)?;

        downloaded.push(known.display_name.to_string());
        let _ = app.emit(DEFAULT_MODELS_EVENT, ());
    }

    Ok(downloaded)
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

#[tauri::command]
pub fn delete_model(app: AppHandle, id: String) -> Result<(), String> {
    let conn = db::open(&app)?;
    let model = db::get_model(&conn, &id)?.ok_or_else(|| format!("Model '{}' not found", id))?;

    if model.source == "builtin" {
        return Err("Default models cannot be deleted".to_string());
    }

    let config = load_config(app.clone())?;
    let in_use = config.methods.face.detection.model_id == id
        || config.methods.face.recognition.model_id == id
        || config.methods.face.anti_spoofing.model.model_id == id;
    if in_use {
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
    fn installed_requires_non_empty_file_with_pinned_hash() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("m.onnx");
        assert!(!is_installed(&path, ABC_SHA256), "missing file");

        std::fs::write(&path, b"").unwrap();
        assert!(!is_installed(&path, ABC_SHA256), "0-byte file");

        std::fs::write(&path, b"ab").unwrap();
        assert!(!is_installed(&path, ABC_SHA256), "partial file");

        std::fs::write(&path, b"abc").unwrap();
        assert!(is_installed(&path, ABC_SHA256));

        assert!(!is_installed(dir.path(), ABC_SHA256), "directory");
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
        }
    }
}

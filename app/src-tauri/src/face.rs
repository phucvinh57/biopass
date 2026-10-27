use std::fs;
use tauri::AppHandle;

use crate::config::{load_config, BiopassConfig};
use crate::db;
use crate::paths::get_faces_dir;

#[tauri::command]
pub fn capture_face(app: AppHandle, camera: Option<String>) -> Result<String, String> {
    let faces_dir = get_faces_dir(&app)?;
    let app_config: BiopassConfig = load_config(app.clone())?;

    let timestamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| format!("Failed to get timestamp: {}", e))?
        .as_millis();
    let filename = format!("face_{}.jpg", timestamp);
    let file_path = faces_dir.join(&filename);

    if !faces_dir.exists() {
        fs::create_dir_all(&faces_dir)
            .map_err(|e| format!("Failed to create faces directory: {}", e))?;
    }

    let conn = db::open(&app)?;
    let model_id = &app_config.methods.face.detection.model_id;
    let detect_model = db::resolve_model_path(&conn, model_id)?
        .ok_or_else(|| format!("Detection model '{}' not found in registry", model_id))?;

    let helper_bin = if std::path::Path::new("/usr/bin/biopass-helper").exists() {
        "/usr/bin/biopass-helper".to_string()
    } else if std::path::Path::new("../../auth/build/pam/biopass-helper").exists() {
        "../../auth/build/pam/biopass-helper".to_string()
    } else {
        "biopass-helper".to_string()
    };

    let mut cmd_builder = std::process::Command::new(&helper_bin);
    cmd_builder
        .arg("capture-face")
        .arg("--output")
        .arg(&file_path)
        .arg("--model")
        .arg(&detect_model);

    if let Some(cam) = camera.filter(|s| !s.is_empty()) {
        cmd_builder.arg("--camera").arg(cam);
    }

    let output = cmd_builder
        .output()
        .map_err(|e| format!("Failed to execute helper: {}", e))?;

    if output.status.success() {
        // The helper reports the final path ("SAVED <path>"), which may carry a
        // sensor tag (.ir/.rgb) the app did not know about.
        let stdout = String::from_utf8_lossy(&output.stdout);
        let saved = stdout
            .lines()
            .rev()
            .find_map(|l| l.strip_prefix("SAVED "))
            .map(str::trim)
            .filter(|p| !p.is_empty())
            .map(str::to_string);
        Ok(saved.unwrap_or_else(|| file_path.to_string_lossy().to_string()))
    } else if output.status.code() == Some(2) {
        Err("No face detected. Please position your face in front of the camera.".to_string())
    } else {
        let stderr = String::from_utf8_lossy(&output.stderr);
        Err(format!(
            "Capture failed (exit {}): {}",
            output.status.code().unwrap_or(-1),
            stderr
        ))
    }
}

#[tauri::command]
pub fn list_faces(app: AppHandle) -> Result<Vec<String>, String> {
    let faces_dir = get_faces_dir(&app)?;

    if !faces_dir.exists() {
        return Ok(vec![]);
    }

    let entries =
        fs::read_dir(&faces_dir).map_err(|e| format!("Failed to read faces directory: {}", e))?;

    let mut files: Vec<String> = entries
        .filter_map(|e| e.ok())
        .filter(|e| {
            e.path()
                .extension()
                .map_or(false, |ext| ext == "jpg" || ext == "png")
        })
        .map(|e| e.path().to_string_lossy().to_string())
        .collect();

    files.sort();
    Ok(files)
}

#[tauri::command]
pub fn delete_face(path: String) -> Result<(), String> {
    fs::remove_file(&path).map_err(|e| format!("Failed to delete file: {}", e))
}

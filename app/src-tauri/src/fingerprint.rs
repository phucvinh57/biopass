use crate::fingerprint_ffi::FingerprintAuth;
use serde::Serialize;
use tauri::AppHandle;

#[derive(Debug, Serialize, Clone)]
pub struct FingerprintDevice {
    pub name: String,
    pub driver: String,
    pub device_id: String,
}

#[tauri::command]
pub fn fingerprint_is_available() -> Result<bool, String> {
    let auth = FingerprintAuth::new();
    Ok(auth.is_available())
}

#[tauri::command]
pub fn list_fingerprint_devices() -> Result<Vec<FingerprintDevice>, String> {
    let auth = FingerprintAuth::new();
    if auth.is_available() {
        Ok(vec![FingerprintDevice {
            name: "Default Fingerprint Reader".to_string(),
            driver: "fprintd".to_string(),
            device_id: "default".to_string(),
        }])
    } else {
        Ok(vec![])
    }
}

#[tauri::command]
pub fn list_enrolled_fingerprints(username: String) -> Result<Vec<String>, String> {
    let auth = FingerprintAuth::new();
    auth.list_enrolled_fingers(&username)
}

#[tauri::command]
pub async fn enroll_fingerprint(
    app: AppHandle,
    username: String,
    finger_name: String,
) -> Result<(), String> {
    let auth = FingerprintAuth::new();

    if !auth.is_available() {
        return Err("Fingerprint device not available".to_string());
    }

    if auth
        .list_enrolled_fingers(&username)?
        .iter()
        .any(|f| f == &finger_name)
    {
        return Err(format!("Finger {} is already enrolled", finger_name));
    }

    let success = auth.enroll(&username, &finger_name, &app)?;
    if !success {
        return Err("Failed to enroll fingerprint".to_string());
    }

    Ok(())
}

#[tauri::command]
pub async fn remove_fingerprint(
    _app: AppHandle,
    username: String,
    finger_name: String,
) -> Result<(), String> {
    let auth = FingerprintAuth::new();

    if !auth.is_available() {
        return Err("Fingerprint device not available".to_string());
    }

    let success = auth.remove_finger(&username, &finger_name)?;
    if !success {
        return Err("Failed to remove fingerprint from device".to_string());
    }

    Ok(())
}

/// Legacy, kept for compatibility.
#[tauri::command]
pub async fn add_fingerprint(
    app: AppHandle,
    _device_id: String,
    finger: String,
) -> Result<(), String> {
    let username = std::env::var("USER")
        .or_else(|_| std::env::var("USERNAME"))
        .unwrap_or_else(|_| "root".to_string());

    enroll_fingerprint(app, username, finger).await
}

/// Legacy, kept for compatibility.
#[tauri::command]
pub fn delete_fingerprint(app: AppHandle, finger: String) -> Result<(), String> {
    let username = std::env::var("USER")
        .or_else(|_| std::env::var("USERNAME"))
        .unwrap_or_else(|_| "root".to_string());

    let rt = tokio::runtime::Runtime::new().map_err(|e| e.to_string())?;
    rt.block_on(remove_fingerprint(app, username, finger))
}

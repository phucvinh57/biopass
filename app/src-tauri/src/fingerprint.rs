use crate::fingerprint_ffi::FingerprintAuth;
use tauri::AppHandle;

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

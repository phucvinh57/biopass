use crate::blocking::run_blocking;
use crate::fingerprint_ffi::FingerprintAuth;
use tauri::AppHandle;

// fprintd calls block (enroll waits for the scans, up to its own deadline),
// so they run on the blocking pool. FingerprintAuth isn't Send, so each
// closure creates its own.
const TASK: &str = "Fingerprint";

#[tauri::command]
pub async fn list_enrolled_fingerprints(username: String) -> Result<Vec<String>, String> {
    run_blocking(TASK, move || {
        FingerprintAuth::new().list_enrolled_fingers(&username)
    })
    .await
}

#[tauri::command]
pub async fn enroll_fingerprint(
    app: AppHandle,
    username: String,
    finger_name: String,
) -> Result<(), String> {
    run_blocking(TASK, move || {
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
    })
    .await
}

#[tauri::command]
pub async fn remove_fingerprint(
    _app: AppHandle,
    username: String,
    finger_name: String,
) -> Result<(), String> {
    run_blocking(TASK, move || {
        let auth = FingerprintAuth::new();

        if !auth.is_available() {
            return Err("Fingerprint device not available".to_string());
        }

        let success = auth.remove_finger(&username, &finger_name)?;
        if !success {
            return Err("Failed to remove fingerprint from device".to_string());
        }

        Ok(())
    })
    .await
}

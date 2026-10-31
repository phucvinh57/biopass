use crate::fingerprint_ffi::FingerprintAuth;
use tauri::AppHandle;

// fprintd calls block (enroll waits for the scans, up to its own deadline),
// so they run on the blocking pool, never on the main thread or an async
// worker. FingerprintAuth isn't Send, so each closure creates its own.
async fn run_blocking<T: Send + 'static>(
    f: impl FnOnce() -> Result<T, String> + Send + 'static,
) -> Result<T, String> {
    tauri::async_runtime::spawn_blocking(f)
        .await
        .map_err(|e| format!("Fingerprint task failed: {e}"))?
}

#[tauri::command]
pub async fn list_enrolled_fingerprints(username: String) -> Result<Vec<String>, String> {
    run_blocking(move || FingerprintAuth::new().list_enrolled_fingers(&username)).await
}

#[tauri::command]
pub async fn enroll_fingerprint(
    app: AppHandle,
    username: String,
    finger_name: String,
) -> Result<(), String> {
    run_blocking(move || {
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
    run_blocking(move || {
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

#[cfg(test)]
mod tests {
    use super::run_blocking;

    #[test]
    fn run_blocking_runs_off_the_calling_thread() {
        let caller = std::thread::current().id();
        let worker = tauri::async_runtime::block_on(run_blocking(|| {
            Ok::<_, String>(std::thread::current().id())
        }))
        .unwrap();
        assert_ne!(worker, caller);
    }

    #[test]
    fn run_blocking_passes_errors_through() {
        let res: Result<(), String> =
            tauri::async_runtime::block_on(run_blocking(|| Err("boom".to_string())));
        assert_eq!(res, Err("boom".to_string()));
    }

    #[test]
    fn run_blocking_reports_a_panicking_task() {
        let res: Result<(), String> =
            tauri::async_runtime::block_on(run_blocking(|| panic!("boom")));
        assert!(res.unwrap_err().starts_with("Fingerprint task failed"));
    }
}

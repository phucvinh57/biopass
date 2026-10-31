/// Runs `f` on Tauri's blocking pool so slow device or helper I/O never ties
/// up the main thread or an async worker. A panic in `f` comes back as
/// "`what` task failed: ...".
pub async fn run_blocking<T: Send + 'static>(
    what: &'static str,
    f: impl FnOnce() -> Result<T, String> + Send + 'static,
) -> Result<T, String> {
    tauri::async_runtime::spawn_blocking(f)
        .await
        .map_err(|e| format!("{what} task failed: {e}"))?
}

#[cfg(test)]
mod tests {
    use super::run_blocking;

    #[test]
    fn runs_off_the_calling_thread() {
        let caller = std::thread::current().id();
        let worker = tauri::async_runtime::block_on(run_blocking("Test", || {
            Ok::<_, String>(std::thread::current().id())
        }))
        .unwrap();
        assert_ne!(worker, caller);
    }

    #[test]
    fn passes_errors_through() {
        let res: Result<(), String> =
            tauri::async_runtime::block_on(run_blocking("Test", || Err("boom".to_string())));
        assert_eq!(res, Err("boom".to_string()));
    }

    #[test]
    fn reports_a_panicking_task_by_name() {
        let res: Result<(), String> =
            tauri::async_runtime::block_on(run_blocking("Fingerprint", || panic!("boom")));
        assert!(res.unwrap_err().starts_with("Fingerprint task failed"));
    }
}

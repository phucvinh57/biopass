pub mod bindings;
pub mod blocking;
pub mod config;
pub mod db;
pub mod face;
pub mod face_session;
pub mod fingerprint;
pub mod fingerprint_ffi;
#[cfg(test)]
mod golden;
pub mod models;
pub mod paths;
pub mod system;

use face_session::PreviewState;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(PreviewState::default())
        .setup(|app| {
            paths::restrict_private_paths(app.handle());

            #[cfg(target_os = "linux")]
            {
                // The webview needs no browser permissions: the camera preview
                // is captured natively and streamed as JPEG over IPC. Deny
                // every request (camera, mic, geolocation, notifications, ...).
                use webkit2gtk::{PermissionRequestExt, WebViewExt};
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window.with_webview(|webview| {
                        webview.inner().connect_permission_request(
                            |_view, request: &webkit2gtk::PermissionRequest| {
                                request.deny();
                                true
                            },
                        );
                    });
                }
            }

            Ok(())
        })
        .invoke_handler(bindings::builder().invoke_handler())
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

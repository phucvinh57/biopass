pub mod config;
pub mod db;
pub mod face;
pub mod face_session;
pub mod fingerprint;
pub mod fingerprint_ffi;
pub mod models;
pub mod paths;
pub mod system;

use config::{load_config, save_config, set_appearance};
use face::{delete_face, list_faces};
use face_session::{capture_face_in_session, start_face_preview, stop_face_preview, PreviewState};
use fingerprint::{enroll_fingerprint, list_enrolled_fingerprints, remove_fingerprint};
use models::{
    add_model_from_file, add_model_from_url, delete_model, ensure_default_models, list_models,
    rename_model,
};
use system::{get_current_username, list_video_devices};

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(PreviewState::default())
        .setup(|app| {
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
        .invoke_handler(tauri::generate_handler![
            load_config,
            save_config,
            set_appearance,
            get_current_username,
            start_face_preview,
            stop_face_preview,
            capture_face_in_session,
            list_faces,
            list_video_devices,
            delete_face,
            enroll_fingerprint,
            remove_fingerprint,
            list_enrolled_fingerprints,
            list_models,
            add_model_from_url,
            add_model_from_file,
            delete_model,
            rename_model,
            ensure_default_models
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

use serde::Serialize;
use std::fs;

#[derive(Debug, Serialize, Clone, specta::Type)]
pub struct VideoDeviceInfo {
    pub path: String,
    pub name: String,
    pub display_name: String,
}

#[tauri::command]
#[specta::specta]
pub fn get_current_username() -> Result<String, String> {
    username_from(|key| std::env::var(key).ok())
        .ok_or_else(|| "Could not determine current username".to_string())
}

/// The login name from the environment: USER, then LOGNAME (set by login and
/// PAM when USER isn't), then USERNAME. Empty values are skipped. This is only
/// a hint, not authoritative: fprintd's polkit check on the D-Bus caller is
/// the real guarantee (claiming a device for another name needs
/// net.reactivated.fprint.device.setusername). A uid-based lookup would need
/// getpwuid from a libc crate, which isn't a direct dependency.
fn username_from(var: impl Fn(&str) -> Option<String>) -> Option<String> {
    ["USER", "LOGNAME", "USERNAME"]
        .into_iter()
        .filter_map(var)
        .find(|name| !name.is_empty())
}

fn video_device_sort_key(path: &str) -> i32 {
    path.trim_start_matches("/dev/video")
        .parse::<i32>()
        .unwrap_or(-1)
}

#[tauri::command]
#[specta::specta]
pub fn list_video_devices() -> Result<Vec<VideoDeviceInfo>, String> {
    let mut devices = Vec::new();
    let entries = fs::read_dir("/dev").map_err(|e| format!("Failed to read /dev: {}", e))?;

    for entry in entries.flatten() {
        let file_name = entry.file_name().to_string_lossy().to_string();
        if !file_name.starts_with("video") {
            continue;
        }

        let path = format!("/dev/{}", file_name);
        let name = fs::read_to_string(format!("/sys/class/video4linux/{}/name", file_name))
            .map(|value| value.trim().to_string())
            .unwrap_or_default();
        let display_name = if name.is_empty() {
            path.clone()
        } else {
            format!("{} ({})", name, path)
        };

        devices.push(VideoDeviceInfo {
            path,
            name,
            display_name,
        });
    }

    devices.sort_by_key(|a| video_device_sort_key(&a.path));

    Ok(devices)
}

#[cfg(test)]
mod tests {
    use super::username_from;

    fn env<'a>(vars: &'a [(&'a str, &'a str)]) -> impl Fn(&str) -> Option<String> + 'a {
        move |key| {
            vars.iter()
                .find(|(k, _)| *k == key)
                .map(|(_, v)| v.to_string())
        }
    }

    #[test]
    fn prefers_user_then_logname_then_username() {
        let all = [("USER", "a"), ("LOGNAME", "b"), ("USERNAME", "c")];
        assert_eq!(username_from(env(&all)).as_deref(), Some("a"));
        assert_eq!(username_from(env(&all[1..])).as_deref(), Some("b"));
        assert_eq!(username_from(env(&all[2..])).as_deref(), Some("c"));
    }

    #[test]
    fn empty_value_falls_through() {
        let vars = [("USER", ""), ("LOGNAME", "b")];
        assert_eq!(username_from(env(&vars)).as_deref(), Some("b"));
        assert_eq!(username_from(env(&[("USER", "")])), None);
    }

    #[test]
    fn none_set_is_none() {
        assert_eq!(username_from(env(&[])), None);
    }
}

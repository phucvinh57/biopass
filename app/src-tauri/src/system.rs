use serde::Serialize;
use std::fs;

#[derive(Debug, Serialize, Clone, specta::Type)]
pub struct VideoDeviceInfo {
    /// What config.yaml saves: a stable /dev/v4l link (see stable_path), or
    /// the node when there is none.
    pub path: String,
    /// The /dev/videoN node; its number can change across boots and docking.
    pub node: String,
    /// `node` and every /dev/v4l link to it, so an older saved value (e.g.
    /// /dev/video0) still matches this device.
    pub aliases: Vec<String>,
    pub name: String,
    pub display_name: String,
}

const BY_ID_DIR: &str = "/dev/v4l/by-id";
const BY_PATH_DIR: &str = "/dev/v4l/by-path";

/// (link, canonical target) for each symlink in `dir`; empty if it is missing.
fn read_links(dir: &str) -> Vec<(String, String)> {
    let Ok(entries) = fs::read_dir(dir) else {
        return Vec::new();
    };
    entries
        .flatten()
        .filter_map(|entry| {
            let link = entry.path();
            let target = fs::canonicalize(&link).ok()?;
            Some((
                link.to_string_lossy().into_owned(),
                target.to_string_lossy().into_owned(),
            ))
        })
        .collect()
}

/// The USB device and stream index a by-path link names, without the
/// interface: `…-usb-0:8:1.2-video-index0` -> (`…-usb-0:8`, `0`). None for
/// other links (e.g. platform cameras).
fn usb_group(by_path_link: &str) -> Option<(&str, &str)> {
    let name = by_path_link.rsplit('/').next()?;
    let (head, index) = name.rsplit_once("-video-index")?;
    let (device, interface) = head.rsplit_once(':')?;
    let (config, number) = interface.split_once('.')?;
    let digits = |s: &str| !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit());
    (device.contains("-usb") && digits(config) && digits(number)).then_some((device, index))
}

/// Links in `links` that point to `node`, sorted.
fn links_to<'a>(node: &str, links: &'a [(String, String)]) -> Vec<&'a str> {
    let mut found: Vec<&str> = links
        .iter()
        .filter(|(_, target)| target == node)
        .map(|(link, _)| link.as_str())
        .collect();
    found.sort_unstable();
    found
}

/// The path to save for `node`: its by-id link, else its by-path link, else
/// `node`. udev's by-id name has no USB interface number, so two interfaces
/// of one device (an RGB + IR module with one serial) share it and the link
/// can flip between them across boots. by-id is skipped when another node of
/// the same USB device has the same stream index.
fn stable_path(node: &str, by_id: &[(String, String)], by_path: &[(String, String)]) -> String {
    let own_paths = links_to(node, by_path);
    let shared = own_paths.iter().filter_map(|l| usb_group(l)).any(|group| {
        by_path
            .iter()
            .any(|(link, target)| target != node && usb_group(link) == Some(group))
    });
    let by_id = if shared {
        None
    } else {
        links_to(node, by_id).first().copied()
    };
    by_id
        .or_else(|| own_paths.first().copied())
        .unwrap_or(node)
        .to_string()
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
    let by_id = read_links(BY_ID_DIR);
    let by_path = read_links(BY_PATH_DIR);

    for entry in entries.flatten() {
        let file_name = entry.file_name().to_string_lossy().to_string();
        if !file_name.starts_with("video") {
            continue;
        }

        let node = format!("/dev/{}", file_name);
        let name = fs::read_to_string(format!("/sys/class/video4linux/{}/name", file_name))
            .map(|value| value.trim().to_string())
            .unwrap_or_default();
        let display_name = if name.is_empty() {
            node.clone()
        } else {
            format!("{} ({})", name, node)
        };
        let mut aliases = vec![node.clone()];
        aliases.extend(links_to(&node, &by_id).into_iter().map(String::from));
        aliases.extend(links_to(&node, &by_path).into_iter().map(String::from));

        devices.push(VideoDeviceInfo {
            path: stable_path(&node, &by_id, &by_path),
            node,
            aliases,
            name,
            display_name,
        });
    }

    devices.sort_by_key(|a| video_device_sort_key(&a.node));

    Ok(devices)
}

#[cfg(test)]
mod tests {
    use super::{stable_path, usb_group, username_from, video_device_sort_key};

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

    #[test]
    fn video_device_sort_key_is_the_device_number() {
        assert_eq!(video_device_sort_key("/dev/video0"), 0);
        assert_eq!(video_device_sort_key("/dev/video2"), 2);
        assert_eq!(video_device_sort_key("/dev/video10"), 10);
    }

    #[test]
    fn video_device_without_a_number_sorts_first() {
        for path in [
            "/dev/video",
            "/dev/video-meta",
            "/dev/videoX1",
            "video2",
            "",
        ] {
            assert_eq!(video_device_sort_key(path), -1, "{path}");
        }
    }

    #[test]
    fn video_devices_sort_numerically_not_lexically() {
        let mut paths = ["/dev/video10", "/dev/video2", "/dev/videoX", "/dev/video0"];
        paths.sort_by_key(|p| video_device_sort_key(p));
        assert_eq!(
            paths,
            ["/dev/videoX", "/dev/video0", "/dev/video2", "/dev/video10"]
        );
    }

    fn links(pairs: &[(&str, &str)]) -> Vec<(String, String)> {
        pairs
            .iter()
            .map(|(link, target)| (link.to_string(), target.to_string()))
            .collect()
    }

    const ID: &str = "/dev/v4l/by-id/";
    const PATH: &str = "/dev/v4l/by-path/";

    #[test]
    fn usb_group_drops_the_interface() {
        assert_eq!(
            usb_group("/dev/v4l/by-path/pci-0000:00:14.0-usb-0:8:1.2-video-index0"),
            Some(("pci-0000:00:14.0-usb-0:8", "0"))
        );
        assert_eq!(
            usb_group("pci-0000:00:14.0-usbv2-0:8.3:1.0-video-index1"),
            Some(("pci-0000:00:14.0-usbv2-0:8.3", "1"))
        );
        for link in [
            "platform-fe801000.csi-video-index0",
            "pci-0000:00:05.0-video-index0",
            "pci-0000:00:14.0-usb-0:8:1.x-video-index0",
            "pci-0000:00:14.0-usb-0:8:1.0",
        ] {
            assert_eq!(usb_group(link), None, "{link}");
        }
    }

    #[test]
    fn unique_by_id_link_is_preferred() {
        let by_id = links(&[(
            "/dev/v4l/by-id/usb-Dock_Cam_123-video-index0",
            "/dev/video4",
        )]);
        let by_path = links(&[(
            "/dev/v4l/by-path/pci-0000:3a:00.0-usb-0:2:1.0-video-index0",
            "/dev/video4",
        )]);
        assert_eq!(
            stable_path("/dev/video4", &by_id, &by_path),
            "/dev/v4l/by-id/usb-Dock_Cam_123-video-index0"
        );
    }

    // An RGB + IR module (one USB device, interfaces 1.0 and 1.2, one
    // serial): udev keeps one by-id link per index, here pointing at the IR
    // node. Every node falls back to its by-path link.
    #[test]
    fn by_id_shared_by_two_interfaces_falls_back_to_by_path() {
        let by_id = links(&[
            (
                &format!("{ID}usb-Generic_Integrated_Camera_200901010001-video-index0"),
                "/dev/video2",
            ),
            (
                &format!("{ID}usb-Generic_Integrated_Camera_200901010001-video-index1"),
                "/dev/video1",
            ),
        ]);
        let mut by_path = Vec::new();
        for (usb, iface, index, node) in [
            ("usb", "1.0", 0, 0),
            ("usb", "1.0", 1, 1),
            ("usb", "1.2", 0, 2),
            ("usb", "1.2", 1, 3),
            ("usbv2", "1.0", 0, 0),
            ("usbv2", "1.0", 1, 1),
            ("usbv2", "1.2", 0, 2),
            ("usbv2", "1.2", 1, 3),
        ] {
            by_path.push((
                format!("{PATH}pci-0000:00:14.0-{usb}-0:8:{iface}-video-index{index}"),
                format!("/dev/video{node}"),
            ));
        }
        for (node, iface, index) in [(0, "1.0", 0), (1, "1.0", 1), (2, "1.2", 0), (3, "1.2", 1)] {
            assert_eq!(
                stable_path(&format!("/dev/video{node}"), &by_id, &by_path),
                format!("{PATH}pci-0000:00:14.0-usb-0:8:{iface}-video-index{index}"),
            );
        }
    }

    #[test]
    fn no_links_keeps_the_node() {
        assert_eq!(stable_path("/dev/video7", &[], &[]), "/dev/video7");
        // Links to other nodes don't count.
        let by_id = links(&[("/dev/v4l/by-id/usb-X-video-index0", "/dev/video0")]);
        assert_eq!(stable_path("/dev/video7", &by_id, &[]), "/dev/video7");
    }

    #[test]
    fn non_usb_by_path_link_is_used() {
        let by_path = links(&[(
            "/dev/v4l/by-path/platform-fe801000.csi-video-index0",
            "/dev/video0",
        )]);
        assert_eq!(
            stable_path("/dev/video0", &[], &by_path),
            "/dev/v4l/by-path/platform-fe801000.csi-video-index0"
        );
    }
}

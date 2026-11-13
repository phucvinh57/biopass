use serde::Serialize;
use std::fs;
use std::path::Path;

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
    /// What the UI shows: the USB interface name (e.g. "Integrated IR
    /// Camera"), else `name`.
    pub label: String,
    /// Built-in/external, maker and model, USB port and ID, joined with
    /// " · "; empty for non-USB cameras.
    pub details: String,
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

const CLASS_DIR: &str = "/sys/class/video4linux";

/// A trimmed, non-empty sysfs attribute.
fn read_attr(dir: &Path, name: &str) -> Option<String> {
    let value = fs::read_to_string(dir.join(name)).ok()?;
    let value = value.trim();
    (!value.is_empty()).then(|| value.to_string())
}

/// Capture capabilities distinguish RGB/IR streams from metadata; the BRIO
/// uses indices 0 and 2 for capture, so index 0 alone is not enough.
fn is_capture_node(class_dir: &Path, udev_data: &Path) -> bool {
    if let Some(caps) = udev_property(udev_data, class_dir, "ID_V4L_CAPABILITIES") {
        return caps.split(':').any(|cap| cap == "capture");
    }
    // shortcut: without udev data, retain index-0 filtering; use QUERYCAP to support secondary nodes there.
    read_attr(class_dir, "index")
        .and_then(|index| index.parse::<u32>().ok())
        .is_none_or(|index| index == 0)
}

const UDEV_DATA_DIR: &str = "/run/udev/data";

/// A udev property of a sysfs device with a `dev` attribute (major:minor).
/// Used for video capture capabilities and USB vendor/model descriptions.
fn udev_property(udev_data: &Path, usb: &Path, key: &str) -> Option<String> {
    let dev = read_attr(usb, "dev")?;
    let data = fs::read_to_string(udev_data.join(format!("c{dev}"))).ok()?;
    data.lines()
        .filter_map(|line| line.strip_prefix("E:")?.split_once('='))
        .find(|(k, v)| *k == key && !v.trim().is_empty())
        .map(|(_, v)| v.trim().to_string())
}

/// (label, details) for the node at `class_dir`. `device` links to the USB
/// interface, whose parent is the USB device.
fn describe(class_dir: &Path, name: &str) -> (String, String) {
    describe_with(class_dir, name, Path::new(UDEV_DATA_DIR))
}

fn describe_with(class_dir: &Path, name: &str, udev_data: &Path) -> (String, String) {
    let interface = fs::canonicalize(class_dir.join("device")).ok();
    let label = interface
        .as_deref()
        .and_then(|dir| read_attr(dir, "interface"))
        .or_else(|| (!name.is_empty()).then(|| name.to_string()))
        .unwrap_or_else(|| "Camera".to_string());

    let usb = interface
        .as_deref()
        .and_then(Path::parent)
        .filter(|dir| dir.join("idVendor").exists());
    let Some(usb) = usb else {
        return (label, String::new());
    };
    let attr = |name| read_attr(usb, name);
    let kind = match attr("removable").as_deref() {
        Some("fixed") => Some("Built-in".to_string()),
        Some("removable") => Some("External".to_string()),
        _ => None,
    };
    let from_db = |key| udev_property(udev_data, usb, key);
    let address = attr("busnum").zip(attr("devnum")).and_then(|(bus, dev)| {
        Some(format!(
            "{:03}:{:03}",
            bus.parse::<u32>().ok()?,
            dev.parse::<u32>().ok()?
        ))
    });
    let vendor = from_db("ID_VENDOR_FROM_DATABASE").or_else(|| attr("manufacturer"));
    let product = from_db("ID_MODEL_FROM_DATABASE").or_else(|| attr("product"));
    let model = [address, vendor, product]
        .into_iter()
        .flatten()
        .collect::<Vec<_>>()
        .join(" ");
    let port = usb
        .file_name()
        .map(|port| format!("USB port {}", port.to_string_lossy()));
    let id = attr("idVendor")
        .zip(attr("idProduct"))
        .map(|(v, p)| format!("{v}:{p}"));
    let details = [kind, (!model.is_empty()).then_some(model), port, id]
        .into_iter()
        .flatten()
        .collect::<Vec<_>>()
        .join(" · ");
    (label, details)
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

        let class_dir = Path::new(CLASS_DIR).join(&file_name);
        if !is_capture_node(&class_dir, Path::new(UDEV_DATA_DIR)) {
            continue;
        }

        let node = format!("/dev/{}", file_name);
        let name = read_attr(&class_dir, "name").unwrap_or_default();
        let (label, details) = describe(&class_dir, &name);
        let mut aliases = vec![node.clone()];
        aliases.extend(links_to(&node, &by_id).into_iter().map(String::from));
        aliases.extend(links_to(&node, &by_path).into_iter().map(String::from));

        devices.push(VideoDeviceInfo {
            path: stable_path(&node, &by_id, &by_path),
            node,
            aliases,
            name,
            label,
            details,
        });
    }

    devices.sort_by_key(|a| video_device_sort_key(&a.node));

    Ok(devices)
}

#[cfg(test)]
mod tests {
    use super::{
        describe, describe_with, is_capture_node, stable_path, usb_group, username_from,
        video_device_sort_key,
    };
    use std::fs;
    use std::os::unix::fs::symlink;
    use std::path::Path;

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

    fn write(dir: &Path, attrs: &[(&str, &str)]) {
        fs::create_dir_all(dir).unwrap();
        for (name, value) in attrs {
            fs::write(dir.join(name), format!("{value}\n")).unwrap();
        }
    }

    /// A sysfs-like tree: class/videoN/device -> usb/1-8/1-8:1.2.
    fn usb_node(root: &Path, removable: &str) -> std::path::PathBuf {
        let usb = root.join("usb/1-8");
        write(
            &usb,
            &[
                ("idVendor", "5986"),
                ("idProduct", "118c"),
                ("manufacturer", "Generic"),
                ("product", "Integrated Camera"),
                ("removable", removable),
            ],
        );
        let interface = usb.join("1-8:1.2");
        write(&interface, &[("interface", "Integrated IR Camera")]);
        let class = root.join("class/video2");
        write(&class, &[("index", "0")]);
        symlink(&interface, class.join("device")).unwrap();
        class
    }

    #[test]
    fn describes_a_built_in_usb_camera() {
        let root = tempfile::tempdir().unwrap();
        let class = usb_node(root.path(), "fixed");
        assert_eq!(
            describe(&class, "Integrated Camera: Integrated I"),
            (
                "Integrated IR Camera".to_string(),
                "Built-in · Generic Integrated Camera · USB port 1-8 · 5986:118c".to_string()
            )
        );
    }

    #[test]
    fn prefers_the_usb_id_database_and_shows_the_bus_address() {
        let root = tempfile::tempdir().unwrap();
        let class = usb_node(root.path(), "fixed");
        let usb = root.path().join("usb/1-8");
        write(&usb, &[("dev", "189:2"), ("busnum", "1"), ("devnum", "3")]);
        let udev = root.path().join("udev");
        write(
            &udev,
            &[(
                "c189:2",
                "I:123\nE:ID_VENDOR=Generic\nE:ID_VENDOR_FROM_DATABASE=Bison Electronics Inc.",
            )],
        );
        assert_eq!(
            describe_with(&class, "", &udev).1,
            "Built-in · 001:003 Bison Electronics Inc. Integrated Camera · USB port 1-8 · 5986:118c"
        );
    }

    #[test]
    fn describes_an_external_usb_camera() {
        let root = tempfile::tempdir().unwrap();
        let class = usb_node(root.path(), "removable");
        assert!(describe(&class, "")
            .1
            .starts_with("External · Generic Integrated Camera"));
    }

    #[test]
    fn non_usb_camera_uses_its_name() {
        let root = tempfile::tempdir().unwrap();
        let interface = root.path().join("platform/csi");
        fs::create_dir_all(&interface).unwrap();
        let class = root.path().join("class/video0");
        fs::create_dir_all(&class).unwrap();
        symlink(&interface, class.join("device")).unwrap();
        assert_eq!(
            describe(&class, "unicam-image"),
            ("unicam-image".to_string(), String::new())
        );
        assert_eq!(describe(&class, ""), ("Camera".to_string(), String::new()));
    }

    #[test]
    fn brio_rgb_and_ir_capture_nodes_are_kept_but_metadata_is_skipped() {
        let root = tempfile::tempdir().unwrap();
        let udev = root.path().join("udev");
        for (index, caps, capture) in [
            (0, ":capture:streaming:", true),
            (1, ":meta_capture:streaming:", false),
            (2, ":capture:streaming:", true),
            (3, ":meta_capture:streaming:", false),
        ] {
            let class = root.path().join(format!("video{index}"));
            let dev = format!("81:{index}");
            write(&class, &[("dev", &dev), ("index", &index.to_string())]);
            write(
                &udev,
                &[(&format!("c{dev}"), &format!("E:ID_V4L_CAPABILITIES={caps}"))],
            );
            assert_eq!(is_capture_node(&class, &udev), capture, "index {index}");
        }
    }

    #[test]
    fn capture_capabilities_override_the_index() {
        let root = tempfile::tempdir().unwrap();
        let udev = root.path().join("udev");
        let class = root.path().join("video0");
        write(&class, &[("dev", "81:0"), ("index", "0")]);
        for caps in [":", ":video_output:", ":vbi_capture:", ":meta_capture:"] {
            write(
                &udev,
                &[("c81:0", &format!("E:ID_V4L_CAPABILITIES={caps}"))],
            );
            assert!(!is_capture_node(&class, &udev), "{caps}");
        }
        write(&class, &[("index", "1")]);
        write(&udev, &[("c81:0", "E:ID_V4L_CAPABILITIES=:capture:")]);
        assert!(is_capture_node(&class, &udev));
    }

    #[test]
    fn missing_udev_capabilities_keep_the_first_node_fallback() {
        let root = tempfile::tempdir().unwrap();
        for (index, first) in [(None, true), (Some("0"), true), (Some("1"), false)] {
            let class = root.path().join(format!("video-{first}-{index:?}"));
            fs::create_dir_all(&class).unwrap();
            if let Some(index) = index {
                write(&class, &[("index", index)]);
            }
            assert_eq!(
                is_capture_node(&class, &root.path().join("udev")),
                first,
                "{index:?}"
            );
        }
    }
}

use std::fs;
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use tauri::AppHandle;

use crate::paths::get_faces_dir;

#[tauri::command]
pub fn list_faces(app: AppHandle) -> Result<Vec<String>, String> {
    let faces_dir = get_faces_dir(&app)?;

    if !faces_dir.exists() {
        return Ok(vec![]);
    }
    face_files_in(&faces_dir)
}

fn face_files_in(faces_dir: &Path) -> Result<Vec<String>, String> {
    let entries =
        fs::read_dir(faces_dir).map_err(|e| format!("Failed to read faces directory: {}", e))?;

    let mut files: Vec<String> = entries
        .filter_map(|e| e.ok())
        .filter(|e| is_face_file(&e.path()))
        .map(|e| e.path().to_string_lossy().to_string())
        .collect();

    files.sort();
    Ok(files)
}

#[tauri::command]
pub fn delete_face(app: AppHandle, path: String) -> Result<(), String> {
    let faces_dir = get_faces_dir(&app)?;
    let target = resolve_face_file(&faces_dir, Path::new(&path))?;
    fs::remove_file(&target).map_err(|e| format!("Failed to delete file: {}", e))
}

/// Returns the path to delete only if `path` names a face image (see
/// `is_face_file`) directly inside `faces_dir` (the same set `list_faces` returns).
fn resolve_face_file(faces_dir: &Path, path: &Path) -> Result<PathBuf, String> {
    let invalid = || format!("Not a face image: {}", path.display());

    let name = path.file_name().ok_or_else(invalid)?;
    let parent = path.parent().ok_or_else(invalid)?;
    let parent = fs::canonicalize(parent).map_err(|_| invalid())?;
    let faces_dir = fs::canonicalize(faces_dir).map_err(|_| invalid())?;
    if parent != faces_dir {
        return Err(invalid());
    }

    let target = faces_dir.join(name);
    if !is_face_file(&target) {
        return Err(invalid());
    }
    Ok(target)
}

/// A face image is a regular (non-symlink) file named as `has_image_extension`
/// accepts.
fn is_face_file(path: &Path) -> bool {
    path.file_name()
        .is_some_and(|name| has_image_extension(name.as_bytes()))
        && fs::symlink_metadata(path).is_ok_and(|m| m.file_type().is_file())
}

/// Same rule as auth's biopass::hasImageExtension (core/auth_config.h), so
/// the app lists exactly the faces auth matches against: .jpg, .jpeg or .png
/// (ASCII case-insensitive) after a non-empty stem.
fn has_image_extension(name: &[u8]) -> bool {
    let Some(dot) = name.iter().rposition(|&b| b == b'.') else {
        return false;
    };
    if dot == 0 {
        return false;
    }
    let ext = &name[dot + 1..];
    [&b"jpg"[..], b"jpeg", b"png"]
        .iter()
        .any(|want| ext.eq_ignore_ascii_case(want))
}

#[cfg(test)]
mod tests {
    use super::*;

    struct TempDir(PathBuf);

    impl TempDir {
        fn new(name: &str) -> Self {
            let dir = std::env::temp_dir().join(format!(
                "biopass-face-test-{}-{}",
                name,
                std::process::id()
            ));
            let _ = fs::remove_dir_all(&dir);
            fs::create_dir_all(dir.join("faces")).unwrap();
            TempDir(dir)
        }

        fn faces(&self) -> PathBuf {
            self.0.join("faces")
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn accepts_image_directly_in_faces_dir() {
        let tmp = TempDir::new("ok");
        let img = tmp.faces().join("a.jpg");
        fs::write(&img, b"x").unwrap();
        let resolved = resolve_face_file(&tmp.faces(), &img).unwrap();
        assert_eq!(resolved, fs::canonicalize(&img).unwrap());
    }

    #[test]
    fn rejects_file_outside_faces_dir() {
        let tmp = TempDir::new("outside");
        let other = tmp.0.join("secret.jpg");
        fs::write(&other, b"x").unwrap();
        assert!(resolve_face_file(&tmp.faces(), &other).is_err());
    }

    #[test]
    fn rejects_dot_dot_escape() {
        let tmp = TempDir::new("dotdot");
        fs::write(tmp.0.join("secret.jpg"), b"x").unwrap();
        let escaped = tmp.faces().join("..").join("secret.jpg");
        assert!(resolve_face_file(&tmp.faces(), &escaped).is_err());
    }

    #[test]
    fn rejects_subdirectory() {
        let tmp = TempDir::new("subdir");
        fs::create_dir(tmp.faces().join("sub")).unwrap();
        let nested = tmp.faces().join("sub").join("a.jpg");
        fs::write(&nested, b"x").unwrap();
        assert!(resolve_face_file(&tmp.faces(), &nested).is_err());
    }

    #[test]
    fn rejects_symlink_in_faces_dir() {
        let tmp = TempDir::new("symlink");
        let target = tmp.0.join("secret.jpg");
        fs::write(&target, b"x").unwrap();
        let link = tmp.faces().join("link.jpg");
        std::os::unix::fs::symlink(&target, &link).unwrap();
        assert!(resolve_face_file(&tmp.faces(), &link).is_err());
    }

    #[test]
    fn rejects_non_image_extension() {
        let tmp = TempDir::new("ext");
        let txt = tmp.faces().join("a.txt");
        fs::write(&txt, b"x").unwrap();
        assert!(resolve_face_file(&tmp.faces(), &txt).is_err());
    }

    #[test]
    fn lists_only_regular_image_files() {
        let tmp = TempDir::new("list");
        fs::write(tmp.faces().join("a.jpg"), b"x").unwrap();
        fs::write(tmp.faces().join("b.txt"), b"x").unwrap();
        fs::create_dir(tmp.faces().join("d.jpg")).unwrap();
        let outside = tmp.0.join("secret.png");
        fs::write(&outside, b"x").unwrap();
        std::os::unix::fs::symlink(&outside, tmp.faces().join("link.png")).unwrap();

        let listed = face_files_in(&tmp.faces()).unwrap();
        let expected = tmp.faces().join("a.jpg").to_string_lossy().to_string();
        assert_eq!(listed, vec![expected]);
    }

    /// Same table as auth's face_image_name test, so app and auth list the
    /// same faces.
    #[test]
    fn image_names_match_auth_table() {
        let table = include_str!("../../../auth/test/face_image_name/cases.txt");
        let mut cases = 0;
        for line in table.lines() {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let (name, want) = line.split_once(' ').expect("<name> <1|0>");
            let want = match want {
                "1" => true,
                "0" => false,
                _ => panic!("bad line: {line}"),
            };
            assert_eq!(has_image_extension(name.as_bytes()), want, "{name}");
            cases += 1;
        }
        assert!(cases > 0);
    }

    #[test]
    fn rejects_directory_and_missing_file() {
        let tmp = TempDir::new("dir");
        fs::create_dir(tmp.faces().join("d.jpg")).unwrap();
        assert!(resolve_face_file(&tmp.faces(), &tmp.faces().join("d.jpg")).is_err());
        assert!(resolve_face_file(&tmp.faces(), &tmp.faces().join("missing.jpg")).is_err());
    }
}

use std::fs;
use std::io;
use std::os::unix::fs::{DirBuilderExt, OpenOptionsExt, PermissionsExt};
use std::path::{Path, PathBuf};
use tauri::{AppHandle, Manager};

pub const CONFIG_FILE: &str = "config.yaml";
pub const DB_FILE: &str = "biopass.db";

pub fn get_config_dir(app: &AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_config_dir()
        .map_err(|e| format!("Failed to get config dir: {}", e))
}

pub fn get_config_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(get_config_dir(app)?.join(CONFIG_FILE))
}

pub fn get_db_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(get_config_dir(app)?.join(DB_FILE))
}

pub fn get_data_dir(app: &AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_data_dir()
        .map_err(|e| format!("Failed to get data dir: {}", e))
}

pub fn get_faces_dir(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(get_data_dir(app)?.join("faces"))
}

/// Creates `dir` and any missing parents as 0700 (like auth's
/// setupDataDirs), then drops group/other bits from `dir` if it already
/// existed with them. See `restrict_mode`.
pub fn ensure_private_dir(dir: &Path) -> io::Result<()> {
    fs::DirBuilder::new()
        .recursive(true)
        .mode(0o700)
        .create(dir)?;
    restrict_mode(dir, 0o700)
}

/// Creates an empty `file` as 0600 if it is missing, else drops group/other
/// bits from it. See `restrict_mode`.
pub fn ensure_private_file(file: &Path) -> io::Result<()> {
    // create_new is O_CREAT|O_EXCL: it never follows a symlink.
    match fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(file)
    {
        Ok(_) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => restrict_mode(file, 0o600),
        Err(e) => Err(e),
    }
}

/// Tightens what a looser older version left behind (old releases made the
/// data tree 0755): the data dir and its faces/, debugs/ (face captures),
/// logs/ and models/, the config dir and the DB. Best effort: logs and goes
/// on.
pub fn restrict_private_paths(app: &AppHandle) {
    let mut paths = Vec::new();
    if let Ok(data) = get_data_dir(app) {
        for sub in ["faces", "debugs", "logs", "models"] {
            paths.push((data.join(sub), 0o700));
        }
        paths.push((data, 0o700));
    }
    if let Ok(config) = get_config_dir(app) {
        paths.push((config.join(DB_FILE), 0o600));
        paths.push((config, 0o700));
    }
    for (path, max) in paths {
        match restrict_mode(&path, max) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => eprintln!("Failed to restrict {}: {e}", path.display()),
        }
    }
}

/// Clears the mode bits of `path` outside `max`. A symlink is left alone
/// (not followed), and so is a path we don't own (e.g. root-owned from an
/// old helper): only its owner may chmod it, so that is logged, not failed.
fn restrict_mode(path: &Path, max: u32) -> io::Result<()> {
    let meta = fs::symlink_metadata(path)?;
    if meta.file_type().is_symlink() {
        return Ok(());
    }
    let mode = meta.permissions().mode() & 0o7777;
    if mode & !max == 0 {
        return Ok(());
    }
    match fs::set_permissions(path, fs::Permissions::from_mode(mode & max)) {
        Err(e) if e.kind() == io::ErrorKind::PermissionDenied => {
            eprintln!(
                "Leaving {} as {:o}: not ours to chmod ({e})",
                path.display(),
                mode
            );
            Ok(())
        }
        other => other,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_dir(name: &str) -> tempfile::TempDir {
        tempfile::Builder::new()
            .prefix(&format!("biopass-paths-{name}-"))
            .tempdir()
            .unwrap()
    }

    fn mode(path: &Path) -> u32 {
        fs::symlink_metadata(path).unwrap().permissions().mode() & 0o7777
    }

    fn chmod(path: &Path, mode: u32) {
        fs::set_permissions(path, fs::Permissions::from_mode(mode)).unwrap();
    }

    #[test]
    fn private_dir_is_created_0700_with_parents() {
        let tmp = temp_dir("mkdir");
        let faces = tmp.path().join("data").join("faces");
        ensure_private_dir(&faces).unwrap();
        // The umask may clear owner bits too; only group/other must be clear.
        assert_eq!(mode(&faces) & 0o077, 0);
        assert_eq!(mode(&tmp.path().join("data")) & 0o077, 0);
    }

    #[test]
    fn existing_private_dir_loses_group_and_other_bits() {
        let tmp = temp_dir("dir-tighten");
        let faces = tmp.path().join("faces");
        fs::create_dir(&faces).unwrap();
        chmod(&faces, 0o755);
        ensure_private_dir(&faces).unwrap();
        assert_eq!(mode(&faces), 0o700);

        // Only removes bits.
        chmod(&faces, 0o500);
        ensure_private_dir(&faces).unwrap();
        assert_eq!(mode(&faces), 0o500);
        chmod(&faces, 0o700);
    }

    #[test]
    fn private_dir_does_not_chmod_through_symlink() {
        let tmp = temp_dir("dir-link");
        let target = tmp.path().join("elsewhere");
        fs::create_dir(&target).unwrap();
        chmod(&target, 0o755);
        let link = tmp.path().join("faces");
        std::os::unix::fs::symlink(&target, &link).unwrap();
        ensure_private_dir(&link).unwrap();
        assert_eq!(mode(&target), 0o755);
    }

    #[test]
    fn private_file_is_created_0600_and_empty() {
        let tmp = temp_dir("file-new");
        let db = tmp.path().join("biopass.db");
        ensure_private_file(&db).unwrap();
        assert_eq!(mode(&db) & 0o077, 0);
        assert_eq!(fs::metadata(&db).unwrap().len(), 0);
    }

    #[test]
    fn existing_private_file_is_tightened_and_kept() {
        let tmp = temp_dir("file-tighten");
        let db = tmp.path().join("biopass.db");
        fs::write(&db, b"data").unwrap();
        chmod(&db, 0o644);
        ensure_private_file(&db).unwrap();
        assert_eq!(mode(&db), 0o600);
        assert_eq!(fs::read(&db).unwrap(), b"data");
    }

    #[test]
    fn private_file_does_not_chmod_through_symlink() {
        let tmp = temp_dir("file-link");
        let target = tmp.path().join("elsewhere.db");
        fs::write(&target, b"x").unwrap();
        chmod(&target, 0o644);
        let link = tmp.path().join("biopass.db");
        std::os::unix::fs::symlink(&target, &link).unwrap();
        ensure_private_file(&link).unwrap();
        assert_eq!(mode(&target), 0o644);
    }

    #[test]
    fn restrict_mode_reports_missing_path() {
        let tmp = temp_dir("missing");
        let err = restrict_mode(&tmp.path().join("nope"), 0o700).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::NotFound);
    }
}

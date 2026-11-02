//! Saving the login password so a face or fingerprint login can unlock the
//! keyring (docs/Keyring.md). The app never stores it: it hands it to
//! `pkexec biopass-keyring seal`, which checks it against /etc/shadow and
//! seals it with the TPM under /var/lib/biopass/keyring/<uid>.sealed.

use crate::blocking::run_blocking;
use serde::Serialize;
use std::io::{ErrorKind, Write};
use std::os::unix::fs::MetadataExt;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::UNIX_EPOCH;

const TASK: &str = "Keyring";
const TPM_DEVICE: &str = "/dev/tpmrm0";
const STORE_DIR: &str = "/var/lib/biopass/keyring";
const PKEXEC: &str = "/usr/bin/pkexec";
const KEYRING_TOOL: &str = "/usr/libexec/biopass/biopass-keyring";
/// The TPM seals at most 128 bytes in one object.
pub const MAX_PASSWORD_BYTES: usize = 128;

#[derive(Debug, Clone, Serialize, specta::Type)]
pub struct KeyringStatus {
    /// A TPM resource manager device exists.
    tpm_available: bool,
    /// A sealed password is stored for this user.
    saved: bool,
    /// When it was saved, in ms since the Unix epoch.
    #[specta(type = Option<specta_typescript::Number>)]
    saved_at_ms: Option<u64>,
}

/// The current process's uid: /proc/self belongs to it.
fn current_uid() -> Result<u32, String> {
    std::fs::metadata("/proc/self")
        .map(|m| m.uid())
        .map_err(|e| format!("Could not determine the current user: {e}"))
}

fn sealed_path(uid: u32) -> PathBuf {
    PathBuf::from(STORE_DIR).join(format!("{uid}.sealed"))
}

#[tauri::command]
#[specta::specta]
pub fn get_keyring_status() -> Result<KeyringStatus, String> {
    let uid = current_uid()?;
    // The dir is 0711: the file can be stat'ed by name, not read.
    let (saved, saved_at_ms) = match std::fs::metadata(sealed_path(uid)) {
        Ok(meta) => {
            let at = meta
                .modified()
                .ok()
                .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
                .map(|d| d.as_millis() as u64);
            (true, at)
        }
        Err(_) => (false, None),
    };
    Ok(KeyringStatus {
        tpm_available: std::path::Path::new(TPM_DEVICE).exists(),
        saved,
        saved_at_ms,
    })
}

/// Validates what the user typed, as biopass-keyring will.
fn check_password(password: &str) -> Result<(), String> {
    if password.is_empty() {
        return Err("Enter your password.".to_string());
    }
    if password.len() > MAX_PASSWORD_BYTES {
        return Err(format!(
            "Passwords longer than {MAX_PASSWORD_BYTES} bytes can't be saved."
        ));
    }
    if password.contains('\0') {
        return Err("The password contains a NUL character.".to_string());
    }
    Ok(())
}

/// The message for biopass-keyring's exit code (auth/keyring/keyring_exit.h)
/// or pkexec's own (126, 127). None on success.
fn exit_message(code: Option<i32>) -> Option<String> {
    let msg = match code {
        Some(0) => return None,
        Some(126) => "Authentication was cancelled.",
        Some(127) => "Not authorized. Biopass needs a polkit authentication agent to save the password.",
        Some(2) => "Internal error: biopass-keyring was called with bad arguments.",
        Some(3) => "biopass-keyring must be started through pkexec.",
        Some(4) => "Wrong password.",
        Some(5) => "Your account has no local password (LDAP and SSSD accounts are not supported).",
        Some(6) => "The password must be 1 to 128 bytes.",
        Some(7) => "No TPM was found on this computer.",
        Some(8) => "The TPM owner hierarchy has a password, so Biopass can't use the TPM.",
        Some(9) => "This TPM has no SHA-256 PCR bank. Turn off \"Bind to Secure Boot state\" and try again.",
        Some(10) => "The TPM could not seal the password.",
        Some(11) => "Could not write to /var/lib/biopass/keyring.",
        Some(other) => return Some(format!("biopass-keyring failed (exit code {other}).")),
        None => "biopass-keyring was killed.",
    };
    Some(msg.to_string())
}

/// Overwrites `bytes` in a way the optimizer can't drop.
fn wipe(bytes: &mut [u8]) {
    for b in bytes.iter_mut() {
        // SAFETY: `b` is a valid, exclusive reference.
        unsafe { std::ptr::write_volatile(b, 0) };
    }
    std::sync::atomic::compiler_fence(std::sync::atomic::Ordering::SeqCst);
}

/// Runs `pkexec biopass-keyring <args>`, writing `stdin_data` (if any) to its
/// stdin, and maps the exit code.
fn run_tool(args: &[&str], stdin_data: Option<&[u8]>) -> Result<(), String> {
    let mut child = Command::new(PKEXEC)
        .arg(KEYRING_TOOL)
        .args(args)
        .stdin(if stdin_data.is_some() {
            Stdio::piped()
        } else {
            Stdio::null()
        })
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|e| match e.kind() {
            ErrorKind::NotFound => "pkexec is not installed.".to_string(),
            _ => format!("Failed to start pkexec: {e}"),
        })?;
    if let Some(data) = stdin_data {
        // Dropping stdin closes it: the tool reads to EOF.
        if let Some(mut stdin) = child.stdin.take() {
            // A write error means the tool exited early; its code says why.
            let _ = stdin.write_all(data);
        }
    }
    let status = child
        .wait()
        .map_err(|e| format!("Failed to wait for pkexec: {e}"))?;
    match exit_message(status.code()) {
        None => Ok(()),
        Some(msg) => Err(msg),
    }
}

#[tauri::command]
#[specta::specta]
pub async fn save_keyring_password(password: String, bind_pcr7: bool) -> Result<(), String> {
    let mut bytes = password.into_bytes();
    let checked = std::str::from_utf8(&bytes)
        .map_err(|e| e.to_string())
        .and_then(check_password);
    if let Err(e) = checked {
        wipe(&mut bytes);
        return Err(e);
    }
    run_blocking(TASK, move || {
        let args: &[&str] = if bind_pcr7 {
            &["seal", "--pcr7"]
        } else {
            &["seal"]
        };
        let result = run_tool(args, Some(&bytes));
        wipe(&mut bytes);
        result
    })
    .await
}

#[tauri::command]
#[specta::specta]
pub async fn forget_keyring_password() -> Result<(), String> {
    run_blocking(TASK, || run_tool(&["forget"], None)).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn success_has_no_message() {
        assert_eq!(exit_message(Some(0)), None);
    }

    #[test]
    fn every_known_code_has_its_own_message() {
        let codes = [126, 127, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
        let messages: Vec<String> = codes
            .iter()
            .map(|c| exit_message(Some(*c)).unwrap())
            .collect();
        for (i, m) in messages.iter().enumerate() {
            assert!(!m.contains("exit code"), "code {} is unmapped", codes[i]);
            assert_eq!(messages.iter().filter(|o| *o == m).count(), 1, "{m}");
        }
        assert_eq!(exit_message(Some(4)).unwrap(), "Wrong password.");
    }

    #[test]
    fn unknown_codes_and_signals_are_errors() {
        assert!(exit_message(Some(42)).unwrap().contains("exit code 42"));
        assert!(exit_message(None).is_some());
    }

    #[test]
    fn password_limits() {
        assert!(check_password("").is_err());
        assert!(check_password("a").is_ok());
        assert!(check_password(&"a".repeat(MAX_PASSWORD_BYTES)).is_ok());
        assert!(check_password(&"a".repeat(MAX_PASSWORD_BYTES + 1)).is_err());
        // Bytes, not characters: 64 two-byte characters fit, 65 don't.
        assert!(check_password(&"ä".repeat(64)).is_ok());
        assert!(check_password(&"ä".repeat(65)).is_err());
        assert!(check_password("a\0b").is_err());
    }

    #[test]
    fn wipe_zeroes() {
        let mut b = b"secret".to_vec();
        wipe(&mut b);
        assert!(b.iter().all(|x| *x == 0));
    }

    #[test]
    fn sealed_file_is_named_by_uid() {
        assert_eq!(
            sealed_path(1000),
            PathBuf::from("/var/lib/biopass/keyring/1000.sealed")
        );
    }
}

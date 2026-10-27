use std::io::{BufRead, BufReader, Read, Write};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use base64::{engine::general_purpose, Engine as _};
use tauri::{AppHandle, Emitter};

use crate::config::{load_config, BiopassConfig};
use crate::db;
use crate::paths::get_faces_dir;

const PREVIEW_EVENT: &str = "face-preview-frame";
const FRAME_INTERVAL_MS: u64 = 33; // ~30fps ceiling

struct ChildIO {
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
}

struct PreviewSession {
    child: Child,
    io: Arc<Mutex<ChildIO>>,
    stop: Arc<AtomicBool>,
    thread: Option<JoinHandle<()>>,
}

static SESSION: Mutex<Option<PreviewSession>> = Mutex::new(None);

fn helper_path() -> String {
    if std::path::Path::new("/usr/bin/biopass-helper").exists() {
        "/usr/bin/biopass-helper".into()
    } else if std::path::Path::new("../../auth/build/pam/biopass-helper").exists() {
        "../../auth/build/pam/biopass-helper".into()
    } else {
        "biopass-helper".into()
    }
}

fn read_line_trim(reader: &mut BufReader<ChildStdout>) -> std::io::Result<String> {
    let mut line = String::new();
    let n = reader.read_line(&mut line)?;
    if n == 0 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::UnexpectedEof,
            "child closed stdout",
        ));
    }
    if line.ends_with('\n') {
        line.pop();
    }
    if line.ends_with('\r') {
        line.pop();
    }
    Ok(line)
}

#[tauri::command]
pub fn start_face_preview(app: AppHandle, camera: Option<String>) -> Result<(), String> {
    let mut guard = SESSION.lock().map_err(|e| e.to_string())?;
    if guard.is_some() {
        return Ok(());
    }

    let config: BiopassConfig = load_config(app.clone())?;
    let conn = db::open(&app)?;
    let model_id = &config.methods.face.detection.model_id;
    let detect_model = db::resolve_model_path(&conn, model_id)?
        .ok_or_else(|| format!("Detection model '{}' not found in registry", model_id))?;

    let mut cmd = Command::new(helper_path());
    cmd.arg("preview-session")
        .arg("--model")
        .arg(&detect_model)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null());
    if let Some(cam) = camera.filter(|c| !c.is_empty()) {
        cmd.arg("--camera").arg(cam);
    }

    let mut child = cmd
        .spawn()
        .map_err(|e| format!("Failed to spawn helper: {e}"))?;
    let stdin = child.stdin.take().ok_or("missing stdin")?;
    let stdout = child.stdout.take().ok_or("missing stdout")?;
    let mut reader = BufReader::new(stdout);

    let ready = read_line_trim(&mut reader).map_err(|e| format!("Helper did not respond: {e}"))?;
    if ready != "READY" {
        let _ = child.kill();
        return Err(format!("Helper failed to initialize: {ready}"));
    }

    let io = Arc::new(Mutex::new(ChildIO {
        stdin,
        stdout: reader,
    }));
    let stop = Arc::new(AtomicBool::new(false));
    let thread = {
        let io = Arc::clone(&io);
        let stop = Arc::clone(&stop);
        let app = app.clone();
        thread::spawn(move || {
            while !stop.load(Ordering::Relaxed) {
                let frame_result: Result<Vec<u8>, ()> = {
                    let mut io_guard = match io.lock() {
                        Ok(g) => g,
                        Err(_) => break,
                    };
                    let io_ref: &mut ChildIO = &mut *io_guard;
                    let send_err = io_ref.stdin.write_all(b"FRAME\n").is_err()
                        || io_ref.stdin.flush().is_err();
                    if send_err {
                        break;
                    }
                    let mut header = String::new();
                    if io_ref.stdout.read_line(&mut header).is_err() {
                        break;
                    }
                    let header = header.trim();
                    if let Some(rest) = header.strip_prefix("OK ") {
                        if let Ok(len) = rest.parse::<usize>() {
                            let mut buf = vec![0u8; len];
                            if io_ref.stdout.read_exact(&mut buf).is_err() {
                                break;
                            }
                            Ok(buf)
                        } else {
                            Err(())
                        }
                    } else {
                        // ERR or unexpected, skip this frame.
                        Err(())
                    }
                };

                if let Ok(frame) = frame_result {
                    let b64 = general_purpose::STANDARD.encode(&frame);
                    let _ = app.emit(PREVIEW_EVENT, b64);
                }

                thread::sleep(Duration::from_millis(FRAME_INTERVAL_MS));
            }
        })
    };

    *guard = Some(PreviewSession {
        child,
        io,
        stop,
        thread: Some(thread),
    });
    Ok(())
}

#[tauri::command]
pub fn stop_face_preview() -> Result<(), String> {
    let mut guard = SESSION.lock().map_err(|e| e.to_string())?;
    if let Some(mut sess) = guard.take() {
        sess.stop.store(true, Ordering::Relaxed);
        // Best-effort QUIT; helper exits on EOF anyway.
        if let Ok(mut io) = sess.io.lock() {
            let _ = io.stdin.write_all(b"QUIT\n");
            let _ = io.stdin.flush();
        }
        if let Some(t) = sess.thread.take() {
            let _ = t.join();
        }
        let _ = sess.child.kill();
        let _ = sess.child.wait();
    }
    Ok(())
}

#[tauri::command]
pub fn capture_face_in_session(app: AppHandle) -> Result<String, String> {
    let guard = SESSION.lock().map_err(|e| e.to_string())?;
    let sess = guard.as_ref().ok_or("No active preview session")?;

    let faces_dir = get_faces_dir(&app)?;
    if !faces_dir.exists() {
        std::fs::create_dir_all(&faces_dir)
            .map_err(|e| format!("Failed to create faces directory: {e}"))?;
    }

    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| format!("Failed to get timestamp: {e}"))?
        .as_millis();
    let file_path = faces_dir.join(format!("face_{}.jpg", ts));

    let mut io = sess.io.lock().map_err(|e| e.to_string())?;
    let cmd = format!("CAPTURE {}\n", file_path.display());
    io.stdin
        .write_all(cmd.as_bytes())
        .map_err(|e| format!("write CAPTURE: {e}"))?;
    io.stdin.flush().map_err(|e| format!("flush: {e}"))?;

    let mut response = String::new();
    io.stdout
        .read_line(&mut response)
        .map_err(|e| format!("read response: {e}"))?;
    let response = response.trim();

    match response {
        // "OK" or "OK <path>": the helper may add a sensor tag to the file name.
        s if s == "OK" || s.starts_with("OK ") => Ok(s
            .strip_prefix("OK ")
            .filter(|p| !p.is_empty())
            .map(str::to_string)
            .unwrap_or_else(|| file_path.to_string_lossy().to_string())),
        "NO_FACE" => {
            Err("No face detected. Please position your face in front of the camera.".into())
        }
        s if s.starts_with("ERR") => Err(s.to_string()),
        other => Err(format!("Unexpected response: {other}")),
    }
}

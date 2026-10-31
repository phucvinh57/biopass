use std::io::{self, BufRead, BufReader, Read, Write};
use std::path::Path;
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use base64::{engine::general_purpose, Engine as _};
use tauri::{AppHandle, Emitter, State};

use crate::config::{load_config, BiopassConfig};
use crate::db;
use crate::paths::get_faces_dir;

const PREVIEW_EVENT: &str = "face-preview-frame";
const FRAME_INTERVAL_MS: u64 = 33; // ~30fps ceiling

// Upper bound on one JPEG frame. A larger header means the stream is corrupt.
const MAX_FRAME_BYTES: usize = 32 * 1024 * 1024;
// Opening the camera and loading the model can take a while.
const READY_TIMEOUT: Duration = Duration::from_secs(20);
const FRAME_TIMEOUT: Duration = Duration::from_secs(5);
const CAPTURE_TIMEOUT: Duration = Duration::from_secs(15);
// How long the helper gets to exit after QUIT before it is killed.
const QUIT_GRACE: Duration = Duration::from_millis(500);
// How long a kill waits for the helper to exit before reaping it in the background.
const KILL_WAIT: Duration = Duration::from_secs(2);

/// Reads bytes that a pump thread pulls from the helper's stdout, so a read
/// can give up at a deadline instead of blocking forever on a stuck helper.
struct ChanReader {
    rx: Receiver<Vec<u8>>,
    buf: Vec<u8>,
    pos: usize,
    deadline: Option<Instant>,
}

impl ChanReader {
    fn new(rx: Receiver<Vec<u8>>) -> Self {
        Self {
            rx,
            buf: Vec::new(),
            pos: 0,
            deadline: None,
        }
    }

    // The pump thread ends when the source hits EOF (the helper exited).
    fn spawn<R: Read + Send + 'static>(mut src: R) -> Self {
        let (tx, rx) = mpsc::channel();
        thread::spawn(move || {
            let mut chunk = vec![0u8; 64 * 1024];
            loop {
                match src.read(&mut chunk) {
                    Ok(0) => break,
                    Ok(n) => {
                        if tx.send(chunk[..n].to_vec()).is_err() {
                            break;
                        }
                    }
                    Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
                    Err(_) => break,
                }
            }
        });
        Self::new(rx)
    }

    fn set_timeout(&mut self, timeout: Duration) {
        self.deadline = Some(Instant::now() + timeout);
    }
}

impl Read for ChanReader {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        if self.pos == self.buf.len() {
            let chunk = match self.deadline {
                Some(d) => self
                    .rx
                    .recv_timeout(d.saturating_duration_since(Instant::now())),
                None => self.rx.recv().map_err(|_| RecvTimeoutError::Disconnected),
            };
            match chunk {
                Ok(c) => {
                    self.buf = c;
                    self.pos = 0;
                }
                Err(RecvTimeoutError::Timeout) => {
                    return Err(io::Error::new(
                        io::ErrorKind::TimedOut,
                        "helper did not reply in time",
                    ))
                }
                Err(RecvTimeoutError::Disconnected) => return Ok(0),
            }
        }
        let n = out.len().min(self.buf.len() - self.pos);
        out[..n].copy_from_slice(&self.buf[self.pos..self.pos + n]);
        self.pos += n;
        Ok(n)
    }
}

struct ChildIO<W = ChildStdin> {
    stdin: W,
    stdout: BufReader<ChanReader>,
}

impl<W: Write> ChildIO<W> {
    // Sends one command and starts the reply deadline.
    fn send(&mut self, cmd: &[u8], timeout: Duration) -> io::Result<()> {
        self.stdin.write_all(cmd)?;
        self.stdin.flush()?;
        self.stdout.get_mut().set_timeout(timeout);
        Ok(())
    }
}

/// Owns the helper process: it is killed and reaped when the guard drops, so
/// no early return can leak it or leave a zombie.
struct ChildGuard(Mutex<Option<Child>>);

// Polls `try_wait` until the process exits or `limit` passes. An error means
// it can't be waited on any more, which counts as gone.
fn exited_within(mut try_wait: impl FnMut() -> io::Result<bool>, limit: Duration) -> bool {
    let deadline = Instant::now() + limit;
    loop {
        match try_wait() {
            Ok(true) | Err(_) => return true,
            Ok(false) if Instant::now() >= deadline => return false,
            Ok(false) => thread::sleep(Duration::from_millis(20)),
        }
    }
}

impl ChildGuard {
    fn new(child: Child) -> Self {
        Self(Mutex::new(Some(child)))
    }

    fn child(&self) -> MutexGuard<'_, Option<Child>> {
        self.0.lock().unwrap_or_else(|e| e.into_inner())
    }

    // SIGKILL, then wait at most KILL_WAIT. A helper stuck in the kernel (D
    // state) only dies when its syscall returns; it is then reaped on a
    // detached thread so the caller (and any lock it holds) is not blocked.
    fn kill(&self) {
        let mut slot = self.child();
        let Some(c) = slot.as_mut() else { return };
        let _ = c.kill();
        if exited_within(|| c.try_wait().map(|s| s.is_some()), KILL_WAIT) {
            return;
        }
        eprintln!("face preview: helper did not exit after SIGKILL; reaping in background");
        if let Some(mut c) = slot.take() {
            thread::spawn(move || {
                let _ = c.wait();
            });
        }
    }

    // Lets the helper exit on its own for `grace`, then kills it.
    fn reap(&self, grace: Duration) {
        let exited = {
            let mut slot = self.child();
            match slot.as_mut() {
                Some(c) => exited_within(|| c.try_wait().map(|s| s.is_some()), grace),
                None => true,
            }
        };
        if !exited {
            self.kill();
        }
    }
}

impl Drop for ChildGuard {
    fn drop(&mut self) {
        self.kill();
    }
}

struct PreviewSession {
    child: Arc<ChildGuard>,
    io: Arc<Mutex<ChildIO>>,
    stop: Arc<AtomicBool>,
    thread: Option<JoinHandle<()>>,
}

impl PreviewSession {
    fn is_alive(&self) -> bool {
        self.thread.as_ref().is_some_and(|t| !t.is_finished())
    }
}

impl Drop for PreviewSession {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        // The frame loop exits after its current request (bounded by FRAME_TIMEOUT).
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
        // Best-effort QUIT so the helper releases the camera cleanly.
        if let Ok(mut io) = self.io.lock() {
            let _ = io.stdin.write_all(b"QUIT\n");
            let _ = io.stdin.flush();
        }
        self.child.reap(QUIT_GRACE);
    }
}

type Slot = Arc<Mutex<Option<PreviewSession>>>;

/// The face preview session, managed by Tauri.
#[derive(Default)]
pub struct PreviewState(Slot);

fn helper_path() -> String {
    if Path::new("/usr/bin/biopass-helper").exists() {
        "/usr/bin/biopass-helper".into()
    } else if Path::new("../../auth/build/pam/biopass-helper").exists() {
        "../../auth/build/pam/biopass-helper".into()
    } else {
        "biopass-helper".into()
    }
}

fn read_line_trim<R: BufRead>(reader: &mut R) -> io::Result<String> {
    let mut line = String::new();
    let n = reader.read_line(&mut line)?;
    if n == 0 {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
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

// Reads lines until one is a reply the caller expects. Anything else on the
// helper's stdout (e.g. a stray log line) is skipped; otherwise it would be
// taken as the reply and leave the real one queued for the next command.
fn read_reply<R: BufRead>(reader: &mut R, is_reply: impl Fn(&str) -> bool) -> io::Result<String> {
    loop {
        let line = read_line_trim(reader)?;
        if is_reply(&line) {
            return Ok(line);
        }
    }
}

fn is_ready_reply(line: &str) -> bool {
    line == "READY" || line.starts_with("ERR")
}

// Frame length from an `OK <byte_count>` header.
fn frame_len(line: &str) -> Option<usize> {
    line.strip_prefix("OK ")?.parse().ok()
}

fn is_frame_reply(line: &str) -> bool {
    frame_len(line).is_some() || line.starts_with("ERR")
}

fn is_capture_reply(line: &str) -> bool {
    line == "OK" || line == "NO_FACE" || line.starts_with("ERR")
}

/// Waits for the helper's `READY`. `Ok(Err(msg))` is a failure the helper reported.
fn wait_ready<W: Write>(io: &mut ChildIO<W>, timeout: Duration) -> io::Result<Result<(), String>> {
    io.stdout.get_mut().set_timeout(timeout);
    let line = read_reply(&mut io.stdout, is_ready_reply)?;
    Ok(if line == "READY" { Ok(()) } else { Err(line) })
}

/// `FRAME` -> `OK <n>\n<n bytes>` (Some) | `ERR <msg>` (None).
/// An `Err` leaves the stream out of sync; the caller must drop the helper.
fn request_frame<W: Write>(io: &mut ChildIO<W>, timeout: Duration) -> io::Result<Option<Vec<u8>>> {
    io.send(b"FRAME\n", timeout)?;
    let header = read_reply(&mut io.stdout, is_frame_reply)?;
    let Some(len) = frame_len(&header) else {
        return Ok(None);
    };
    if len > MAX_FRAME_BYTES {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("frame of {len} bytes exceeds {MAX_FRAME_BYTES}"),
        ));
    }
    let mut buf = vec![0u8; len];
    io.stdout.read_exact(&mut buf)?;
    Ok(Some(buf))
}

#[derive(Debug, PartialEq)]
enum CaptureReply {
    Saved,
    NoFace,
    Failed(String),
}

/// `CAPTURE <path>` -> `OK` | `NO_FACE` | `ERR <msg>`.
fn request_capture<W: Write>(
    io: &mut ChildIO<W>,
    path: &Path,
    timeout: Duration,
) -> io::Result<CaptureReply> {
    io.send(format!("CAPTURE {}\n", path.display()).as_bytes(), timeout)?;
    let line = read_reply(&mut io.stdout, is_capture_reply)?;
    Ok(match line.as_str() {
        "OK" => CaptureReply::Saved,
        "NO_FACE" => CaptureReply::NoFace,
        _ => CaptureReply::Failed(line),
    })
}

fn frame_loop(
    app: AppHandle,
    io: Arc<Mutex<ChildIO>>,
    child: Arc<ChildGuard>,
    stop: Arc<AtomicBool>,
) {
    while !stop.load(Ordering::Relaxed) {
        let frame = match io.lock() {
            Ok(mut io) => request_frame(&mut io, FRAME_TIMEOUT),
            Err(_) => break,
        };
        match frame {
            Ok(Some(frame)) => {
                let _ = app.emit(PREVIEW_EVENT, general_purpose::STANDARD.encode(&frame));
            }
            Ok(None) => {} // ERR from the helper: skip this frame.
            Err(e) => {
                if !stop.load(Ordering::Relaxed) {
                    eprintln!("face preview: helper failed: {e}");
                }
                // Stuck or out of sync: kill it so later commands fail fast.
                child.kill();
                break;
            }
        }
        thread::sleep(Duration::from_millis(FRAME_INTERVAL_MS));
    }
}

fn lock_slot(slot: &Slot) -> Result<MutexGuard<'_, Option<PreviewSession>>, String> {
    slot.lock().map_err(|e| e.to_string())
}

fn start(app: &AppHandle, slot: &Slot, camera: Option<String>) -> Result<(), String> {
    let mut guard = lock_slot(slot)?;
    if guard.as_ref().is_some_and(PreviewSession::is_alive) {
        return Ok(());
    }
    // A session whose helper died is dropped here and restarted below.
    *guard = None;

    let config: BiopassConfig = load_config(app.clone())?;
    let conn = db::open(app)?;
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
    let stdin = child.stdin.take();
    let stdout = child.stdout.take();
    // From here on, every early return kills and reaps the helper.
    let child = Arc::new(ChildGuard::new(child));
    let mut io = ChildIO {
        stdin: stdin.ok_or("missing stdin")?,
        stdout: BufReader::new(ChanReader::spawn(stdout.ok_or("missing stdout")?)),
    };

    wait_ready(&mut io, READY_TIMEOUT)
        .map_err(|e| format!("Helper did not respond: {e}"))?
        .map_err(|msg| format!("Helper failed to initialize: {msg}"))?;

    let io = Arc::new(Mutex::new(io));
    let stop = Arc::new(AtomicBool::new(false));
    let thread = {
        let (app, io, child, stop) = (app.clone(), io.clone(), child.clone(), stop.clone());
        thread::spawn(move || frame_loop(app, io, child, stop))
    };

    *guard = Some(PreviewSession {
        child,
        io,
        stop,
        thread: Some(thread),
    });
    Ok(())
}

fn capture(app: &AppHandle, slot: &Slot) -> Result<String, String> {
    let guard = lock_slot(slot)?;
    let sess = guard.as_ref().ok_or("No active preview session")?;

    let faces_dir = get_faces_dir(app)?;
    if !faces_dir.exists() {
        std::fs::create_dir_all(&faces_dir)
            .map_err(|e| format!("Failed to create faces directory: {e}"))?;
    }

    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| format!("Failed to get timestamp: {e}"))?
        .as_millis();
    let file_path = faces_dir.join(format!("face_{}.jpg", ts));

    let reply = {
        let mut io = sess.io.lock().map_err(|e| e.to_string())?;
        request_capture(&mut io, &file_path, CAPTURE_TIMEOUT)
    };
    match reply {
        Ok(CaptureReply::Saved) => Ok(file_path.to_string_lossy().to_string()),
        Ok(CaptureReply::NoFace) => {
            Err("No face detected. Please position your face in front of the camera.".into())
        }
        Ok(CaptureReply::Failed(msg)) => Err(msg),
        Err(e) => {
            // Stuck or out of sync: kill it, which also ends the frame loop.
            sess.child.kill();
            Err(format!("Helper failed: {e}"))
        }
    }
}

// Runs blocking helper I/O off the main thread so the UI never freezes.
async fn run_blocking<T: Send + 'static>(
    f: impl FnOnce() -> Result<T, String> + Send + 'static,
) -> Result<T, String> {
    tauri::async_runtime::spawn_blocking(f)
        .await
        .map_err(|e| format!("Preview task failed: {e}"))?
}

#[tauri::command]
pub async fn start_face_preview(
    app: AppHandle,
    state: State<'_, PreviewState>,
    camera: Option<String>,
) -> Result<(), String> {
    let slot = state.0.clone();
    run_blocking(move || start(&app, &slot, camera)).await
}

#[tauri::command]
pub async fn stop_face_preview(state: State<'_, PreviewState>) -> Result<(), String> {
    let slot = state.0.clone();
    run_blocking(move || {
        // Dropping the session stops the frame loop and reaps the helper.
        let sess = lock_slot(&slot)?.take();
        drop(sess);
        Ok(())
    })
    .await
}

#[tauri::command]
pub async fn capture_face_in_session(
    app: AppHandle,
    state: State<'_, PreviewState>,
) -> Result<String, String> {
    let slot = state.0.clone();
    run_blocking(move || capture(&app, &slot)).await
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn skips_log_noise_before_reply() {
        let mut r = Cursor::new(&b"[info] libcamera: noise\nREADY\n"[..]);
        assert_eq!(read_reply(&mut r, is_ready_reply).unwrap(), "READY");
    }

    #[test]
    fn frame_payload_does_not_leak_into_next_reply() {
        let mut r = Cursor::new(&b"log line\nOK 5\nab\ncdanother log\nNO_FACE\n"[..]);
        let header = read_reply(&mut r, is_frame_reply).unwrap();
        let mut buf = vec![0u8; frame_len(&header).unwrap()];
        r.read_exact(&mut buf).unwrap();
        assert_eq!(buf, b"ab\ncd");
        assert_eq!(read_reply(&mut r, is_capture_reply).unwrap(), "NO_FACE");
    }

    #[test]
    fn frame_err_is_a_reply() {
        let mut r = Cursor::new(&b"ERR capture failed\n"[..]);
        let header = read_reply(&mut r, is_frame_reply).unwrap();
        assert_eq!(frame_len(&header), None);
    }

    #[test]
    fn eof_without_reply_is_an_error() {
        let mut r = Cursor::new(&b"noise\n"[..]);
        assert!(read_reply(&mut r, is_capture_reply).is_err());
    }

    fn io_with(bytes: &[u8]) -> ChildIO<Vec<u8>> {
        let (tx, rx) = mpsc::channel();
        tx.send(bytes.to_vec()).unwrap();
        ChildIO {
            stdin: Vec::new(),
            stdout: BufReader::new(ChanReader::new(rx)),
        }
    }

    const T: Duration = Duration::from_secs(1);

    #[test]
    fn request_frame_reads_ok_and_bytes() {
        let mut io = io_with(b"noise\nOK 5\nab\ncd");
        assert_eq!(request_frame(&mut io, T).unwrap(), Some(b"ab\ncd".to_vec()));
        assert_eq!(io.stdin, b"FRAME\n");
    }

    #[test]
    fn request_frame_err_is_none() {
        let mut io = io_with(b"ERR capture failed\n");
        assert_eq!(request_frame(&mut io, T).unwrap(), None);
    }

    #[test]
    fn request_frame_short_read_is_error() {
        let mut io = io_with(b"OK 10\nabc");
        let e = request_frame(&mut io, T).unwrap_err();
        assert_eq!(e.kind(), io::ErrorKind::UnexpectedEof);
    }

    #[test]
    fn request_frame_garbage_header_is_skipped_until_eof() {
        let mut io = io_with(b"OK abc\nOK -5\n");
        let e = request_frame(&mut io, T).unwrap_err();
        assert_eq!(e.kind(), io::ErrorKind::UnexpectedEof);
    }

    #[test]
    fn request_frame_rejects_oversized_frame() {
        let header = format!("OK {}\n", MAX_FRAME_BYTES + 1);
        let mut io = io_with(header.as_bytes());
        let e = request_frame(&mut io, T).unwrap_err();
        assert_eq!(e.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn request_capture_replies() {
        let p = std::path::Path::new("/tmp/f.jpg");
        let mut io = io_with(b"OK\n");
        assert_eq!(request_capture(&mut io, p, T).unwrap(), CaptureReply::Saved);
        assert_eq!(io.stdin, b"CAPTURE /tmp/f.jpg\n");
        let mut io = io_with(b"NO_FACE\n");
        assert_eq!(
            request_capture(&mut io, p, T).unwrap(),
            CaptureReply::NoFace
        );
        let mut io = io_with(b"ERR save failed\n");
        assert_eq!(
            request_capture(&mut io, p, T).unwrap(),
            CaptureReply::Failed("ERR save failed".into())
        );
    }

    #[test]
    fn wait_ready_replies() {
        assert!(wait_ready(&mut io_with(b"log\nREADY\n"), T)
            .unwrap()
            .is_ok());
        assert_eq!(
            wait_ready(&mut io_with(b"ERR failed to open camera\n"), T).unwrap(),
            Err("ERR failed to open camera".into())
        );
    }

    // A source that never produces data, like a hung helper.
    struct Stuck;
    impl Read for Stuck {
        fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
            loop {
                thread::park();
            }
        }
    }

    #[test]
    fn stuck_helper_times_out() {
        let mut io = ChildIO {
            stdin: Vec::new(),
            stdout: BufReader::new(ChanReader::spawn(Stuck)),
        };
        let start = Instant::now();
        let e = request_frame(&mut io, Duration::from_millis(100)).unwrap_err();
        assert_eq!(e.kind(), io::ErrorKind::TimedOut);
        assert!(start.elapsed() < Duration::from_secs(2));
    }

    #[test]
    fn wait_for_exit_is_bounded() {
        // A process that never exits (e.g. stuck in D state after SIGKILL).
        let start = Instant::now();
        assert!(!exited_within(|| Ok(false), Duration::from_millis(100)));
        assert!(start.elapsed() < Duration::from_secs(2));
        assert!(exited_within(|| Ok(true), Duration::from_secs(5)));
        assert!(exited_within(
            || Err(io::Error::other("ECHILD")),
            Duration::from_secs(5)
        ));
    }

    #[test]
    fn guard_kills_and_reaps_child_on_drop() {
        let child = Command::new("sleep").arg("30").spawn().unwrap();
        let proc_dir = format!("/proc/{}", child.id());
        let guard = ChildGuard::new(child);
        assert!(std::path::Path::new(&proc_dir).exists());
        let start = Instant::now();
        drop(guard);
        assert!(start.elapsed() < Duration::from_secs(5));
        // Reaped, not a zombie: the /proc entry is gone.
        assert!(!std::path::Path::new(&proc_dir).exists());
    }
}

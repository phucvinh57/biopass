//! Guards the webview hardening in tauri.conf.json and the capability file
//! so a later edit can't silently reopen them.

use serde_json::Value;

fn tauri_conf() -> Value {
    serde_json::from_str(include_str!("../tauri.conf.json")).expect("tauri.conf.json parses")
}

fn capability() -> Value {
    serde_json::from_str(include_str!("../capabilities/default.json"))
        .expect("capabilities/default.json parses")
}

fn csp_directive(csp: &str, name: &str) -> Option<Vec<String>> {
    csp.split(';').map(str::trim).find_map(|d| {
        let mut parts = d.split_whitespace();
        (parts.next() == Some(name)).then(|| parts.map(String::from).collect())
    })
}

#[test]
fn csp_is_set_and_strict() {
    let conf = tauri_conf();
    let csp = conf["app"]["security"]["csp"]
        .as_str()
        .expect("app.security.csp must be a non-null string");

    assert_eq!(
        csp_directive(csp, "default-src"),
        Some(vec!["'self'".into()])
    );
    let script = csp_directive(csp, "script-src").expect("script-src set");
    assert_eq!(script, vec!["'self'".to_string()]);
    assert_eq!(
        csp_directive(csp, "object-src"),
        Some(vec!["'none'".into()])
    );

    // Nothing may pull from a remote origin or allow eval.
    const LOCAL_HTTP: [&str; 2] = ["http://asset.localhost", "http://ipc.localhost"];
    for token in csp.split(|c: char| c == ';' || c.is_whitespace()) {
        let is_http = token.starts_with("http:") || token.starts_with("https:");
        assert!(
            !is_http || LOCAL_HTTP.contains(&token),
            "remote source in CSP: {token}"
        );
        assert!(token != "*", "wildcard source in CSP");
        assert!(token != "'unsafe-eval'", "unsafe-eval in CSP");
    }

    // Face thumbnails (asset protocol) and the live preview (data: JPEG).
    let img = csp_directive(csp, "img-src").expect("img-src set");
    for src in ["asset:", "http://asset.localhost", "data:"] {
        assert!(img.iter().any(|s| s == src), "img-src lacks {src}");
    }
    // Tauri IPC.
    let connect = csp_directive(csp, "connect-src").expect("connect-src set");
    for src in ["ipc:", "http://ipc.localhost"] {
        assert!(connect.iter().any(|s| s == src), "connect-src lacks {src}");
    }
}

#[test]
fn asset_scope_is_only_the_faces_dir() {
    let conf = tauri_conf();
    let scope = &conf["app"]["security"]["assetProtocol"]["scope"];
    assert_eq!(scope, &serde_json::json!(["$APPDATA/faces/**"]));
}

#[test]
fn capability_has_no_unused_write_permissions() {
    let cap = capability();
    let ids: Vec<&str> = cap["permissions"]
        .as_array()
        .expect("permissions array")
        .iter()
        .filter_map(|p| p.as_str().or_else(|| p["identifier"].as_str()))
        .collect();
    for id in &ids {
        assert!(
            !id.contains("write") && !id.contains("remove") && !id.contains("mkdir"),
            "unexpected write permission: {id}"
        );
    }
}

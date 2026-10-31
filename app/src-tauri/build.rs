use std::env;
use std::path::PathBuf;

fn main() {
    let project_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let library_dir = PathBuf::from(project_dir).join("../../auth/build/fingerprint");

    println!("cargo:rustc-link-search=native={}", library_dir.display());
    println!("cargo:rustc-link-lib=static=biopass_fingerprint");
    println!(
        "cargo:rerun-if-changed={}",
        library_dir.join("libbiopass_fingerprint.a").display()
    );

    println!("cargo:rustc-link-lib=stdc++");

    pkg_config::Config::new()
        .atleast_version("2.0")
        .probe("glib-2.0")
        .unwrap();
    pkg_config::Config::new()
        .atleast_version("2.0")
        .probe("gio-2.0")
        .unwrap();
    pkg_config::Config::new()
        .atleast_version("2.0")
        .probe("gobject-2.0")
        .unwrap();

    tauri_build::build()
}

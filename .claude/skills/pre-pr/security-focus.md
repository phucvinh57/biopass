Biopass is a Linux PAM module plus a Tauri desktop app. Pay particular attention to:

- PAM fail-open: any path in auth/pam or auth/core that returns PAM_SUCCESS without a verified biometric match (timeouts, helper crashes, signals, missing config or models, unexpected exit codes).
- biopass-helper and post-install/remove scripts run as root: check for trust in user-writable paths (~/.config, ~/.local/share, /tmp), environment variables, symlink races, and unsanitised config values.
- Anti-spoofing bypass: config or code paths that disable or weaken liveness checks without the user explicitly choosing it.
- PAM configuration (app/src-tauri/scripts/pam.conf) changes that alter control flags or ordering in a way that could bypass or lock out password authentication.
- Tauri commands and capabilities that expose filesystem, shell or system access to the webview beyond what the UI needs.
- Model downloads: integrity of fetched ONNX models and URLs.

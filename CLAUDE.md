# Biopass

Multi-modal biometric login (face + fingerprint) for Linux. Full guide: [CONTRIBUTING.md](CONTRIBUTING.md).

## Layout

- `auth/` — C++17 / CMake. PAM module (`auth/pam`: `libbiopass_pam.so` + `biopass-helper`), face (ONNX Runtime), fingerprint, shared `core`. libcamera is built from source and bundled (`auth/BundleLibcamera.cmake`).
- `app/` — Tauri v2 desktop app. React + Vite + TypeScript + Tailwind in `app/src`, Rust in `app/src-tauri` (links `auth/build/fingerprint/libbiopass_fingerprint.a`).
- `app/src-tauri/scripts/` — deb/rpm post-install scripts, PAM config, ld config. Shipped to users as root.
- `docs/` — PAM, Polkit, IR camera docs.

## Verify before saying "done"

Show the output; don't just claim it passes. Run what the change touches:

| Area | Command |
| --- | --- |
| Frontend | `cd app && bun run lint && bunx tsc --noEmit` |
| Rust | `cd app/src-tauri && cargo fmt --all --check && cargo clippy --all-targets -- -D warnings && cargo test` (needs `make build-auth` and `app/dist` from `bun run build`) |
| C++ | `make build-auth` and `clang-format --dry-run --Werror` on changed `auth/**/*.{cc,h}` |
| Packaging | `make package` |

Before opening or updating a PR, run `/pre-pr`. It does the checks above, a fresh-context review against REVIEW.md, and `/security-review` for high-risk paths. All AI review runs locally; GitHub CI only runs the mechanical checks.

## Rules

- Never test PAM changes on the host's real `/etc/pam.d` — use a VM (see CONTRIBUTING "System Lockout Warnings").
- PAM code must fail closed: any error → `PAM_AUTH_ERR`/`PAM_IGNORE`, never `PAM_SUCCESS`.
- Don't add dependencies or change auth/install/packaging/PAM flows without a linked discussion.
- PR descriptions and issues may be AI-drafted. Follow `.github/PULL_REQUEST_TEMPLATE.md` / `.github/ISSUE_TEMPLATE/`, keep every heading, facts and real output only.
- Conventional commits: `fix(scope): …`, `feat(scope): …`, `chore: …`.
- Review checklist and known recurring issues: [REVIEW.md](REVIEW.md).

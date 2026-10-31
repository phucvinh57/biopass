# Review guide

Read by the local reviewer (`/pre-pr`, `/code-review`) and by humans. Keep it short.

## What counts as a failure

Report these as **Important**:

1. **PAM fails open.** Any path in `auth/pam` or `auth/core` that can return `PAM_SUCCESS` without a successful biometric match (timeouts, crashes, missing config, missing models, helper exit codes).
2. **Lockout.** Anything that can stop a user from logging in by password: PAM config in `app/src-tauri/scripts/pam.conf`, post-install/remove scripts, changes to `Auth-Type`/`Priority`/control flags.
3. **Privilege boundary.** Root-run code (`biopass-helper`, `postinst`/`postrm`, Polkit) trusting user-writable paths, env vars, or config without validation.
4. **Packaging drift.** File paths in `tauri.conf.json` bundle `files` that no longer match what `auth/` builds; deb and rpm sections diverging without reason.
5. **Docs lie.** README/CONTRIBUTING/`docs/*.md` describing commands, paths, or flows that the change makes wrong.
6. **Real bugs** anywhere else: crashes, data loss in the config/DB, broken Tauri commands, unhandled errors in the UI.

## What not to report

- Formatting and import order — Biome, clang-format and rustfmt enforce them.
- Style preferences, naming bikesheds, "consider adding a comment".
- Speculative refactors unrelated to the diff.
- At most 2 **Nit** comments per review.

## Ignored paths

`app/src/routeTree.gen.ts`, `app/bun.lock`, `app/src-tauri/Cargo.lock`, `app/src-tauri/icons/**`, `assets/**`, `docs/images/**`, `auth/face/models/**`.

## Output format

- Every finding cites `path:line`. Don't guess filenames — open the file.
- Label each finding **Important**, **Nit**, or **Pre-existing** (bug not introduced by this PR).
- For a bug class, say so once and list all locations, so the author can fix them together.
- Start with a one-line summary: `N important, N nits, N pre-existing` — or `No issues found.`
- On re-review, look only at the new changes; don't repeat findings already raised. After 3 rounds on the same change, stop and ask the author to rethink the approach instead.

## Recurring issues

Add an entry here whenever the reviewer (or a revert/incident) shows the same mistake twice. Format: `- YYYY-MM-DD — what went wrong — how to spot it`.

- _(none yet)_

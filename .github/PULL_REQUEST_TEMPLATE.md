<!--
AI tools may draft this description (`/pre-pr` does). Rules for whoever fills it in, human or AI:
- Facts only: what changed, what ran, what it printed. No marketing words, no restating the diff line by line.
- Keep every heading. Write "N/A" or "Not tested: <reason>" instead of deleting a section.
- The author reads and stands behind every line before opening the PR.
-->

## Summary

<!-- 1–3 sentences: the problem and how this change solves it. -->

Closes #<!-- issue --> / Discussion: <!-- link, required for new deps, auth/install/packaging/PAM flows, features, large refactors -->

## Changes

<!-- One bullet per behavior change, grouped by area (auth/pam, auth/face, app/src, app/src-tauri, scripts, docs). Mention anything a reviewer should look at first. -->

-

## Verification

<!-- Commands run with their real output (trim to the relevant lines). Use the CLAUDE.md table for the areas touched. -->

```text

```

Manual testing:

| Distro / version | Package (`.deb`/`.rpm`/AUR/source) | DE / display manager | Hardware (camera, IR, fingerprint) | Flow tested (login, sudo, polkit, lock screen, app) | Result |
| --- | --- | --- | --- | --- | --- |
| | | | | | |

Not tested: <!-- what was not covered and why (no device, needs VM, ...) -->

## Review report

<!-- Paste the `/pre-pr` report summary, or equivalent: AI review line (`N important, N nits, N pre-existing`), security review (ran / not needed), findings fixed or dismissed with a reason. -->

## Risk

- [ ] Low: docs, UI copy, styling, tests
- [ ] Medium: app logic, Rust/Tauri commands, face/fingerprint pipeline
- [ ] High: PAM module or helper, install/remove scripts, PAM config, packaging, CI/release workflows

What could break, and for whom:

## How to revert

<!-- Is `git revert` enough, or does it also need a release, a config migration, or users re-running `pam-auth-update`? -->

## Checklist

- [ ] I read the whole diff and can explain every change.
- [ ] Verification above shows real output, not a claim.
- [ ] All Important review findings are fixed or answered.
- [ ] Docs are updated if behavior, install, packaging, or distro support changed.

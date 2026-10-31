## Summary

Describe the change in your own words.

## Related discussion or issue

Link the discussion or issue for this work. A discussion is required before adding dependencies, changing auth/install/packaging/PAM/application flows, introducing features, or making large refactors.

## Impact

Explain who is affected and what behavior changes.

## Verification

List the commands, distros, desktop environments, PAM flows, or package types you tested. Paste or link the output (test results, screenshots, logs).

## Risk

- [ ] Low: docs, UI copy, styling, tests
- [ ] Medium: app logic, Rust/Tauri commands, face/fingerprint pipeline
- [ ] High: PAM module or helper, install/remove scripts, PAM config, packaging, CI/release workflows

What could break, and for whom?

## How to revert

Is `git revert` enough, or does a revert also need a new release, a config migration, or users to re-run `pam-auth-update`?

## Distro notes

- Distro and version:
- Package format tested (`.deb`, `.rpm`, AUR, source build, other):
- Desktop environment or display manager:
- PAM file or distro auth tool involved:

## Contributor checklist

- [ ] I wrote this PR description myself.
- [ ] I opened a discussion first if this changes dependencies, flows, features, or broad behavior.
- [ ] I included clear evidence for the behavior or distro impact.
- [ ] I updated docs when behavior, installation, packaging, or distro support changed.

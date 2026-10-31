---
name: pre-pr
description: Run Biopass's local review flow before opening or updating a PR — verification checks, a fresh-context code review against REVIEW.md, and a security review for high-risk paths. Use when the user runs /pre-pr or asks to check a change before a PR.
---

# Pre-PR review (local)

All review runs on this machine; there is no AI reviewer on GitHub. CI only runs the mechanical checks.

## 1. Scope

Diff against the base: `git diff main...HEAD` plus uncommitted changes. If the user passed a base branch, use it instead. Note which areas are touched: `app/src`, `app/src-tauri`, `auth/`, `app/src-tauri/scripts`, docs.

## 2. Verify

Run the commands from the "Verify before saying done" table in CLAUDE.md for every touched area, and show the output. If a check fails, fix it before continuing. If something can't run here (no camera, no fingerprint reader, PAM needs a VM), say so explicitly instead of skipping silently.

## 3. Code review

Spawn one subagent with fresh context (don't pass it your conclusions). Give it the diff range and tell it to follow REVIEW.md exactly and use CLAUDE.md for context. Its output must use REVIEW.md's format (`path:line`, Important / Nit / Pre-existing, summary line).

Then check each Important finding yourself by opening the cited line. Drop the ones that don't hold up and say why.

## 4. Security review

Run this step if the diff touches `auth/pam`, `auth/core`, `app/src-tauri/scripts`, `app/src-tauri/capabilities`, `tauri.conf.json`, or model download code. Run `/security-review` and apply the focus list in [security-focus.md](security-focus.md).

## 5. Converge

- Fix confirmed Important findings. Then re-run only steps 2 and 3, on the new changes only.
- Stop after 3 rounds. If findings keep appearing, tell the user to rethink the approach instead of looping.
- If the same kind of issue came up that REVIEW.md doesn't already list, propose a one-line entry for its "Recurring issues" section.

## 6. Report

Give a short report:
- the checks that ran, with pass or fail
- the findings fixed or dismissed, each with a reason
- a suggested Risk tier and "How to revert" line for the PR template

Do not write the PR description: CONTRIBUTING.md requires the author to write it.

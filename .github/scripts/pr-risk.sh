#!/usr/bin/env bash
# Classify a PR's risk from its changed paths (one per line on stdin).
# Prints low, medium or high. The highest tier of any path wins; unknown
# paths are medium. Keep High in sync with .github/CODEOWNERS and the Risk
# section of .github/PULL_REQUEST_TEMPLATE.md.
set -euo pipefail

high='^(auth/pam/|auth/core/|auth/keyring/|app/src-tauri/scripts/|app/src-tauri/capabilities/|\.github/|\.claude/)'
high+='|^(app/src-tauri/tauri\.conf\.json|Makefile|CLAUDE\.md|REVIEW\.md|lefthook\.yml|app/bun\.lock)$'
high+='|^auth/[^/]+\.cmake$'
high+='|(^|/)(package\.json|Cargo\.toml|Cargo\.lock)$'
high_cmake='(^|/)CMakeLists\.txt$'

# Low is only for changes that can't weaken auth or mislead users into a
# lockout: the PAM/Polkit/Keyring guides, CONTRIBUTING (lockout warnings) and
# the security/PAM test suites are medium.
low='^(docs/images/|assets/)'
low+='|^(README\.md|docs/IR camera\.md)$'
low+='|^app/src/.*(\.test\.ts|\.css)$'

tier=low
seen=false
# `|| [ -n "$path" ]` keeps a last line without a trailing newline.
while IFS= read -r path || [ -n "$path" ]; do
  [ -n "$path" ] || continue
  seen=true
  if [[ $path =~ $high ]] || { [[ $path =~ $high_cmake ]] && [[ $path != auth/test/* ]]; }; then
    tier=high
  elif [ "$tier" = low ] && ! [[ $path =~ $low ]]; then
    tier=medium
  fi
done
# Read all input (no early exit) so the producer never gets SIGPIPE.

# An empty file list is unexpected; don't call it low.
if [ "$seen" = false ] && [ "$tier" = low ]; then
  tier=medium
fi
echo "$tier"

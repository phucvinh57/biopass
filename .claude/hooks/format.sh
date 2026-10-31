#!/usr/bin/env bash
# PostToolUse hook: format the file Claude just edited, using the same
# tools as lefthook. Never blocks the edit; formatter errors go to stderr.
set -u

root="${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel)}"
file=$(jq -r '.tool_input.file_path // empty')
[ -n "$file" ] && [ -f "$file" ] || exit 0

case "$file" in
  "$root"/app/*.js | "$root"/app/*.ts | "$root"/app/*.jsx | "$root"/app/*.tsx | "$root"/app/*.json)
    (cd "$root/app" && bunx biome check --write --no-errors-on-unmatched "$file") >&2 || true
    ;;
  "$root"/auth/*.cc | "$root"/auth/*.h)
    clang-format -i --style=file "$file" >&2 || true
    ;;
  *.rs)
    rustfmt --edition 2021 "$file" >&2 || true
    ;;
esac
exit 0

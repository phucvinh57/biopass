#!/bin/sh
# Checks the RUNPATH of the ELF files Tauri ships as they are from the build
# tree (biopass-helper runs as root). Every entry must be $ORIGIN-relative or
# /usr/lib/biopass: no empty entry (ld.so reads it as the current directory)
# and no absolute build-machine path.
# Usage: check.sh <elf>...
command -v readelf >/dev/null 2>&1 || { echo "SKIP: readelf not found"; exit 77; }
rc=0
for f in "$@"; do
  [ -f "$f" ] || { echo "FAIL $f: missing"; rc=1; continue; }
  rp=$(LC_ALL=C readelf -d "$f" | sed -n 's/.*(R\(UN\)\{0,1\}PATH).*\[\(.*\)\]$/\2/p')
  [ -n "$rp" ] || { echo "ok   $f: no RUNPATH"; continue; }
  # shellcheck disable=SC2016 # '$ORIGIN' is literal: the loader expands it, not the shell
  bad=$(printf '%s\n' "$rp" | tr ':' '\n' | while IFS= read -r e; do
    case $e in
      '$ORIGIN' | '$ORIGIN/'* | /usr/lib/biopass) ;;
      *) printf '[%s] ' "$e" ;;
    esac
  done)
  if [ -n "$bad" ]; then
    echo "FAIL $f: bad RUNPATH entries $bad(in $rp)"
    rc=1
  else
    echo "ok   $f: $rp"
  fi
done
exit $rc

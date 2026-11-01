#!/usr/bin/env bash
# Checks the deb and rpm that `make package` builds: package dependencies,
# deb maintainer scripts, the files listed in tauri.conf.json, no ld.so.conf.d
# entry, and the RUNPATH of every shipped ELF file (auth/test/runpath/check.sh).
# Needs dpkg-deb, rpm, rpm2cpio, cpio, jq and readelf.
# Usage: check-packages.sh <bundle dir>   (app/src-tauri/target/release/bundle)
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
conf="$root/app/src-tauri/tauri.conf.json"
bundle=${1:?usage: check-packages.sh <bundle dir>}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
rc=0
fail() {
  echo "FAIL $*"
  rc=1
}

# Prints the only file matching the glob $1, or fails.
only() {
  local m
  m=$(compgen -G "$1" || true)
  [ -n "$m" ] && [ "$(printf '%s\n' "$m" | wc -l)" -eq 1 ] || {
    echo "FAIL expected exactly one $1, got: ${m:-none}" >&2
    return 1
  }
  printf '%s\n' "$m"
}

# $1 = deb|rpm, $2 = extracted tree, $3 = dependency field
check_tree() {
  local kind=$1 tree=$2 deps=$3 dep path elfs=()
  while IFS= read -r dep; do
    grep -qF -- "$dep" <<<"$deps" || fail "$kind: missing dependency '$dep'"
  done < <(jq -r ".bundle.linux.$kind.depends[]" "$conf")
  while IFS= read -r path; do
    [ -f "$tree$path" ] || fail "$kind: missing $path"
  done < <(jq -r ".bundle.linux.$kind.files | keys[]" "$conf")
  # ld.so must not search /usr/lib/biopass for every program on the system.
  if [ -n "$(find "$tree" -path '*/ld.so.conf.d*' -print -quit)" ]; then
    fail "$kind: ships an ld.so.conf.d entry"
  fi
  while IFS= read -r -d '' path; do
    if [ "$(head -c4 "$path" | od -An -c | tr -d ' \n')" = '177ELF' ]; then
      elfs+=("$path")
    fi
  done < <(find "$tree" -type f -print0)
  [ "${#elfs[@]}" -gt 0 ] || fail "$kind: no ELF files"
  echo "--- $kind RUNPATH"
  sh "$root/auth/test/runpath/check.sh" "${elfs[@]}" || fail "$kind: RUNPATH"
}

deb=$(only "$bundle/deb/*.deb")
echo "=== $deb"
dpkg-deb -I "$deb"
dpkg-deb -c "$deb"
dpkg-deb -e "$deb" "$work/deb-control"
for s in postinst prerm; do
  if [ ! -x "$work/deb-control/$s" ]; then
    fail "deb: $s missing or not executable"
  elif ! cmp -s "$work/deb-control/$s" "$root/app/src-tauri/scripts/deb/$s"; then
    fail "deb: $s differs from app/src-tauri/scripts/deb/$s"
  fi
done
dpkg-deb -x "$deb" "$work/deb"
check_tree deb "$work/deb" "$(dpkg-deb -f "$deb" Depends)"

rpm=$(only "$bundle/rpm/*.rpm")
echo "=== $rpm"
rpm -qpi "$rpm"
echo "--- requires"
rpm -qp --requires "$rpm"
echo "--- scripts"
rpm -qp --scripts "$rpm"
echo "--- files"
rpm -qpl "$rpm"
mkdir "$work/rpm"
rpm2cpio "$rpm" | (cd "$work/rpm" && cpio -idm --quiet)
check_tree rpm "$work/rpm" "$(rpm -qp --requires "$rpm")"

[ "$rc" -eq 0 ] && echo "=== packages ok"
exit "$rc"

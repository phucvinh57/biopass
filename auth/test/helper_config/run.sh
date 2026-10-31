#!/bin/sh
# Runs `biopass-helper auth` against odd config.yaml files and checks that it
# returns PAM_IGNORE (exit 2) quickly and writes nothing to stdout, which
# belongs to the PAM caller. Uses an unknown user so the helper falls back to
# $HOME for the config path. Usage: run.sh <path-to-biopass-helper>
helper="$1"
if [ "$(id -u)" -eq 0 ]; then
  # As root the helper refuses unknown users before reading anything.
  echo "SKIP: run as a normal user"
  exit 77
fi
home=$(mktemp -d /tmp/biopass-helper-config-XXXXXX) || exit 1
trap 'rm -rf "$home"' EXIT
dir="$home/.config/com.ticklab.biopass"
mkdir -p "$dir"
cfg="$dir/config.yaml"
failures=0

# $1 = description. polkit-1 is in the default ignore_services, so a config
# that falls back to defaults returns PAM_IGNORE without touching a camera.
check() {
  out="$home/stdout"
  # The memory cap keeps a helper that does read /dev/zero from eating RAM.
  (
    ulimit -v 4000000
    HOME="$home" exec timeout 10 "$helper" auth -u biopass-no-such-user -s polkit-1
  ) >"$out" 2>/dev/null
  rc=$?
  if [ "$rc" -eq 2 ] && [ ! -s "$out" ]; then
    echo "[PASS] $1"
  else
    echo "[FAIL] $1 (exit $rc, stdout: $(head -c 200 "$out"))"
    failures=$((failures + 1))
  fi
}

check "missing config is ignored"

printf 'schema_version: 1\n' >"$cfg"
check "outdated config falls back to defaults without writing to stdout"

# Valid config: debug on and no methods. Only a parsed config creates the
# debug log dir (under $HOME too, for an unknown user).
cat >"$cfg" <<'YAML'
schema_version: 2
strategy:
  debug: true
  ignore_services: []
methods:
  face:
    enable: false
  fingerprint:
    enable: false
YAML
check "regular config is read"
if [ -d "$home/.local/share/com.ticklab.biopass/logs" ]; then
  echo "[PASS] regular config is parsed (debug log dir created)"
else
  echo "[FAIL] regular config is parsed (debug log dir created)"
  failures=$((failures + 1))
fi

rm -f "$cfg"
ln -s /dev/zero "$cfg"
check "config symlinked to /dev/zero is ignored without reading it"

rm -f "$cfg"
mkfifo "$cfg"
check "config FIFO is ignored without blocking"

if [ "$failures" -eq 0 ]; then
  echo "ALL PASS"
  exit 0
fi
echo "FAILURES"
exit 1

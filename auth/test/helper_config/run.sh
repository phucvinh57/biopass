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
  # The dead system bus address keeps fprintd (and any real device) out of it.
  (
    ulimit -v 4000000
    HOME="$home" DBUS_SYSTEM_BUS_ADDRESS="unix:path=$home/no-such-bus" exec timeout 10 "$helper" auth -u biopass-no-such-user -s polkit-1
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

# Fingerprint enabled but fprintd unreachable: no method could even start, so
# the helper must fall through as PAM_IGNORE rather than report an
# authentication failure (PAM_AUTH_ERR).
cat >"$cfg" <<'YAML'
schema_version: 2
strategy:
  ignore_services: []
methods:
  face:
    enable: false
  fingerprint:
    enable: true
YAML
check "no available method is ignored"

rm -f "$cfg"
ln -s /dev/zero "$cfg"
check "config symlinked to /dev/zero is ignored without reading it"

rm -f "$cfg"
mkfifo "$cfg"
check "config FIFO is ignored without blocking"
rm -f "$cfg"

# $1 = expected exit, $2 = description, rest = helper args. Stdout must stay
# empty: it belongs to the PAM caller, so not even help text may go there.
check_args() {
  want="$1"
  what="$2"
  shift 2
  out="$home/stdout"
  (HOME="$home" DBUS_SYSTEM_BUS_ADDRESS="unix:path=$home/no-such-bus" exec timeout 10 "$helper" "$@") \
    >"$out" 2>/dev/null
  rc=$?
  if [ "$rc" -eq "$want" ] && [ ! -s "$out" ]; then
    echo "[PASS] $what"
  else
    echo "[FAIL] $what (exit $rc, want $want, stdout: $(head -c 80 "$out"))"
    failures=$((failures + 1))
  fi
}

# User names are untrusted (typed at a login prompt). Passed as one
# `--username=<name>` argument, as the PAM module does, any name is just a
# user without a config (PAM_IGNORE).
for name in --help -h --version -v --username ""; do
  check_args 2 "user name '$name' is only a name" auth "--username=$name" --service=polkit-1
done

# Anything that stops CLI parsing (help, version, bad or missing options) is
# not an authentication and must fail closed (PAM_AUTH_ERR), never exit 0.
check_args 1 "auth --help fails closed" auth --help
check_args 1 "auth -h fails closed" auth -h
check_args 1 "auth --version fails closed" auth --version
check_args 1 "auth -v fails closed" auth -v
check_args 1 "--help after the user name fails closed" auth --username=x --help
check_args 1 "unknown option fails closed" auth --username=x --bogus
check_args 1 "missing user name fails closed" auth --service=polkit-1
check_args 1 "top-level --help fails closed" --help

# Humans still get help, on stderr; a non-zero exit is fine.
if "$helper" --help 2>&1 >/dev/null | grep -q "auth"; then
  echo "[PASS] --help still prints help"
else
  echo "[FAIL] --help still prints help"
  failures=$((failures + 1))
fi

if [ "$failures" -eq 0 ]; then
  echo "ALL PASS"
  exit 0
fi
echo "FAILURES"
exit 1

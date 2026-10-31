#!/bin/sh
# Runs `biopass-helper auth` for a user with no passwd entry and checks that
# it returns PAM_IGNORE (exit 2) quickly, writes nothing to stdout (it
# belongs to the PAM caller), and never falls back to $HOME for that user's
# files. How config.yaml itself is read and parsed is covered by the
# user_file and config_validation tests. Usage: run.sh <path-to-biopass-helper>
helper="$1"
if [ "$(id -u)" -eq 0 ]; then
  echo "SKIP: run as a normal user"
  exit 77
fi
home=$(mktemp -d /tmp/biopass-helper-config-XXXXXX) || exit 1
trap 'rm -rf "$home"' EXIT
dir="$home/.config/com.ticklab.biopass"
mkdir -p "$dir"
cfg="$dir/config.yaml"
failures=0

# $1 = description. The service is not in any ignore list, so only the
# unknown user can make the helper step aside.
check() {
  out="$home/stdout"
  # The dead system bus address keeps fprintd (and any real device) out of it.
  (
    HOME="$home" DBUS_SYSTEM_BUS_ADDRESS="unix:path=$home/no-such-bus" exec timeout 10 "$helper" auth -u biopass-no-such-user -s sudo
  ) >"$out" 2>/dev/null
  rc=$?
  if [ "$rc" -eq 2 ] && [ ! -s "$out" ]; then
    echo "[PASS] $1"
  else
    echo "[FAIL] $1 (exit $rc, stdout: $(head -c 200 "$out"))"
    failures=$((failures + 1))
  fi
}

check "unknown user without a config is ignored"

# A config under $HOME with debug on: reading it would create the debug log
# dir under $HOME. A user with no passwd entry has no files, so it must not
# be read.
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
check "unknown user with a config under \$HOME is ignored"
if [ ! -e "$home/.local/share/com.ticklab.biopass" ]; then
  echo "[PASS] unknown user: the \$HOME config is not read (no debug log dir)"
else
  echo "[FAIL] unknown user: the \$HOME config is not read (no debug log dir)"
  failures=$((failures + 1))
fi
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

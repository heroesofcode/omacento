#!/usr/bin/env bash
#
# test/omacento-build.sh — does a change to the addon's source get compiled?
#
# omacento-build runs on every fcitx5 start and every time the panel applies,
# and it has to decide whether the installed .so is current. It once only asked
# "is there one, and was it built for this fcitx5?", so a plugin update that
# changed the C++ was pulled, reported as up to date, and never compiled:
# everyone who updated kept running the old addon, with no error anywhere.
#
# Works on a copy of the plugin in a temporary directory, with OMACENTO_PREFIX
# pointing into it, so it never touches ~/.local or the checkout it runs from.
# Needs what omacento-build needs: fcitx5 (for pkg-config) and base-devel.

set -uo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/plugin/addon"
cp "$HERE/omacento-build" "$WORK/plugin/"
cp -r "$HERE/addon/src" "$HERE/addon/Makefile" "$HERE/addon/omacento.conf.in" "$WORK/plugin/addon/"
export OMACENTO_PREFIX="$WORK/prefix"
BUILD="$WORK/plugin/omacento-build"

failures=0
expect() { # label wanted-exit command...
  local label=$1 want=$2 got; shift 2
  "$@" >"$WORK/out" 2>&1; got=$?
  if [[ $got == "$want" ]]; then
    printf '  ok   %s\n' "$label"
  else
    printf '  FAIL %s: exit %s, wanted %s\n' "$label" "$got" "$want"
    sed 's/^/         /' "$WORK/out"
    failures=$((failures + 1))
  fi
}

expect "a first run builds and installs (exit 10)"        10 "$BUILD"
expect "a second run finds it current and does nothing"    0 "$BUILD"
expect "--check agrees it is current"                      0 "$BUILD" --check

# What a plugin update does: the source changes, the installed .so does not.
printf '\n// changed by an update\n' >> "$WORK/plugin/addon/src/omacento.cpp"

expect "--check notices the source has changed"            1 "$BUILD" --check
expect "the next run rebuilds it (exit 10)"               10 "$BUILD"
expect "and after that it is current again"                0 "$BUILD"

# A build installed before the source was recorded at all -- every install of
# 1.1.0 and earlier -- has to be treated as out of date, once.
rm -f "$OMACENTO_PREFIX/share/fcitx5/addon/omacento.source"
expect "a build with no record of its source is rebuilt"  10 "$BUILD"

if (( failures )); then
  printf '\n%s failure(s)\n' "$failures"
  exit 1
fi
printf '\nall as expected\n'

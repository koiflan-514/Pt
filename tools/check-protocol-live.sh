#!/usr/bin/env bash
#
# Live xdg-activation-v1 and xdg-foreign-unstable-v2 check (2.4 / M26 W5).
#
# Runs the Wayland suite as a real client inside a nested niri compositor and
# requires both optional globals to be present. The suite itself asserts:
#
#   activation: get_activation_token -> done delivers a non-empty token ->
#               activate(token, surface) returns true and the connection still
#               dispatches (no protocol error).
#   foreign:    export_toplevel -> handle delivers a non-empty opaque string ->
#               a second toplevel imports it and calls set_parent_of -> both
#               windows survive the roundtrip and dispatch cleanly.
#
# The token and handle are opaque; only their non-empty length is reported, and
# neither value is parsed or persisted. Without a parent display, niri or the
# built suite the check exits 0 with a skip, so `make check` stays headless.
#
# Run from the repository root with: make check-protocol-live
set -u

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD:-$root/build}
probe=$build/tests/test_wayland

if [ "${1:-}" = "--inner" ]; then
    shift
    failed=0
    for attempt in 1 2 3; do
        out=$(mktemp "${TMPDIR:-/tmp}/pt-protocol-live.XXXXXX")
        PT_PROTOCOL_LIVE_REQUIRED=1 "$probe" >"$out" 2>&1
        status=$?
        cat "$out"
        if [ "$status" -eq 0 ] &&
           grep -q '^protocol-live activation=ok' "$out" &&
           grep -q '^protocol-live foreign=ok' "$out"; then
            echo "PASS live xdg-activation-v1 + xdg-foreign-v2 ($probe)"
            rm -f "$out"
            exit 0
        fi
        echo "live protocol attempt $attempt did not pass"
        rm -f "$out"
        failed=1
        sleep 0.3
    done
    echo "FAIL live xdg-activation-v1 + xdg-foreign-v2"
    exit 1
fi

if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${WAYLAND_SOCKET:-}" ]; then
    echo "skip live protocol (no Wayland display)"
    exit 0
fi
if ! command -v niri >/dev/null 2>&1; then
    echo "skip live protocol (niri not installed)"
    exit 0
fi
if [ ! -x "$probe" ]; then
    echo "skip live protocol (Wayland suite not built)"
    exit 0
fi

exec "$root/tools/nested-wayland.sh" --fullscreen -- \
    "$root/tools/check-protocol-live.sh" --inner
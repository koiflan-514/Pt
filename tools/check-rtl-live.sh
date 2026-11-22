#!/usr/bin/env bash
#
# Live RTL caret/selection consistency test (2.0 wave 7).
#
# Runs a focused RTL entry inside a nested niri compositor, types one byte
# through zwp_virtual_keyboard_v1 (the real wire path) and then walks the visual
# caret with Right/Left/Home/End and Shift+Right. The probe asserts, for every
# stop, that the IME rectangle (the compositor-facing caret position) matches
# the visual map's caret x within one pixel, that selection spans stay ordered
# and inside the line, and that the entry renders ink in the live surface.
#
# Screenshot readback is not exposed by the protocol surface, so the check uses
# the documented fallback: IME rect versus visual map plus a nested-compositor
# smoke run. The window must survive on the wire for the probe to print PASS.
#
# Run from the repository root with: make check-rtl-live
set -u

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD:-$root/build}
probe=$build/tests/rtl_live_probe
inject=$build/tests/vkbd_type

if [ "${1:-}" = "--inner" ]; then
    shift
    failed=0
    for attempt in 1 2 3; do
        out=$(mktemp "${TMPDIR:-/tmp}/pt-rtl-live.XXXXXX")
        "$probe" >"$out" 2>&1 &
        probe_pid=$!
        ready=0
        for _ in $(seq 1 200); do
            if grep -q '^READY' "$out" 2>/dev/null; then
                ready=1
                break
            fi
            if ! kill -0 "$probe_pid" 2>/dev/null; then
                break
            fi
            sleep 0.05
        done
        if [ "$ready" != 1 ]; then
            wait "$probe_pid" 2>/dev/null
            cat "$out"
            rm -f "$out"
            echo "live RTL attempt $attempt did not become ready"
            failed=1
            continue
        fi
        # The user can move focus after READY; put the nested window back
        # before injecting, exactly like the typing and IME live checks.
        if [ -n "${PT_PARENT_NIRI_SOCKET:-}" ] && [ -n "${PT_NESTED_WINDOW_ID:-}" ]; then
            NIRI_SOCKET=$PT_PARENT_NIRI_SOCKET niri msg action focus-window \
                --id "$PT_NESTED_WINDOW_ID" >/dev/null 2>&1 || true
            sleep 0.2
        fi
        if ! timeout 15 "$inject" x; then
            kill "$probe_pid" 2>/dev/null
            wait "$probe_pid" 2>/dev/null
            cat "$out"
            rm -f "$out"
            echo "live RTL attempt $attempt could not inject keys"
            failed=1
            continue
        fi
        status=1
        for _ in $(seq 1 400); do
            if ! kill -0 "$probe_pid" 2>/dev/null; then
                wait "$probe_pid"
                status=$?
                break
            fi
            sleep 0.05
        done
        if kill -0 "$probe_pid" 2>/dev/null; then
            kill "$probe_pid" 2>/dev/null
            wait "$probe_pid" 2>/dev/null
            status=1
        fi
        cat "$out"
        if [ "$status" -eq 0 ] && grep -q '^RESULT PASS$' "$out"; then
            echo "PASS live RTL caret/selection ($probe)"
            rm -f "$out"
            exit 0
        fi
        echo "live RTL attempt $attempt disturbed; retrying"
        rm -f "$out"
        failed=1
        sleep 0.3
    done
    echo "FAIL live RTL caret/selection"
    exit 1
fi

if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${WAYLAND_SOCKET:-}" ]; then
    echo "skip live RTL (no Wayland display)"
    exit 0
fi
if ! command -v niri >/dev/null 2>&1; then
    echo "skip live RTL (niri not installed)"
    exit 0
fi
if [ ! -x "$probe" ] || [ ! -x "$inject" ]; then
    echo "skip live RTL (probe or virtual-keyboard injector not built)"
    exit 0
fi

exec "$root/tools/nested-wayland.sh" -- "$root/tools/check-rtl-live.sh" --inner
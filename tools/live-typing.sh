#!/usr/bin/env bash
# Hermetic live typing regression test.
#
# Starts a nested niri compositor, opens a Pt window with a focused entry,
# injects keys through zwp_virtual_keyboard_v1, and asserts the entry received
# exactly the typed text while text-input-v3 is enabled. Running nested keeps
# the test out of the user's session and away from a live input method.
#
# Run from the repository root with: make check-typing
# Skips when there is no Wayland session or niri is not installed.
set -u

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD:-$root/build}
probe=$build/tests/typing_probe
inject=$build/tests/vkbd_type

# The nested runner re-invokes this script with --inner and the private
# WAYLAND_DISPLAY in the environment, so the probe and injector share one seat.
if [ "${1:-}" = "--inner" ]; then
    shift
    text=${1:-ptdirect}
    probe_pid=
    out=
    cleanup() {
        if [ -n "$probe_pid" ] && kill -0 "$probe_pid" 2>/dev/null; then
            kill "$probe_pid" 2>/dev/null
        fi
        [ -n "$out" ] && rm -f "$out"
    }
    trap cleanup EXIT INT TERM

    for attempt in 1 2 3; do
        out=$(mktemp "${TMPDIR:-/tmp}/pt-live-typing.XXXXXX")
        "$probe" "$text" >"$out" 2>&1 &
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
            probe_pid=
            echo "FAIL typing probe did not become ready"
            cat "$out"
            exit 1
        fi

        # The user can move focus after READY. The nested compositor drops its
        # keyboard focus with the parent window's focus, so put the window back
        # before the virtual keyboard types into it.
        if [ -n "${PT_PARENT_NIRI_SOCKET:-}" ] && [ -n "${PT_NESTED_WINDOW_ID:-}" ]; then
            NIRI_SOCKET=$PT_PARENT_NIRI_SOCKET niri msg action focus-window \
                --id "$PT_NESTED_WINDOW_ID" >/dev/null 2>&1 || true
            sleep 0.2
        fi

        if ! timeout 15 "$inject" "$text"; then
            echo "FAIL could not inject keys"
            cat "$out"
            exit 1
        fi
        wait "$probe_pid"
        status=$?
        probe_pid=
        cat "$out"
        if [ "$status" -eq 0 ] && grep -Fqx "TEXT=$text" "$out"; then
            echo "PASS live typing ($text)"
            exit 0
        fi
        echo "live typing attempt $attempt disturbed; retrying"
        rm -f "$out"
        out=
        sleep 0.3
    done
    echo "FAIL live typing (expected TEXT=$text)"
    exit 1
fi

text=${1:-ptdirect}
if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${WAYLAND_SOCKET:-}" ]; then
    echo "skip live typing (no Wayland display)"
    exit 0
fi
exec "$root/tools/nested-wayland.sh" -- "$root/tools/live-typing.sh" --inner "$text"

#!/usr/bin/env bash
# Run a command inside a nested niri compositor.
#
# Usage: tools/nested-wayland.sh [--fullscreen] -- COMMAND [ARG...]
#
# The parent session must be a Wayland compositor (WAYLAND_DISPLAY or
# WAYLAND_SOCKET). The nested compositor gets its own seat, pointer and
# text-input focus, so live tests do not race a desktop the user is working in.
# --fullscreen covers the parent output with the nested window, keeping the
# nested pointer focus stable for popup tests; it needs niri as the parent
# compositor and is rejected quietly otherwise.
#
# Exits 0 without running COMMAND when no parent display or niri is available,
# so live targets can stay in a headless `make` transcript.
set -u

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fullscreen=0
if [ "${1:-}" = "--fullscreen" ]; then
    fullscreen=1
    shift
fi
if [ "${1:-}" = "--" ]; then
    shift
fi
if [ "$#" -eq 0 ]; then
    echo "usage: $0 [--fullscreen] -- COMMAND [ARG...]" >&2
    exit 2
fi

if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${WAYLAND_SOCKET:-}" ]; then
    echo "skip nested Wayland (no parent display)"
    exit 0
fi
if ! command -v niri >/dev/null 2>&1; then
    echo "skip nested Wayland (niri not installed)"
    exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/pt-nested-wayland.XXXXXX")
nested_pid=
parent_niri=${NIRI_SOCKET:-}
nested_window=
cleanup() {
    if [ -n "$nested_pid" ] && kill -0 "$nested_pid" 2>/dev/null; then
        kill "$nested_pid" 2>/dev/null
        wait "$nested_pid" 2>/dev/null
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT INT TERM

cat >"$tmp/config.kdl" <<'KDL'
input {
    keyboard {
        xkb {
            layout "us"
        }
    }
}
KDL

# The parent's WAYLAND_DISPLAY/WAYLAND_SOCKET stays in the environment for the
# winit backend; niri creates its own socket under XDG_RUNTIME_DIR.
niri -c "$tmp/config.kdl" >"$tmp/niri.log" 2>&1 &
nested_pid=$!

display=
ipc=
for _ in $(seq 1 200); do
    display=$(grep -ao 'listening on Wayland socket: wayland-[0-9]*' \
        "$tmp/niri.log" 2>/dev/null | head -n1 | sed 's/.*: //')
    ipc=$(grep -ao 'IPC listening on: [^ ]*' "$tmp/niri.log" 2>/dev/null |
        head -n1 | sed 's/.*: //')
    if [ -n "$display" ]; then
        break
    fi
    if ! kill -0 "$nested_pid" 2>/dev/null; then
        echo "FAIL nested niri exited before creating a socket" >&2
        cat "$tmp/niri.log" >&2
        exit 1
    fi
    sleep 0.05
done
if [ -z "$display" ]; then
    echo "FAIL nested niri did not create a Wayland socket" >&2
    cat "$tmp/niri.log" >&2
    exit 1
fi
runtime=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
for _ in $(seq 1 100); do
    [ -S "$runtime/$display" ] && break
    sleep 0.05
done
if [ ! -S "$runtime/$display" ]; then
    echo "FAIL nested Wayland socket $runtime/$display never appeared" >&2
    cat "$tmp/niri.log" >&2
    exit 1
fi

if [ "$fullscreen" = 1 ] || [ -n "$parent_niri" ]; then
    window=
    for _ in $(seq 1 100); do
        window=$(NIRI_SOCKET="$parent_niri" niri msg windows 2>/dev/null | awk -v pid="$nested_pid" '
            /^Window ID / { id=$3; sub(":", "", id) }
            $1 == "PID:" && $2 == pid { print id; exit }')
        [ -n "$window" ] && break
        sleep 0.05
    done
    nested_window=$window
    if [ -n "$window" ]; then
        NIRI_SOCKET="$parent_niri" niri msg action focus-window --id "$window" \
            >/dev/null 2>&1 || true
        if [ "$fullscreen" = 1 ]; then
            NIRI_SOCKET="$parent_niri" niri msg action fullscreen-window \
                >/dev/null 2>&1 || true
        fi
        # A nested compositor clears its own keyboard focus when the parent
        # window is not focused, so let the parent keyboard-enter round trip
        # land before the client starts.
        sleep 0.3
    else
        echo "warning: nested Wayland window not found in the parent; running without focus/fullscreen" >&2
    fi
fi

(
    unset WAYLAND_SOCKET
    export WAYLAND_DISPLAY="$display"
    if [ -n "$ipc" ]; then
        export NIRI_SOCKET="$ipc"
    else
        unset NIRI_SOCKET
    fi
    export PT_PARENT_NIRI_SOCKET="$parent_niri"
    export PT_NESTED_WINDOW_ID="$nested_window"
    "$@"
)

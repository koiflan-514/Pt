#!/usr/bin/env bash
# Live input-method regression test (niri + fcitx5, input-method-v2).
#
# Starts a nested niri compositor and a private-session-bus fcitx5 with its
# pinyin engine, then types "nihao " into a Pt window that opted into custom IME
# delivery. A passing run needs both a non-empty preedit sequence and the
# committed text "你好" to arrive through zwp_text_input_v3.
#
# Why the keys are injected into the parent compositor: niri forwards a physical
# keyboard to the zwp_input_method_v2 keyboard grab, but key events created by
# zwp_virtual_keyboard_v1 on the nested display are delivered straight to the
# focused client and bypass that grab. Injecting on the parent display instead
# makes the nested compositor receive them from its own winit backend, which is
# the path real typing takes. The nested window must therefore be focused
# through the parent's niri IPC, and this target skips otherwise.
#
# Run from the repository root with: make check-ime-live
# Skips when there is no Wayland session, niri, fcitx5 (with pinyin) or a
# private session bus to run it on. fcitx5 is killed before this script returns.
set -u

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD:-$root/build}
probe=$build/tests/ime_live_probe
inject=$build/tests/vkbd_type
expect=你好

# The nested runner re-invokes this script with --inner inside the nested
# session, with PT_PARENT_NIRI_SOCKET, PT_NESTED_WINDOW_ID and
# PT_PARENT_WAYLAND_DISPLAY describing the parent session.
if [ "${1:-}" = "--inner" ]; then
    shift
    if [ -z "${PT_NESTED_WINDOW_ID:-}" ] || [ -z "${PT_PARENT_NIRI_SOCKET:-}" ] ||
        [ -z "${PT_PARENT_WAYLAND_DISPLAY:-}" ]; then
        echo "skip live IME (no focused nested window in a niri parent)"
        exit 0
    fi

    tmp=$(mktemp -d "${TMPDIR:-/tmp}/pt-live-ime.XXXXXX")
    fcitx_pid=
    probe_pid=
    out=
    cleanup() {
        if [ -n "$probe_pid" ] && kill -0 "$probe_pid" 2>/dev/null; then
            kill "$probe_pid" 2>/dev/null
            wait "$probe_pid" 2>/dev/null
        fi
        if [ -n "$fcitx_pid" ] && kill -0 "$fcitx_pid" 2>/dev/null; then
            kill "$fcitx_pid" 2>/dev/null
            wait "$fcitx_pid" 2>/dev/null
        fi
        [ -n "$out" ] && rm -f "$out"
        rm -rf "$tmp"
    }
    trap cleanup EXIT INT TERM

    # Keep every fcitx5 file inside the harness: the user's profile, engine
    # state and IM history must not be read or rewritten by a live test.
    mkdir -p "$tmp/config/fcitx5" "$tmp/cache" "$tmp/data" "$tmp/state"
    cat >"$tmp/config/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin

[Groups/0/Items/0]
Name=pinyin
Layout=

[GroupOrder]
0=Default
PROFILE
    export XDG_CONFIG_HOME="$tmp/config"
    export XDG_CACHE_HOME="$tmp/cache"
    export XDG_DATA_HOME="$tmp/data"
    export XDG_STATE_HOME="$tmp/state"

    # -D keeps fcitx5 in the foreground so the trap owns its lifetime. The
    # dbus addon (needed for input-context activation) stays enabled; frontends
    # and UI addons that would touch the user's desktop are disabled.
    fcitx5 -D --verbose "default=4" \
        --disable dbusfrontend,notifications,notificationitem,classicui,xim,ibusfrontend,xcb,fcitx4frontend,kimpanel,clipboard,spell,cloudpinyin \
        >"$tmp/fcitx5.log" 2>&1 &
    fcitx_pid=$!

    # Wait for the private-bus fcitx5 to register its name. The current input
    # method can only be queried once a text-input client activates it, so
    # engine selection happens after the probe reports READY.
    server=0
    for _ in $(seq 1 100); do
        if ! kill -0 "$fcitx_pid" 2>/dev/null; then
            break
        fi
        if fcitx5-remote --check >/dev/null 2>&1; then
            server=1
            break
        fi
        sleep 0.1
    done
    if [ "$server" != 1 ]; then
        echo "FAIL fcitx5 did not register on the private session bus"
        tail -n 20 "$tmp/fcitx5.log"
        exit 1
    fi

    for attempt in 1 2 3; do
        out=$(mktemp "${TMPDIR:-/tmp}/pt-live-ime-out.XXXXXX")
        "$probe" "$expect" >"$out" 2>&1 &
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
            echo "FAIL live IME probe did not become ready"
            cat "$out"
            tail -n 20 "$tmp/fcitx5.log"
            exit 1
        fi

        # The nested compositor drops keyboard focus with the parent window's
        # focus, and the input method only composes for a focused text-input.
        NIRI_SOCKET=$PT_PARENT_NIRI_SOCKET niri msg action focus-window \
            --id "$PT_NESTED_WINDOW_ID" >/dev/null 2>&1 || true
        sleep 0.2

        # Focus activated fcitx5 on the private bus. Select the pinyin engine;
        # its dictionary loads lazily, so retry until the switch is visible.
        engine=
        for _ in $(seq 1 100); do
            fcitx5-remote -s pinyin >/dev/null 2>&1 || true
            fcitx5-remote -o >/dev/null 2>&1 || true
            engine=$(fcitx5-remote -n 2>/dev/null || true)
            [ "$engine" = "pinyin" ] && break
            sleep 0.1
        done
        if [ "$engine" != "pinyin" ]; then
            echo "FAIL fcitx5 pinyin engine did not become current (got '${engine:-none}')"
            cat "$out"
            tail -n 20 "$tmp/fcitx5.log"
            exit 1
        fi

        if ! env WAYLAND_DISPLAY="$PT_PARENT_WAYLAND_DISPLAY" \
            timeout 15 "$inject" "nihao "; then
            echo "FAIL could not inject keys through the parent compositor"
            cat "$out"
            exit 1
        fi
        # The probe exits by itself after its composition window; do not let a
        # wedged compositor hang the target.
        status=1
        for _ in $(seq 1 300); do
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
        fi
        probe_pid=
        cat "$out"
        if [ "$status" -eq 0 ] && grep -Fqx "TEXT=$expect" "$out" &&
            grep -Eq '^PREEDIT=[^ ]' "$out"; then
            echo "PASS live IME (nihao + space -> $expect)"
            exit 0
        fi
        echo "live IME attempt $attempt disturbed; retrying"
        rm -f "$out"
        out=
        sleep 0.3
    done
    echo "FAIL live IME (expected preedit and TEXT=$expect)"
    tail -n 20 "$tmp/fcitx5.log"
    exit 1
fi

if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "skip live IME (no named parent Wayland display)"
    exit 0
fi
if [ -z "${NIRI_SOCKET:-}" ]; then
    echo "skip live IME (no parent niri IPC to focus and type into the nesting window)"
    exit 0
fi
command -v niri >/dev/null 2>&1 || {
    echo "skip live IME (niri not installed)"
    exit 0
}
command -v fcitx5 >/dev/null 2>&1 && command -v fcitx5-remote >/dev/null 2>&1 || {
    echo "skip live IME (fcitx5 or fcitx5-remote not installed)"
    exit 0
}
command -v dbus-run-session >/dev/null 2>&1 || {
    echo "skip live IME (dbus-run-session not installed)"
    exit 0
}
if [ ! -f /usr/share/fcitx5/addon/pinyin.conf ]; then
    echo "skip live IME (fcitx5 pinyin engine not installed)"
    exit 0
fi

# A private session bus keeps this fcitx5 away from any IME already running in
# the user's session, and lets fcitx5-remote drive the harness instance only.
exec dbus-run-session -- env PT_PARENT_WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
    "$root/tools/nested-wayland.sh" -- "$root/tools/live-ime.sh" --inner
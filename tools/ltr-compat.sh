#!/usr/bin/env bash
#
# Pt - a small Wayland-native GUI framework.
# Copyright 2026 The Pt Authors.
# Licensed under the Apache License, Version 2.0.
#
# Cross-version LTR byte-compatibility gate.
#
# Builds two revisions in isolated temporary trees, compiles tools/ltr_corpus.c
# against each tree's headers and static library, renders the fixed LTR corpus
# headlessly, and compares the resulting PPM images byte for byte.
#
# Why a same-machine comparison instead of checked-in golden hashes: FreeType,
# HarfBuzz, Fontconfig and the installed font files decide glyph rasterization.
# Those versions legitimately differ between machines, so a checked-in hash
# would be unstable. Building both revisions on one machine holds them constant
# and leaves only the library's own rendering as the variable.
#
# Important: `git archive` compares committed trees only. Uncommitted changes in
# the working tree are not part of either revision. The 2.0 release gate must be
# run on a frozen tree (clean checkout/tag), not on a dirty worktree.
#
# Usage: tools/ltr-compat.sh [old-ref] [new-ref]
#   old-ref defaults to 1.4.0, new-ref defaults to HEAD.
# Exit status: 0 when every scene is byte-identical, 1 on any difference or
# build/render failure.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OLD_REF=${1:-1.4.0}
NEW_REF=${2:-HEAD}
CC=${CC:-cc}

if ! command -v timeout >/dev/null 2>&1; then
    echo "ltr-compat: timeout(1) is required to keep the gate bounded" >&2
    exit 2
fi

resolve() {
    git -C "$ROOT" rev-parse --verify --quiet "$1^{commit}" || {
        echo "ltr-compat: unknown revision: $1" >&2
        exit 2
    }
}

OLD_COMMIT=$(resolve "$OLD_REF")
NEW_COMMIT=$(resolve "$NEW_REF")
OLD_SHORT=$(git -C "$ROOT" rev-parse --short "$OLD_COMMIT")
NEW_SHORT=$(git -C "$ROOT" rev-parse --short "$NEW_COMMIT")

TMP=$(mktemp -d /tmp/pt-ltr-compat.XXXXXX)
cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT HUP INT TERM

PKGS="wayland-client wayland-cursor xkbcommon fontconfig freetype2 harfbuzz zlib"
PKG_CFLAGS=$(pkg-config --cflags $PKGS)
PKG_LIBS=$(pkg-config --libs $PKGS)
if DBUS_LIBS=$(pkg-config --libs dbus-1 2>/dev/null); then
    PKG_LIBS="$PKG_LIBS $DBUS_LIBS"
fi

echo "ltr-compat: old=$OLD_REF ($OLD_SHORT) new=$NEW_REF ($NEW_SHORT)"
echo "ltr-compat: workspace $TMP"

prepare() {
    local label=$1 ref=$2 dir=$3
    echo "ltr-compat: [$label] extracting $ref"
    mkdir -p "$dir"
    git -C "$ROOT" archive "$ref" | tar -x -C "$dir"

    echo "ltr-compat: [$label] building libpt"
    if ! timeout 900 make -C "$dir" -j2 lib >"$dir/build.log" 2>&1; then
        echo "ltr-compat: [$label] build failed; last 40 lines:" >&2
        tail -n 40 "$dir/build.log" >&2
        exit 1
    fi

    echo "ltr-compat: [$label] compiling corpus"
    if ! timeout 300 "$CC" -std=c23 -D_GNU_SOURCE -O2 -g \
        -Wall -Wextra -Wpedantic -Werror \
        -I"$dir/include" $PKG_CFLAGS \
        "$ROOT/tools/ltr_corpus.c" "$dir/build/libpt.a" $PKG_LIBS -lm \
        -o "$dir/ltr_corpus" >"$dir/compile.log" 2>&1; then
        echo "ltr-compat: [$label] corpus compile failed; last 40 lines:" >&2
        tail -n 40 "$dir/compile.log" >&2
        exit 1
    fi

    mkdir -p "$dir/out"
    echo "ltr-compat: [$label] rendering corpus"
    if ! (cd "$dir" && PT_HEADLESS=1 timeout 180 ./ltr_corpus "$dir/out") \
        >"$dir/render.log" 2>&1; then
        echo "ltr-compat: [$label] render failed; last 40 lines:" >&2
        tail -n 40 "$dir/render.log" >&2
        exit 1
    fi
}

OLD_DIR="$TMP/old"
NEW_DIR="$TMP/new"
prepare old "$OLD_COMMIT" "$OLD_DIR"
prepare new "$NEW_COMMIT" "$NEW_DIR"

OLD_LIST="$TMP/old.list"
NEW_LIST="$TMP/new.list"
(cd "$OLD_DIR/out" && ls -1 ./*.ppm 2>/dev/null | sort) >"$OLD_LIST" || true
(cd "$NEW_DIR/out" && ls -1 ./*.ppm 2>/dev/null | sort) >"$NEW_LIST" || true

if [ ! -s "$NEW_LIST" ]; then
    echo "ltr-compat: new revision produced no images" >&2
    exit 1
fi

same=0
diff=0
echo "ltr-compat: comparing $(wc -l <"$NEW_LIST") scenes"

while IFS= read -r path; do
    name=${path#./}
    if [ ! -f "$OLD_DIR/out/$name" ]; then
        printf '  %-28s MISSING\n' "$name"
        diff=$((diff + 1))
    elif cmp -s "$OLD_DIR/out/$name" "$NEW_DIR/out/$name"; then
        printf '  %-28s SAME\n' "$name"
        same=$((same + 1))
    else
        printf '  %-28s DIFF\n' "$name"
        diff=$((diff + 1))
    fi
done <"$NEW_LIST"

# A file that exists only on the old side is a difference too (a scene was
# dropped or renamed).
while IFS= read -r path; do
    name=${path#./}
    if [ ! -f "$NEW_DIR/out/$name" ]; then
        printf '  %-28s MISSING\n' "$name"
        diff=$((diff + 1))
    fi
done <"$OLD_LIST"

echo "ltr-compat: $same same, $diff different"
if [ "$diff" -ne 0 ]; then
    echo "ltr-compat: FAIL"
    exit 1
fi
echo "ltr-compat: PASS"
exit 0

#!/bin/sh
# CI-equivalent local verification for Pt.
#
# Runs a clean werror build, every C and binding suite, API/ABI audits,
# performance guards, staged-install consumers for C/C++/Python/Java, a
# sanitizer pass, a reproducible tarball, and a build+test from that tarball.
#
# Usage: tools/ci.sh [--quick]
#   --quick skips the sanitizer pass and the tarball rebuild.
# Environment: JOBS, PT_CI_BUILD, SOURCE_DATE_EPOCH, PT_PERF_GUARD_SCALE.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
base=${PT_CI_BUILD:-$root/build/ci}
case $base in
/*) ;;
*) base=$root/$base ;;
esac
normal=$base/normal
sanitized=$base/sanitize
version=$(sed -n 's/^VERSION := //p' "$root/Makefile")
epoch=${SOURCE_DATE_EPOCH:-1767225600}
quick=0
if [ "${1:-}" = "--quick" ]; then
    quick=1
fi

say() {
    printf '\n== %s\n' "$*"
}

makeit() {
    say "make $*"
    make -C "$root" "$@"
}

say "cleaning $base"
rm -rf "$base"

makeit -j"$jobs" BUILD="$normal" werror=1 all
makeit BUILD="$normal" werror=1 check
makeit BUILD="$normal" werror=1 perf-check

say "installed consumers and ABI"
"$root/tools/install-check.sh" "$normal"

if [ "$quick" -eq 0 ]; then
    makeit -j"$jobs" BUILD="$sanitized" werror=1 sanitize=1 check-sanitize
    makeit BUILD="$sanitized" werror=1 sanitize=1 lib

    asan_lib=$(cc -print-file-name=libasan.so)
    say "sanitized Python binding suite"
    LD_PRELOAD="$asan_lib" ASAN_OPTIONS=detect_leaks=0 PT_HEADLESS=1 \
        PT_LIBRARY="$sanitized/libpt.so" PYTHONPATH="$root/bindings/python" \
        python3 -m unittest discover -s "$root/bindings/python" -p 'test_*.py'

    say "sanitized Java binding suite"
    LD_PRELOAD="$asan_lib" ASAN_OPTIONS=detect_leaks=0 PT_HEADLESS=1 \
        java --enable-native-access=ALL-UNNAMED -Dpt.library="$sanitized/libpt.so" \
        -cp "$normal/java" pt.PtTest
fi

say "reproducible tarball"
makeit BUILD="$normal" SOURCE_DATE_EPOCH="$epoch" dist
archive=$normal/dist/pt-$version.tar.gz
first=$(sha256sum "$archive" | cut -d' ' -f1)
sidecar=$(cut -d' ' -f1 "$archive.sha256")
if [ "$first" != "$sidecar" ]; then
    echo "tarball sha256 sidecar does not match the archive: $first != $sidecar" >&2
    exit 1
fi
makeit BUILD="$normal" SOURCE_DATE_EPOCH="$epoch" dist
second=$(sha256sum "$archive" | cut -d' ' -f1)
if [ "$first" != "$second" ]; then
    echo "tarball is not reproducible: $first != $second" >&2
    exit 1
fi
printf 'tarball sha256 %s (reproduced, sidecar %s)\n' "$first" "$archive.sha256"

if [ "$quick" -eq 0 ]; then
    tarball_root=$base/tarball
    rm -rf "$tarball_root"
    mkdir -p "$tarball_root"
    say "extracting and rebuilding the tarball"
    tar -xzf "$archive" -C "$tarball_root"
    tarball=$tarball_root/pt-$version
    say "make -C $tarball -j$jobs werror=1"
    make -C "$tarball" -j"$jobs" werror=1
    say "make -C $tarball check"
    make -C "$tarball" werror=1 check
fi

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    makeit BUILD="$normal" check-wayland
    makeit BUILD="$normal" check-typing
fi

printf '\nCI verification passed for Pt %s\n' "$version"

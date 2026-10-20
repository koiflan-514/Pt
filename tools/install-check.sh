#!/bin/sh
# Verify a staged install: layout, pkg-config, SONAME, exported ABI, C/C++/
# Python/Java consumers, and uninstall completeness.
# Usage: tools/install-check.sh [build-dir]
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=${1:-$root/build}
case $build in
/*) ;;
*) build=$root/$build ;;
esac
version=$(sed -n 's/^VERSION := //p' "$root/Makefile")
soname=$(sed -n 's/^SONAME := //p' "$root/Makefile")
prefix=/usr
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
run=$stage/run
mkdir -p "$run"

say() {
    printf '== %s\n' "$*"
}

say "staged install (BUILD=$build, version $version)"
make -C "$root" BUILD="$build" install PREFIX="$prefix" DESTDIR="$stage" >/dev/null
lib="$stage$prefix/lib/libpt.so.$version"
for path in \
    "$lib" \
    "$stage$prefix/lib/$soname" \
    "$stage$prefix/lib/libpt.so" \
    "$stage$prefix/lib/libpt.a" \
    "$stage$prefix/lib/pkgconfig/pt.pc" \
    "$stage$prefix/include/pt/base.h" \
    "$stage$prefix/include/pt/widget.h" \
    "$stage$prefix/include/pt/interaction.h" \
    "$stage$prefix/include/pt/pt.hpp" \
    "$stage$prefix/share/pt/bindings/python/pt.py" \
    "$stage$prefix/share/pt/bindings/java/src/pt/Pt.java"; do
    test -e "$path" || {
        echo "missing installed file: $path" >&2
        exit 1
    }
done

[ -L "$stage$prefix/lib/$soname" ] || {
    echo "missing SONAME symlink $soname" >&2
    exit 1
}
[ -L "$stage$prefix/lib/libpt.so" ] || {
    echo "missing development symlink libpt.so" >&2
    exit 1
}
actual_soname=$(readelf -d "$lib" | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
[ "$actual_soname" = "$soname" ] || {
    echo "SONAME mismatch: expected $soname, got ${actual_soname:-none}" >&2
    exit 1
}
say "SONAME $actual_soname"

export PKG_CONFIG_PATH="$stage$prefix/lib/pkgconfig"
pc_version=$(pkg-config --modversion pt)
[ "$pc_version" = "$version" ] || {
    echo "pkg-config version mismatch: expected $version, got $pc_version" >&2
    exit 1
}
pkg-config --cflags --libs pt >/dev/null
say "pkg-config reports $pc_version"

python3 "$root/tools/api-audit.py" --library "$lib" >/dev/null
say "exported ABI matches the checked-in baseline"

say "C consumer"
# shellcheck disable=SC2046
cc -std=c23 -o "$run/consumer-c" "$root/tests/install/consumer.c" \
    $(pkg-config --cflags --libs pt)
LD_LIBRARY_PATH="$stage$prefix/lib" PT_HEADLESS=1 "$run/consumer-c"

say "C++ consumer"
# shellcheck disable=SC2046
c++ -std=c++23 -o "$run/consumer-cpp" "$root/tests/install/consumer.cpp" \
    $(pkg-config --cflags --libs pt)
LD_LIBRARY_PATH="$stage$prefix/lib" PT_HEADLESS=1 "$run/consumer-cpp"

say "Python consumer"
PT_HEADLESS=1 PT_LIBRARY="$lib" \
    PYTHONPATH="$stage$prefix/share/pt/bindings/python" \
    python3 "$root/tests/install/consumer.py"

say "Java consumer"
mkdir -p "$run/java"
javac -Xlint:all -d "$run/java" \
    "$stage$prefix/share/pt/bindings/java/src/pt/Pt.java" \
    "$root/tests/install/Consumer.java"
PT_HEADLESS=1 java --enable-native-access=ALL-UNNAMED -Dpt.library="$lib" \
    -cp "$run/java" Consumer

say "uninstall"
make -C "$root" BUILD="$build" uninstall PREFIX="$prefix" DESTDIR="$stage" >/dev/null
remaining=$(find "$stage$prefix" \( -type f -o -type l \) 2>/dev/null | wc -l)
[ "$remaining" -eq 0 ] || {
    echo "uninstall left $remaining installed file(s)" >&2
    exit 1
}

echo "install check passed"

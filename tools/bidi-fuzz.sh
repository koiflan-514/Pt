#!/usr/bin/env bash
#
# Long-running bidi fuzz driver (2.0 wave 7).
#
# Runs tests/test_bidi_fuzz.c with a caller-chosen deterministic iteration
# count. `make check` runs the bounded default (100 sequences, under two
# seconds); this script is the unbounded mode used for hardening and sanitizer
# runs, where the interesting failures usually need tens of thousands of
# sequences.
#
# Usage: tools/bidi-fuzz.sh [ITERATIONS]
#   ITERATIONS defaults to 10000.
# Environment:
#   SANITIZE=1        build with address+undefined sanitizers and run with
#                     ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
#   PT_BIDI_FUZZ_SEED fixed seed (the binary also accepts it directly)
#   BUILD             build directory (default build, or build/bidi-fuzz for
#                     sanitizer runs so a normal build is never clobbered)
#   JOBS              parallel make jobs
#
# The test binary itself prints the number of sequences and the seed, so a
# failing run can be reproduced exactly:
#   PT_BIDI_FUZZ_SEED=<seed> PT_BIDI_FUZZ_ITERATIONS=<n> build/tests/test_bidi_fuzz
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
iterations=${1:-${PT_BIDI_FUZZ_ITERATIONS:-10000}}
jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
sanitize=${SANITIZE:-0}

if [ "$sanitize" = "1" ]; then
    build=${BUILD:-$root/build/bidi-fuzz-sanitize}
    build_args=(werror=1 sanitize=1)
    run_env=(ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1)
else
    build=${BUILD:-$root/build}
    build_args=(werror=1)
    run_env=()
fi

echo "bidi-fuzz: building $build/tests/test_bidi_fuzz (sanitize=$sanitize)"
make -C "$root" -j"$jobs" BUILD="$build" "${build_args[@]}" "$build/tests/test_bidi_fuzz"

echo "bidi-fuzz: running $iterations sequence(s)"
env "${run_env[@]}" PT_BIDI_FUZZ_ITERATIONS="$iterations" \
    "$build/tests/test_bidi_fuzz"
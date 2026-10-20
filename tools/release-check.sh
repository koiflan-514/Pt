#!/bin/sh
# 1.0 readiness gate for Pt.
#
# Checks the machine-verifiable items of the checklist in docs/api-policy.md:
# the version agrees across the tree, the public headers carry no draft
# markers, the API baseline and binding parity audits are clean, the generated
# reference covers every exported function, the deprecation inventory is
# documented, and each language has a current example. Anything it cannot
# decide locally is reported as an open item instead of being skipped.
#
# Usage: tools/release-check.sh [--full]
#   --full also runs tools/ci.sh --quick (werror build, every suite, staged
#   install consumers and a reproducible tarball) before the readiness report.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

full=0
if [ "${1:-}" = "--full" ]; then
    full=1
elif [ $# -gt 0 ]; then
    echo "usage: tools/release-check.sh [--full]" >&2
    exit 2
fi

failures=0
opens=0

section() {
    printf '\n== %s\n' "$*"
}

pass() {
    printf '  ok    %s\n' "$*"
}

problem() {
    printf '  FAIL  %s\n' "$*"
    failures=$((failures + 1))
}

open_item() {
    printf '  open  %s\n' "$*"
    opens=$((opens + 1))
}

version=$(sed -n 's/^VERSION := //p' Makefile)
build=${PT_RELEASE_BUILD:-build}

if [ "$full" -eq 1 ]; then
    section "CI-equivalent verification"
    ./tools/ci.sh --quick
fi

section "Version agreement"
header_version=$(sed -n 's/^#define pt_version_string "\(.*\)"/\1/p' include/pt/base.h)
readme_version=$(sed -n 's/^Status: \*\*\([0-9.]*\)\*\*.*/\1/p' README.md)
reference_version=$(sed -n 's/^Version \*\*\([0-9.]*\)\*\* · .*/\1/p' docs/api-reference.md)
soname=$(sed -n 's/^SONAME := \(.*\)/\1/p' Makefile)
if [ "$header_version" = "$version" ]; then
    pass "include/pt/base.h reports $version"
else
    problem "include/pt/base.h reports '${header_version:-nothing}', Makefile reports $version"
fi
if [ "$readme_version" = "$version" ]; then
    pass "README status line reports $version"
else
    problem "README status line reports '${readme_version:-nothing}', expected $version"
fi
if [ "$reference_version" = "$version" ]; then
    pass "docs/api-reference.md reports $version"
else
    problem "docs/api-reference.md reports '${reference_version:-nothing}', expected $version"
fi
case $version in
1.*)
    if [ "$soname" = "libpt.so.1" ]; then
        pass "SONAME $soname matches a 1.x release"
    else
        problem "SONAME $soname must become libpt.so.1 for 1.0"
    fi
    ;;
*)
    if [ "$soname" = "libpt.so.0" ]; then
        pass "SONAME $soname matches the 0.x series"
    else
        problem "SONAME $soname is neither libpt.so.0 (0.x) nor libpt.so.1 (1.x)"
    fi
    ;;
esac

section "Public header surface"
draft=$(grep -n -E 'TODO|FIXME|XXX|experimental|pt_draft|PT_DRAFT' include/pt/*.h || true)
if [ -z "$draft" ]; then
    pass "no draft or experimental markers in include/pt"
else
    problem "public headers still carry draft markers:"
    printf '%s\n' "$draft" | sed 's/^/          /'
fi

baseline_functions=$(sed -n 's/^func \([a-z0-9_]*\) .*/\1/p' api/pt.api | sort -u)
baseline_count=$(printf '%s\n' "$baseline_functions" | sed '/^$/d' | wc -l | tr -d ' ')
section "API baseline (api/pt.api)"
readme_count=$(sed -n 's/.*a \([0-9]*\)-function C ABI.*/\1/p' README.md)
reference_count=$(sed -n 's/^Version \*\*[0-9.]*\*\* · \([0-9]*\) public functions.*/\1/p' \
    docs/api-reference.md)
if [ "$readme_count" = "$baseline_count" ]; then
    pass "README quotes the baseline size ($baseline_count functions)"
else
    problem "README quotes '${readme_count:-nothing}' functions, baseline has $baseline_count"
fi
if [ "$reference_count" = "$baseline_count" ]; then
    pass "docs/api-reference.md quotes the baseline size"
else
    problem "docs/api-reference.md quotes '${reference_count:-nothing}' functions, baseline has $baseline_count"
fi

section "Audit and binding parity"
if [ ! -f "$build/libpt.so" ]; then
    problem "$build/libpt.so is missing; run make first (or --full)"
else
    report=$build/release-audit.txt
    if python3 tools/api-audit.py --library "$PWD/$build/libpt.so" >"$report" 2>&1; then
        pass "api-audit reports no baseline drift"
    else
        problem "api-audit failed; see $report"
        sed -n '/^result:/,$p' "$report" | sed 's/^/          /'
    fi
    for language in cpp python java; do
        line=$(sed -n "s/^  $language *//p" "$report" | head -1)
        if [ -z "$line" ]; then
            problem "no parity line for $language in the audit report"
            continue
        fi
        case $line in
        *"100.0%"*"0 missing"*"0 allowlisted"*)
            pass "$language: $line"
            ;;
        *)
            problem "$language: $line"
            ;;
        esac
    done
fi

section "Generated reference coverage"
undocumented=$(
    for name in $baseline_functions; do
        grep -q "\b$name\b" docs/api-reference.md || printf '%s\n' "$name"
    done
)
if [ -z "$undocumented" ]; then
    pass "docs/api-reference.md mentions all $baseline_count baseline functions"
else
    problem "functions absent from the generated reference:"
    printf '%s\n' "$undocumented" | sed 's/^/          /'
fi

section "Deprecation inventory"
deprecated=$(awk '/pt_deprecated\(/ {
        getline
        if (match($0, /pt_[a-z0-9_]*\(/)) print substr($0, RSTART, RLENGTH - 1)
    }' include/pt/*.h | sort -u)
if [ -z "$deprecated" ]; then
    pass "nothing is deprecated; the removal window is closed"
else
    for name in $deprecated; do
        if grep -q "$name" docs/api-policy.md && grep -q "$name" docs/release.md; then
            pass "$name is documented in the policy and the release notes"
        else
            problem "$name is deprecated in the headers but not tracked in api-policy.md and release.md"
        fi
        for binding in bindings/cpp/pt/pt.hpp bindings/python/pt.py bindings/java/src/pt/Pt.java; do
            if grep -q "\b$name\b" "$binding"; then
                continue
            fi
            problem "$name is not wrapped by $binding"
        done
    done
    open_item "the entries above must be removed by the 1.0 freeze candidate, not shipped into 1.0.0"
fi

section "Examples"
for example in examples/c/combobox.c examples/cpp/combobox.cpp examples/python/combobox.py \
    examples/java/Combobox.java; do
    if [ -f "$example" ]; then
        pass "$example exists"
    else
        problem "$example is missing"
    fi
done
for target in run-combobox run-cpp-combobox run-python-combobox run-java-combobox; do
    if grep -q "^$target:" Makefile; then
        pass "Makefile defines $target"
    else
        problem "Makefile does not define $target"
    fi
done

section "Bilingual documentation"
if python3 tools/check-bilingual-docs.py; then
    pass "all public guides have Simplified Chinese counterparts"
else
    problem "the bilingual documentation check failed"
fi

section "Items this script cannot decide"
if git -C "$root" rev-parse --verify -q HEAD >/dev/null 2>&1 &&
    git -C "$root" rev-parse -q --verify "refs/tags/$version" >/dev/null 2>&1; then
    pass "freeze commit and annotated tag $version are present"
else
    open_item "freeze commit and annotated tag: left to the repository owner"
fi
if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    open_item "live compositor present: run make check-wayland, make check-typing and the example smoke test"
else
    open_item "no WAYLAND_DISPLAY: the live compositor, typing and example smoke tests still need a session"
fi

printf '\n'
if [ "$failures" -gt 0 ]; then
    printf 'release check: %s failure(s), %s open item(s) for Pt %s\n' \
        "$failures" "$opens" "$version"
    exit 1
fi
printf 'release check: automated gates pass for Pt %s, %s open item(s) left for a human\n' \
    "$version" "$opens"

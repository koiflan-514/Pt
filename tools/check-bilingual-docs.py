#!/usr/bin/env python3
"""Check that the public guides have complete Simplified Chinese counterparts.

The check is intentionally structural: a translator may phrase a paragraph
differently, but code, headings, tables and local links have to stay in sync.
"""
from __future__ import annotations

import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[1]
pairs = [
    ("README.md", "README.zh-CN.md"),
    ("docs/quickstart.md", "docs/zh-CN/quickstart.md"),
    ("docs/architecture.md", "docs/zh-CN/architecture.md"),
    ("docs/accessibility.md", "docs/zh-CN/accessibility.md"),
    ("docs/rtl.md", "docs/zh-CN/rtl.md"),
    ("docs/theming.md", "docs/zh-CN/theming.md"),
    ("docs/bindings.md", "docs/zh-CN/bindings.md"),
    ("docs/comparison.md", "docs/zh-CN/comparison.md"),
    ("docs/migration-1.0.md", "docs/zh-CN/migration-1.0.md"),
]

fence_re = re.compile(r"^( {0,3})(```+|~~~+).*?^\1\2\s*$", re.M | re.S)
heading_re = re.compile(r"^(#{1,6})\s+", re.M)
link_re = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def blocks(text: str) -> list[str]:
    return [match.group(0) for match in fence_re.finditer(text)]


def table_rows(text: str) -> int:
    return sum(1 for line in text.splitlines() if line.lstrip().startswith("|"))


def check_links(name: str, text: str, errors: list[str]) -> None:
    path = root / name
    without_code = fence_re.sub("", text)
    for target in link_re.findall(without_code):
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        target = target.split("#", 1)[0]
        if target and not (path.parent / target).resolve().exists():
            errors.append(f"{name}: broken local link {target!r}")


errors: list[str] = []
for english_name, chinese_name in pairs:
    english_path = root / english_name
    chinese_path = root / chinese_name
    if not chinese_path.exists():
        errors.append(f"{chinese_name}: missing")
        continue
    english = english_path.read_text(encoding="utf-8")
    chinese = chinese_path.read_text(encoding="utf-8")

    header = "\n".join(chinese.splitlines()[:8])
    if "简体中文" not in header:
        errors.append(f"{chinese_name}: missing Simplified Chinese language header")
    if "简体中文" not in "\n".join(english.splitlines()[:8]):
        errors.append(f"{english_name}: missing Chinese language link")

    if blocks(english) != blocks(chinese):
        errors.append(f"{chinese_name}: fenced code differs from {english_name}")

    english_headings = [len(m.group(1)) for m in heading_re.finditer(english)]
    chinese_headings = [len(m.group(1)) for m in heading_re.finditer(chinese)]
    if english_headings != chinese_headings:
        errors.append(f"{chinese_name}: heading levels differ from {english_name}")

    english_tables = table_rows(fence_re.sub("", english))
    chinese_tables = table_rows(fence_re.sub("", chinese))
    if english_tables != chinese_tables:
        errors.append(
            f"{chinese_name}: table rows {chinese_tables} != {english_tables}"
        )

    check_links(english_name, english, errors)
    check_links(chinese_name, chinese, errors)

    print(
        f"{chinese_name}: {len(blocks(chinese))} code blocks, "
        f"{len(chinese_headings)} headings, {chinese_tables} table rows"
    )

if errors:
    print("\nbilingual documentation check failed:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)
print(f"bilingual documentation ok ({len(pairs)} public guides)")

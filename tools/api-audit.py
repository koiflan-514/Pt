#!/usr/bin/env python3
"""Audit the Pt public C API, shared-library exports and binding parity.

The public headers are the source of truth. The audit performs four checks:

1. Every pt_api function is exported by build/libpt.so and no other pt_*
   function is exported.
2. The current API surface matches api/pt.api. Removed or changed entries are
   errors; additions are also errors until the baseline is refreshed with
   --update after review.
3. Every function introduced for the 0.6 table/tree/navigation/interaction
   surface, the 0.7 data-view/accessibility/input surface, the 0.8 streamed
   drag/styled-text surface, the 0.9 token/chip/editing surface and the 0.10
   combobox/selection surface is wrapped by all three language bindings.
4. Binding parity is a hard gate: every public function must be wrapped by
   C++, Python and Java, except entries listed in BINDING_ALLOWLIST after
   review as intentionally C-only. The report still lists per-language
   coverage and missing wrappers so gaps are visible in CI.

The report is written to build/api-report.txt by default.
"""

from __future__ import annotations

import argparse
import ast
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FUNCTION_RE = re.compile(
    r"\bpt_api\b\s*(?:pt_deprecated\s*\([^)]*\)\s*)?"
    r"(?P<decl>[^;{}]*?)\b(?P<name>pt_[A-Za-z0-9_]+)\s*"
    r"\((?P<args>[^;{}]*?)\)\s*;",
    re.S,
)
ENUM_RE = re.compile(
    r"typedef\s+enum\s+(?P<tag>pt_[A-Za-z0-9_]+)\s*\{(?P<body>[^}]*)\}\s*"
    r"(?P<name>pt_[A-Za-z0-9_]+)\s*;",
    re.S,
)
STRUCT_RE = re.compile(
    r"typedef\s+struct\s+(?P<tag>pt_[A-Za-z0-9_]+)\s*\{(?P<body>[^}]*)\}\s*"
    r"(?P<name>pt_[A-Za-z0-9_]+)\s*;",
    re.S,
)
OPAQUE_RE = re.compile(r"typedef\s+struct\s+(pt_[A-Za-z0-9_]+)\s+(pt_[A-Za-z0-9_]+)\s*;")
CALLBACK_RE = re.compile(
    r"typedef\s+(?P<ret>[A-Za-z_][A-Za-z0-9_\s\*]*?)\s*\(\s*\*\s*"
    r"(?P<name>pt_[A-Za-z0-9_]+)\s*\)\s*\((?P<args>[^;]*)\)\s*;",
    re.S,
)
ALIAS_RE = re.compile(
    r"typedef\s+(?!enum\b|struct\b)(?P<decl>[^;{}()]+?)\s+"
    r"(?P<name>pt_[A-Za-z0-9_]+)\s*;"
)
DEFINE_RE = re.compile(r"^#define\s+(pt_[A-Za-z0-9_]+)\s+(\S.*?)\s*$", re.M)

ROUND_06_FUNCTIONS = {
    "pt_widget_set_icon",
    "pt_widget_get_icon",
    "pt_window_set_drag_image",
    "pt_window_clear_drag_image",
    "pt_window_set_stylus_callback",
}
ROUND_06_PREFIXES = (
    "pt_icon_",
    "pt_table_",
    "pt_tree_",
    "pt_tabs_",
    "pt_sidebar_",
    "pt_uri_list_",
)

# 0.7 additions are listed exactly: groups such as pt_table_ already contain
# frozen 0.6 functions, so prefixes would overstate the new surface.
ROUND_07_FUNCTIONS = {
    "pt_window_get_drag_active",
    "pt_window_cancel_drag",
    "pt_window_set_drag_timeout",
    "pt_window_get_drag_timeout",
    "pt_list_view_refresh_rows",
    "pt_list_view_set_row_key_callback",
    "pt_list_view_set_row_text_callback",
    "pt_list_view_get_selected_key",
    "pt_list_view_set_selected_key",
    "pt_table_set_column_hidden",
    "pt_table_get_column_hidden",
    "pt_table_set_on_sort_columns",
    "pt_table_set_sort_columns",
    "pt_table_get_sort_columns",
    "pt_table_set_row_key_callback",
    "pt_table_set_row_text_callback",
    "pt_table_refresh_rows",
    "pt_table_get_selected_key",
    "pt_table_set_selected_key",
}

# 0.8 additions are listed exactly: streamed offers reuse the pt_drop_ and
# pt_window_ prefixes owned by earlier rounds, so prefixes would overstate the
# new surface.
ROUND_08_FUNCTIONS = {
    "pt_drop_offer_mime_count",
    "pt_drop_offer_mime",
    "pt_drop_offer_has_mime",
    "pt_drop_offer_selected_mime",
    "pt_drop_offer_actions",
    "pt_drop_offer_read",
    "pt_drop_stream_cancel",
    "pt_drop_stream_mime",
    "pt_drop_stream_size",
    "pt_drop_stream_active",
    "pt_widget_set_text_spans",
    "pt_widget_clear_text_spans",
    "pt_widget_get_text_span_count",
    "pt_widget_get_text_span",
    "pt_widget_set_input_purpose",
    "pt_widget_get_input_purpose",
    "pt_window_begin_drag",
    "pt_window_get_drag_result",
    "pt_window_set_drag_finished_callback",
}

# 0.9 additions: semantic tokens and window overrides, chip and skeleton,
# busy/invalid states, table cell editing, marquee selection, column reorder
# and row drag, and tree renaming.
ROUND_09_FUNCTIONS = {
    "pt_app_get_theme_metric",
    "pt_app_set_theme_metric",
    "pt_window_get_theme_color",
    "pt_window_set_theme_color",
    "pt_window_get_theme_metric",
    "pt_window_set_theme_metric",
    "pt_window_clear_theme_overrides",
    "pt_chip_create",
    "pt_chip_set_icon",
    "pt_chip_get_selected",
    "pt_chip_set_selected",
    "pt_chip_get_removable",
    "pt_chip_set_removable",
    "pt_chip_set_on_click",
    "pt_chip_set_on_remove",
    "pt_skeleton_create",
    "pt_widget_set_busy",
    "pt_widget_get_busy",
    "pt_widget_set_invalid",
    "pt_widget_get_invalid",
    "pt_table_set_editable",
    "pt_table_get_editable",
    "pt_table_begin_edit",
    "pt_table_commit_edit",
    "pt_table_cancel_edit",
    "pt_table_is_editing",
    "pt_table_set_on_cell_edited",
    "pt_table_get_selection_count",
    "pt_table_get_selection",
    "pt_table_is_row_selected",
    "pt_table_select_range",
    "pt_table_clear_selection",
    "pt_table_set_columns_reorderable",
    "pt_table_get_columns_reorderable",
    "pt_table_move_column",
    "pt_table_set_on_columns_reordered",
    "pt_table_set_on_row_drag",
    "pt_tree_set_renamable",
    "pt_tree_get_renamable",
    "pt_tree_begin_rename",
    "pt_tree_commit_rename",
    "pt_tree_cancel_rename",
    "pt_tree_is_renaming",
    "pt_tree_set_on_rename",
}

# 0.10 additions: the combobox widget and multi-row selection parity for the
# list, table and tree views.
ROUND_10_FUNCTIONS = {
    "pt_combobox_create",
    "pt_combobox_add_item",
    "pt_combobox_insert_item",
    "pt_combobox_remove_item",
    "pt_combobox_clear",
    "pt_combobox_get_item_count",
    "pt_combobox_get_item_text",
    "pt_combobox_set_item_text",
    "pt_combobox_set_item_data",
    "pt_combobox_get_item_data",
    "pt_combobox_set_item_disabled",
    "pt_combobox_get_item_disabled",
    "pt_combobox_set_mode",
    "pt_combobox_get_mode",
    "pt_combobox_get_selected",
    "pt_combobox_set_selected",
    "pt_combobox_get_text",
    "pt_combobox_set_text",
    "pt_combobox_get_placeholder",
    "pt_combobox_set_placeholder",
    "pt_combobox_open",
    "pt_combobox_close",
    "pt_combobox_is_open",
    "pt_combobox_set_popup_max_height",
    "pt_combobox_get_popup_max_height",
    "pt_combobox_set_on_change",
    "pt_combobox_set_on_text_change",
    "pt_list_view_get_selection_count",
    "pt_list_view_get_selection",
    "pt_list_view_is_row_selected",
    "pt_list_view_select_range",
    "pt_list_view_select_toggle",
    "pt_list_view_clear_selection",
    "pt_table_select_toggle",
    "pt_tree_get_selection_count",
    "pt_tree_get_selection",
    "pt_tree_is_selected",
    "pt_tree_select_range",
    "pt_tree_select_toggle",
    "pt_tree_clear_selection",
}

# Functions reviewed as intentionally C-only. Keep empty unless a binding
# cannot express the signature without a runtime bridge.
BINDING_ALLOWLIST: dict[str, set[str]] = {
    "cpp": set(),
    "python": set(),
    "java": set(),
}

FROZEN_05_DATA_VIEW = (
    "pt_list_view_create",
    "pt_list_view_set_row_count",
    "pt_list_view_get_row_count",
    "pt_list_view_set_row_height",
    "pt_list_view_get_row_height",
    "pt_list_view_set_on_bind_row",
    "pt_list_view_set_on_select",
    "pt_list_view_set_on_activate",
    "pt_list_view_get_selected",
    "pt_list_view_set_selected",
    "pt_list_view_refresh",
    "pt_list_view_scroll_to_row",
)

FROZEN_05_MOTION = (
    "pt_ease",
    "pt_ease_value",
    "pt_transition_init",
    "pt_transition_to",
    "pt_transition_advance",
    "pt_transition_set",
    "pt_transition_active",
    "pt_app_set_reduced_motion",
    "pt_app_get_reduced_motion",
)

BINDINGS = {
    "cpp": ("bindings/cpp/pt/pt.hpp",),
    "python": ("bindings/python/pt.py",),
    "java": ("bindings/java/src/pt/Pt.java",),
}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def squash(text: str) -> str:
    return re.sub(r"\s+", "", text)


def split_top_level(text: str, separator: str = ",") -> list[str]:
    parts: list[str] = []
    depth = 0
    current: list[str] = []
    for char in text:
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        if char == separator and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    parts.append("".join(current))
    return parts


def safe_eval(expression: str) -> int | float:
    value = expression.strip()
    value = re.sub(r"(?<=\d)[uUlL]+\b", "", value)
    value = re.sub(r"(?<=\d)[fF]\b", "", value)
    value = re.sub(
        r"\(\s*(?:u?int(?:8|16|32|64)_t|size_t|pt_[A-Za-z0-9_]+)\s*\)", "", value
    )
    node = ast.parse(value, mode="eval")
    allowed = (
        ast.Expression,
        ast.Constant,
        ast.BinOp,
        ast.UnaryOp,
        ast.Load,
        ast.Add,
        ast.Sub,
        ast.Mult,
        ast.Div,
        ast.FloorDiv,
        ast.BitOr,
        ast.BitAnd,
        ast.BitXor,
        ast.LShift,
        ast.RShift,
        ast.USub,
        ast.UAdd,
        ast.Invert,
    )
    for child in ast.walk(node):
        if not isinstance(child, allowed):
            raise ValueError(f"unsupported C expression: {expression!r}")
    return eval(compile(node, "<api-expression>", "eval"), {"__builtins__": {}}, {})


def format_value(value: int | float) -> str:
    if isinstance(value, float):
        return repr(value)
    return str(value)


def normalize_declaration(declaration: str) -> str:
    text = re.sub(r"\s+", " ", declaration).strip()
    text = re.sub(r"\s*\*\s*", "*", text)
    return text


def parse_headers(header_dir: pathlib.Path) -> tuple[dict[str, str], set[str]]:
    surface: dict[str, str] = {}
    functions: set[str] = set()
    for header in sorted(header_dir.glob("*.h")):
        raw = strip_comments(header.read_text(encoding="utf-8"))
        for match in FUNCTION_RE.finditer(raw):
            name = match.group("name")
            ret = normalize_declaration(match.group("decl"))
            args = normalize_declaration(match.group("args"))
            signature = f"{ret} {name}({args})"
            surface[f"func {name}"] = f"func {name} {squash(signature)}"
            functions.add(name)

        for match in ENUM_RE.finditer(raw):
            previous = -1
            for member in split_top_level(match.group("body")):
                member = member.strip()
                if not member:
                    continue
                if "=" in member:
                    name, expression = member.split("=", 1)
                    name = name.strip()
                    value = safe_eval(expression)
                else:
                    name = member
                    value = previous + 1
                previous = value
                surface[f"enum {name}"] = f"enum {name} = {format_value(value)}"

        for match in CALLBACK_RE.finditer(raw):
            name = match.group("name")
            ret = normalize_declaration(match.group("ret"))
            args = normalize_declaration(match.group("args"))
            surface[f"callback {name}"] = (
                f"callback {name} {squash(f'{ret}({args})')}"
            )

        for match in STRUCT_RE.finditer(raw):
            tag = match.group("tag")
            fields = []
            for field in match.group("body").split(";"):
                field = field.strip()
                if field:
                    fields.append(squash(normalize_declaration(field)))
            surface[f"struct {tag}"] = f"struct {tag} {'|'.join(fields)}"

        for match in OPAQUE_RE.finditer(raw):
            name = match.group(1)
            surface[f"opaque {name}"] = f"opaque {name}"

        for match in ALIAS_RE.finditer(raw):
            name = match.group("name")
            declaration = match.group("decl")
            surface[f"type {name}"] = (
                f"type {name} {squash(normalize_declaration(declaration))}"
            )

        for match in DEFINE_RE.finditer(raw):
            name = match.group(1)
            value = match.group(2)
            if name.startswith("pt_version_"):
                continue
            if "(" in value and not value.strip().startswith("("):
                continue
            try:
                number = safe_eval(value)
            except (ValueError, SyntaxError):
                continue
            surface[f"define {name}"] = f"define {name} = {format_value(number)}"
    functions.discard("")
    return surface, functions


def exported_functions(library: pathlib.Path) -> set[str]:
    output = subprocess.run(
        ["nm", "-D", "--defined-only", str(library)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    symbols: set[str] = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2].startswith("pt_") and parts[1] in "TtWw":
            symbols.add(parts[2])
    return symbols


def baseline_lines(header_dir: pathlib.Path) -> list[str]:
    surface, _ = parse_headers(header_dir)
    lines = [
        "# Pt public API baseline. Generated by tools/api-audit.py --update.",
        "# Reviewed entries freeze the C names, signatures, enum values and",
        "# public struct layouts. Do not edit by hand.",
    ]
    lines.extend(sorted(surface.values()))
    return lines


def load_baseline(path: pathlib.Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    if not path.exists():
        return entries
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split(maxsplit=2)
        if len(parts) < 2:
            continue
        key = f"{parts[0]} {parts[1]}"
        entries[key] = line
    return entries


def binding_source(paths: tuple[str, ...]) -> str:
    text = ""
    for path in paths:
        file = ROOT / path
        if file.exists():
            text += strip_comments(file.read_text(encoding="utf-8"))
    return text


def binding_coverage(functions: set[str]) -> dict[str, list[str]]:
    coverage: dict[str, list[str]] = {}
    for language, paths in BINDINGS.items():
        source = binding_source(paths)
        coverage[language] = [
            name for name in sorted(functions) if not re.search(rf"\b{re.escape(name)}\b", source)
        ]
    return coverage


def is_round_06(name: str) -> bool:
    return name in ROUND_06_FUNCTIONS or name.startswith(ROUND_06_PREFIXES)


def is_round_07(name: str) -> bool:
    return name in ROUND_07_FUNCTIONS


def is_round_08(name: str) -> bool:
    return name in ROUND_08_FUNCTIONS


def is_round_09(name: str) -> bool:
    return name in ROUND_09_FUNCTIONS


def is_round_10(name: str) -> bool:
    return name in ROUND_10_FUNCTIONS


def build_report(
    header_dir: pathlib.Path,
    baseline_path: pathlib.Path,
    library: pathlib.Path,
) -> tuple[str, list[str]]:
    surface, functions = parse_headers(header_dir)
    baseline = load_baseline(baseline_path)
    errors: list[str] = []

    exported = exported_functions(library)
    missing_exports = sorted(functions - exported)
    unexpected_exports = sorted(exported - functions)
    if missing_exports:
        errors.append("declared but not exported: " + ", ".join(missing_exports))
    if unexpected_exports:
        errors.append("exported but not declared: " + ", ".join(unexpected_exports))

    removed = sorted(set(baseline) - set(surface))
    added = sorted(set(surface) - set(baseline))
    changed = sorted(
        key for key in set(surface) & set(baseline) if surface[key] != baseline[key]
    )
    if removed:
        errors.append("removed from baseline: " + ", ".join(removed))
    if changed:
        errors.append("changed from baseline: " + ", ".join(changed))
    if added:
        errors.append(
            "not in baseline (review, then run make api-update): " + ", ".join(added)
        )

    coverage = binding_coverage(functions)
    parity_gaps = {
        language: [
            name for name in missing if name not in BINDING_ALLOWLIST[language]
        ]
        for language, missing in coverage.items()
    }
    for language, missing in parity_gaps.items():
        if missing:
            errors.append(
                f"binding parity: {len(missing)} function(s) missing from "
                f"{language}: " + ", ".join(missing)
            )

    round06 = sorted(name for name in functions if is_round_06(name))
    round06_gaps = {
        language: [name for name in round06 if name in missing]
        for language, missing in coverage.items()
    }
    round07 = sorted(name for name in functions if is_round_07(name))
    round07_missing = sorted(ROUND_07_FUNCTIONS - functions)
    round07_gaps = {
        language: [name for name in round07 if name in missing]
        for language, missing in coverage.items()
    }
    if round07_missing:
        errors.append("0.7 surface missing from headers: " + ", ".join(round07_missing))

    round08 = sorted(name for name in functions if is_round_08(name))
    round08_missing = sorted(ROUND_08_FUNCTIONS - functions)
    round08_gaps = {
        language: [name for name in round08 if name in missing]
        for language, missing in coverage.items()
    }
    if round08_missing:
        errors.append("0.8 surface missing from headers: " + ", ".join(round08_missing))

    round09 = sorted(name for name in functions if is_round_09(name))
    round09_missing = sorted(ROUND_09_FUNCTIONS - functions)
    round09_gaps = {
        language: [name for name in round09 if name in missing]
        for language, missing in coverage.items()
    }
    if round09_missing:
        errors.append("0.9 surface missing from headers: " + ", ".join(round09_missing))

    round10 = sorted(name for name in functions if is_round_10(name))
    round10_missing = sorted(ROUND_10_FUNCTIONS - functions)
    round10_gaps = {
        language: [name for name in round10 if name in missing]
        for language, missing in coverage.items()
    }
    if round10_missing:
        errors.append("0.10 surface missing from headers: " + ", ".join(round10_missing))

    frozen = FROZEN_05_DATA_VIEW + FROZEN_05_MOTION
    frozen_missing = [name for name in frozen if name not in functions]
    if frozen_missing:
        errors.append("frozen 0.5 API missing: " + ", ".join(frozen_missing))

    enum_count = sum(1 for key in surface if key.startswith("enum "))
    callback_count = sum(1 for key in surface if key.startswith("callback "))
    struct_count = sum(1 for key in surface if key.startswith("struct "))
    lines = [
        "Pt API audit",
        f"headers: {header_dir}",
        f"library: {library}",
        f"surface: {len(functions)} functions, {enum_count} enum constants, "
        f"{callback_count} callback typedefs, {struct_count} public structs, "
        f"{len(surface)} frozen entries",
        "",
        f"exports: {len(exported)} pt_* symbols; "
        f"{len(missing_exports)} missing, {len(unexpected_exports)} unexpected",
        f"baseline: {len(baseline)} entries; {len(removed)} removed, "
        f"{len(changed)} changed, {len(added)} added",
        "",
        "frozen 0.5 data-view and motion API:",
    ]
    for name in frozen:
        lines.append(f"  {'present' if name in functions else 'MISSING'}  {name}")
    lines.append("")
    lines.append("binding parity (hard gate):")
    for language, missing in coverage.items():
        covered = len(functions) - len(missing)
        percentage = 100.0 * covered / len(functions) if functions else 100.0
        allowed = len(BINDING_ALLOWLIST[language])
        lines.append(
            f"  {language:6s} {covered:3d}/{len(functions)} "
            f"({percentage:5.1f}%), {len(missing)} missing, "
            f"{allowed} allowlisted"
        )
    lines.append(
        f"  0.6 surface: {len(round06)} functions, "
        + ", ".join(
            f"{language} {'complete' if not missing else str(len(missing)) + ' missing'}"
            for language, missing in round06_gaps.items()
        )
    )
    lines.append(
        f"  0.7 surface: {len(round07)} functions, "
        + ", ".join(
            f"{language} {'complete' if not missing else str(len(missing)) + ' missing'}"
            for language, missing in round07_gaps.items()
        )
    )
    lines.append(
        f"  0.8 surface: {len(round08)} functions, "
        + ", ".join(
            f"{language} {'complete' if not missing else str(len(missing)) + ' missing'}"
            for language, missing in round08_gaps.items()
        )
    )
    lines.append(
        f"  0.9 surface: {len(round09)} functions, "
        + ", ".join(
            f"{language} {'complete' if not missing else str(len(missing)) + ' missing'}"
            for language, missing in round09_gaps.items()
        )
    )
    lines.append(
        f"  0.10 surface: {len(round10)} functions, "
        + ", ".join(
            f"{language} {'complete' if not missing else str(len(missing)) + ' missing'}"
            for language, missing in round10_gaps.items()
        )
    )
    for language, missing in coverage.items():
        if missing:
            lines.append(f"  missing in {language}: " + ", ".join(missing))
    lines.append("")
    if errors:
        lines.append("result: FAIL")
        lines.extend(f"  error: {error}" for error in errors)
    else:
        lines.append("result: OK")
    return "\n".join(lines) + "\n", errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headers", default=str(ROOT / "include" / "pt"))
    parser.add_argument("--baseline", default=str(ROOT / "api" / "pt.api"))
    parser.add_argument("--library", default=str(ROOT / "build" / "libpt.so"))
    parser.add_argument("--report", default=str(ROOT / "build" / "api-report.txt"))
    parser.add_argument("--update", action="store_true", help="refresh the baseline")
    arguments = parser.parse_args()

    header_dir = pathlib.Path(arguments.headers)
    baseline_path = pathlib.Path(arguments.baseline)
    library = pathlib.Path(arguments.library)

    if not library.exists():
        print(f"api-audit: library not found: {library}", file=sys.stderr)
        return 2

    if arguments.update:
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_path.write_text("\n".join(baseline_lines(header_dir)) + "\n",
                                 encoding="utf-8")
        print(f"api-audit: wrote {baseline_path}")

    report, errors = build_report(header_dir, baseline_path, library)
    report_path = pathlib.Path(arguments.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(report, encoding="utf-8")
    print(report, end="")
    print(f"report: {report_path}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())

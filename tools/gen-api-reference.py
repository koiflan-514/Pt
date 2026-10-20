#!/usr/bin/env python3
"""Generate the Pt C API reference from the public headers.

The generator is authoritative: docs/api-reference.md is checked in so it can
be read offline, and `--check` verifies that it matches the headers exactly.
Each function entry carries its signature, the header documentation, and
generated contract notes for ownership, valid objects, callbacks and failure
behavior. Literal enums, callback typedefs, public structs and macros are
collected in an appendix.

Run `make api-docs` after touching a public header or its comments.
"""

from __future__ import annotations

import argparse
import difflib
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "api-reference.md"

MODULES = (
    ("base.h", "Core types, errors and memory"),
    ("theme.h", "Theme tokens and helper functions"),
    ("app.h", "Application lifecycle, timers and preference"),
    ("window.h", "Windows, clipboard and input handlers"),
    ("widget.h", "Widgets, containers and data views"),
    ("overlay.h", "Menus, dialogs and toasts"),
    ("a11y.h", "Accessibility model and bridge"),
    ("dnd.h", "External drag and drop"),
    ("interaction.h", "Gestures, momentum and stylus input"),
    ("motion.h", "Easing and transitions"),
)

KIND_BY_PREFIX = (
    ("pt_list_view_", "`pt_widget_list`"),
    ("pt_table_", "`pt_widget_table`"),
    ("pt_tree_node_", "a node handle returned by `pt_tree`"),
    ("pt_tree_", "`pt_widget_tree`"),
    ("pt_tabs_", "`pt_widget_tabs`"),
    ("pt_sidebar_", "`pt_widget_sidebar`"),
    ("pt_segmented_", "`pt_widget_segmented`"),
    ("pt_text_area_", "`pt_widget_text_area`"),
    ("pt_entry_", "`pt_widget_entry`"),
    ("pt_button_", "`pt_widget_button`"),
    ("pt_toggle_", "`pt_widget_toggle`"),
    ("pt_slider_", "`pt_widget_slider`"),
    ("pt_progress_", "`pt_widget_progress`"),
    ("pt_scroll_", "`pt_widget_scroll`"),
    ("pt_grid_", "`pt_widget_grid`"),
    ("pt_box_", "`pt_widget_box`"),
)

WIDGET_CREATORS = {
    "pt_box_create",
    "pt_row_create",
    "pt_column_create",
    "pt_label_create",
    "pt_button_create",
    "pt_entry_create",
    "pt_toggle_create",
    "pt_slider_create",
    "pt_scroll_create",
    "pt_divider_create",
    "pt_card_create",
    "pt_spacer_create",
    "pt_checkbox_create",
    "pt_radio_create",
    "pt_progress_create",
    "pt_spinner_create",
    "pt_segmented_create",
    "pt_text_area_create",
    "pt_wrap_create",
    "pt_grid_create",
    "pt_list_view_create",
    "pt_icon_create",
    "pt_icon_button_create",
    "pt_icon_button_create",
    "pt_table_create",
    "pt_tree_create",
    "pt_tabs_create",
    "pt_sidebar_create",
}

BORROWED_POINTERS = {
    "pt_widget_parent": "the parent widget; it is owned by its own parent",
    "pt_window_root": "the window's root widget; the window owns it",
    "pt_window_app": "the application that owns the window",
    "pt_widget_get_text": "NUL-terminated UTF-8 owned by the widget",
    "pt_widget_get_a11y_name": "UTF-8 owned by the widget",
    "pt_widget_get_a11y_description": "UTF-8 owned by the widget",
    "pt_tree_node_get_text": "UTF-8 owned by the tree node",
    "pt_tree_node_get_data": "the application pointer registered on the node",
    "pt_tree_node_parent": "the parent node, or NULL for a root",
    "pt_tree_node_get_child": "the child node; the tree owns it",
    "pt_tree_add_node": "the new node; the tree owns it",
    "pt_tree_get_root_node": "the root node; the tree owns it",
    "pt_tree_get_visible_node": "the visible node; the tree owns it",
    "pt_tree_get_selected": "the selected node; the tree owns it",
    "pt_tabs_add_page": "the new page widget; the tab control owns it",
    "pt_tabs_get_page": "the page widget; the tab control owns it",
    "pt_tabs_get_page_title": "UTF-8 owned by the tab control",
    "pt_sidebar_add_item": "the new item widget; the sidebar owns it",
    "pt_sidebar_get_item": "the item widget; the sidebar owns it",
    "pt_sidebar_get_header": "UTF-8 owned by the sidebar",
    "pt_get_last_error": "thread-local UTF-8 valid until the next Pt call on the thread",
}

OWNED_COPIES = {
    "pt_window_clipboard_get": "the caller owns the returned string and releases it with `pt_free`",
    "pt_uri_list_get": "the caller owns the returned string and releases it with `pt_free`",
}


def load_audit():
    path = ROOT / "tools" / "api-audit.py"
    spec = importlib.util.spec_from_file_location("pt_api_audit", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


AUDIT = load_audit()


def clean_comment(block: str) -> str:
    body = block[2:-2]
    lines = []
    for line in body.splitlines():
        line = re.sub(r"^\s*\* ?", "", line)
        lines.append(line.rstrip())
    text = "\n".join(lines).strip()
    return re.sub(r"\n{3,}", "\n\n", text)


CONTRACT_RE = re.compile(r"/\* Header contract(?P<body>.*?)\*/", re.S)


def preceding_comment(raw: str, start: int) -> str:
    prefix = raw[:start]
    blocks = list(re.finditer(r"/\*.*?\*/", prefix, re.S))
    if not blocks:
        return ""
    block = blocks[-1]
    between = prefix[block.end() :]
    if between.strip():
        return ""
    text = clean_comment(block.group(0))
    if "Copyright" in text or text.startswith("Header contract"):
        return ""
    return text


def header_contract(raw: str) -> str:
    """Return the module-level contract block, without its marker line."""
    match = CONTRACT_RE.search(raw)
    if not match:
        return ""
    text = clean_comment("/* Header contract" + match.group("body") + "*/")
    lines = text.splitlines()
    if lines and lines[0].strip() == "Header contract":
        lines = lines[1:]
    return "\n".join(line.rstrip() for line in lines).strip()


def normalize_space(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def anchor(filename: str) -> str:
    return re.sub(r"[^a-z0-9_-]", "", filename.lower())


def signature(match: re.Match[str]) -> str:
    ret = normalize_space(match.group("decl"))
    args = normalize_space(match.group("args")) or "void"
    return f"{ret} {match.group('name')}({args})"


def widget_kind(name: str) -> str:
    if name in WIDGET_CREATORS or name.endswith("_create"):
        return ""
    if name == "pt_icon_set_size" or name == "pt_icon_get_size":
        return "`pt_widget_icon`"
    for prefix, kind in KIND_BY_PREFIX:
        if name.startswith(prefix):
            return kind
    return ""


def callback_note(name: str) -> str:
    if name == "pt_app_add_timeout":
        return (
            "The callback is one-shot and runs on the UI thread when the timer "
            "expires; `user_data` must remain valid until it fires or the timer "
            "is cancelled."
        )
    if name == "pt_widget_set_drop_target":
        return (
            "The target structure is copied. Its callbacks run on the UI thread "
            "during a drag; `user_data` must stay valid until the target is "
            "replaced or cleared."
        )
    if "_set_on_" in name or name.endswith("_callback") or name.endswith(
        "_set_callback"
    ):
        return (
            "The callback runs on the UI thread while events are dispatched or "
            "a frame is advanced. `user_data` is passed through unchanged and "
            "must remain valid until the callback is replaced or the owning "
            "object is destroyed."
        )
    return ""


def contract_notes(name: str, return_type: str) -> list[str]:
    notes: list[str] = []
    callbacks = callback_note(name)
    if callbacks:
        notes.append(callbacks)

    if name == "pt_app_create":
        notes.append(
            "On success the caller owns the application and must release it with "
            "`pt_app_destroy`. Returns NULL when the compositor connection fails; "
            "call `pt_get_last_error`."
        )
    elif name == "pt_window_create":
        notes.append(
            "The application owns the window and frees it with `pt_app_destroy`. "
            "Returns NULL on failure; call `pt_get_last_error`."
        )
    elif name in WIDGET_CREATORS:
        notes.append(
            "The parent owns the new widget and frees it with its subtree. A NULL "
            "parent creates a detached tree that the caller owns and may release "
            "with `pt_widget_destroy`. Returns NULL on failure; call "
            "`pt_get_last_error`."
        )
    elif name == "pt_menu_create":
        notes.append(
            "The caller owns the menu and must release it with `pt_menu_destroy`. "
            "Returns NULL on failure; call `pt_get_last_error`."
        )
    elif name == "pt_dialog_create":
        notes.append(
            "The caller owns the dialog and must release it with "
            "`pt_dialog_destroy`. Returns NULL on failure; call "
            "`pt_get_last_error`."
        )
    elif name == "pt_window_show_toast":
        notes.append("The window owns the returned toast handle.")
    elif name == "pt_app_destroy":
        notes.append("Destroys every window, overlay and timer owned by the application.")
    elif name.endswith("_destroy") or name.endswith("_clear"):
        notes.append("Releases the object and any owned children; the handle is invalid afterwards.")
    elif name == "pt_free":
        notes.append("Releases caller-owned memory returned by Pt; passing NULL is safe.")

    kind = widget_kind(name)
    if kind:
        notes.append(f"Valid object: {kind}. Other widget kinds are ignored.")

    if name in BORROWED_POINTERS:
        notes.append(f"Borrowed pointer: {BORROWED_POINTERS[name]}. Do not free it.")
    if name in OWNED_COPIES:
        notes.append(f"Ownership: {OWNED_COPIES[name]}. Returns NULL when no value is available.")

    if return_type.startswith("const char *") and name not in BORROWED_POINTERS:
        notes.append("Returned text is borrowed and must not be freed.")
    if return_type == "bool":
        notes.append(
            "Returns false for an invalid object or when the operation or query "
            "does not apply."
        )
    elif return_type == "void":
        if not any(
            marker in name
            for marker in ("_destroy", "_clear", "pt_free")
        ):
            notes.append("No-op for NULL objects or out-of-range values; no error is returned.")
    elif return_type not in ("int", "uint32_t", "uint64_t", "size_t", "float", "pt_color",
                             "pt_a11y_role", "pt_a11y_state", "pt_color_scheme",
                             "pt_popup_policy", "pt_a11y_status", "pt_sort_direction",
                             "pt_widget_kind") and "*" in return_type:
        if name not in BORROWED_POINTERS and name not in OWNED_COPIES:
            notes.append("Returns NULL for invalid arguments or when no value is available.")

    return notes


def public_types() -> list[str]:
    lines: list[str] = ["## Public types", ""]
    lines.append(
        "Types are listed in header order. Enum values are the stable ABI values "
        "recorded in `api/pt.api`."
    )
    lines.append("")
    for filename, _ in MODULES:
        path = ROOT / "include" / "pt" / filename
        raw = path.read_text(encoding="utf-8")
        stripped = AUDIT.strip_comments(raw)
        entries: list[str] = []

        for match in AUDIT.ENUM_RE.finditer(stripped):
            body: list[str] = []
            previous = -1
            for member in AUDIT.split_top_level(match.group("body")):
                member = member.strip()
                if not member:
                    continue
                if "=" in member:
                    member_name, expression = member.split("=", 1)
                    member_name = member_name.strip()
                    raw_value = expression.strip()
                    try:
                        value = AUDIT.safe_eval(expression)
                        raw_value = str(AUDIT.format_value(value))
                    except (ValueError, SyntaxError):
                        value = previous + 1
                    previous = value
                else:
                    member_name = member
                    value = previous + 1
                    previous = value
                    raw_value = str(value)
                body.append(f"`{member_name}` = {raw_value}")
            entries.append(
                f"**`{match.group('name')}`**\n\n" + ", ".join(body)
            )

        for match in AUDIT.CALLBACK_RE.finditer(stripped):
            ret = normalize_space(match.group("ret"))
            args = normalize_space(match.group("args")) or "void"
            entries.append(
                f"**`{match.group('name')}`**\n\n```c\n{ret} (*)({args})\n```"
            )

        for match in AUDIT.STRUCT_RE.finditer(stripped):
            fields = []
            for field in match.group("body").split(";"):
                field = field.strip()
                if field:
                    fields.append(f"    {normalize_space(field)};")
            entries.append(
                f"**`struct {match.group('tag')}`**\n\n```c\ntypedef struct {{\n"
                + "\n".join(fields)
                + f"\n}} {match.group('name')};\n```"
            )

        for match in AUDIT.OPAQUE_RE.finditer(stripped):
            entries.append(f"**`{match.group(1)}`** — opaque handle\n")

        for match in AUDIT.DEFINE_RE.finditer(stripped):
            name = match.group(1)
            if name.startswith("pt_version_"):
                continue
            value = match.group(2)
            if "(" in value and not value.strip().startswith("("):
                continue
            try:
                number = AUDIT.safe_eval(value)
            except (ValueError, SyntaxError):
                continue
            entries.append(f"**`{name}`** = `{AUDIT.format_value(number)}`\n")

        if entries:
            lines.append(f"### {filename}")
            lines.append("")
            lines.extend(entries)
            lines.append("")
    return lines


def build_reference() -> str:
    surface_entries: dict[tuple[str, int], tuple[str, str, str, str]] = {}
    order: list[tuple[str, str]] = []
    for filename, _ in MODULES:
        path = ROOT / "include" / "pt" / filename
        raw = path.read_text(encoding="utf-8")
        for match in AUDIT.FUNCTION_RE.finditer(raw):
            name = match.group("name")
            ret = normalize_space(match.group("decl"))
            doc = preceding_comment(raw, match.start())
            surface_entries[(filename, match.start())] = (name, ret, doc, filename)
            order.append((filename, name))

    functions = {entry[0] for entry in surface_entries.values()}
    lines: list[str] = []
    lines.append("# Pt C API reference")
    lines.append("")
    lines.append(
        "Generated from the public headers in `include/pt` by "
        "`tools/gen-api-reference.py`. Do not edit this file by hand; run "
        "`make api-docs` after changing a declaration or its comments."
    )
    lines.append("")
    base_header = (ROOT / "include" / "pt" / "base.h").read_text(encoding="utf-8")
    version_match = re.search(
        r'#define\s+pt_version_string\s+"([^"]+)"', base_header
    )
    version = version_match.group(1) if version_match else "unknown"
    lines.append(f"Version **{version}** · {len(functions)} public functions")
    lines.append("")
    lines.append("## Conventions")
    lines.append("")
    lines.append(
        "**Threading.** Pt is single-threaded. Every function, callback, timer and "
        "frame must be driven from the thread that created the application. Do not "
        "call the C API from worker threads; marshal work with a timer or an "
        "application-owned queue instead."
    )
    lines.append("")
    lines.append(
        "**Ownership.** Constructors that take a parent append to it and transfer "
        "ownership to the parent's subtree. Functions returning handles either "
        "return a borrowed pointer owned by a container or allocate an object the "
        "caller must destroy; each function entry states which applies. Never free "
        "a borrowed pointer. `pt_free` releases buffers that Pt explicitly hands "
        "to the caller."
    )
    lines.append("")
    lines.append(
        "**Callback threading and lifetimes.** Callbacks are invoked synchronously "
        "on the UI thread during event dispatch, painting or frame advancement. "
        "They may call the API, but must not block. A registered callback and its "
        "`user_data` stay valid until the callback is replaced or the owning object "
        "is destroyed."
    )
    lines.append("")
    lines.append(
        "**Error behavior.** Functions that can fail by creation or lookup return "
        "NULL, false, zero or a documented enum default. `pt_get_last_error` "
        "returns a thread-local message for the most recent failure. Setters are "
        "no-ops for NULL objects, mismatched widget kinds and out-of-range indices; "
        "they do not report errors through return values. Text is UTF-8; text "
        "offsets are byte positions."
    )
    lines.append("")
    lines.append(
        "**Valid widget kinds.** Functions with a kind-specific prefix operate on "
        "the named widget kind and ignore other kinds. Functions prefixed "
        "`pt_widget_` work with any widget unless their description narrows the "
        "set."
    )
    lines.append("")
    lines.append("## Modules")
    lines.append("")
    for filename, summary in MODULES:
        count = sum(1 for entry in order if entry[0] == filename)
        lines.append(f"- [{filename}](#{anchor(filename)}) — {summary} ({count} functions)")
    lines.append("")

    for filename, summary in MODULES:
        module_entries = [
            (start, entry) for start, entry in sorted(surface_entries.items())
            if entry[3] == filename
        ]
        lines.append(f"## {filename}")
        lines.append("")
        lines.append(summary + ".")
        lines.append("")
        contract = header_contract(
            (ROOT / "include" / "pt" / filename).read_text(encoding="utf-8")
        )
        if contract:
            lines.append("**Contract**")
            lines.append("")
            lines.append(contract)
            lines.append("")
        if not module_entries:
            lines.append("This header declares types only.")
            lines.append("")
            continue
        for _, (name, ret, doc, _header) in module_entries:
            lines.append(f"### {name}")
            lines.append("")
            lines.append("```c")
            lines.append(signature_for(filename, name))
            lines.append("```")
            lines.append("")
            lines.append(f"**Header:** [`pt/{filename}`](../include/pt/{filename})")
            lines.append("")
            if doc:
                lines.append(doc)
                lines.append("")
            notes = contract_notes(name, ret)
            if notes:
                lines.append("**Contract**")
                lines.append("")
                lines.extend(f"- {note}" for note in notes)
                lines.append("")

    lines.extend(public_types())
    return "\n".join(lines).rstrip() + "\n"


_SIGNATURES: dict[tuple[str, str], str] = {}


def signature_for(filename: str, name: str) -> str:
    key = (filename, name)
    if key not in _SIGNATURES:
        raw = (ROOT / "include" / "pt" / filename).read_text(encoding="utf-8")
        for match in AUDIT.FUNCTION_RE.finditer(raw):
            if match.group("name") == name:
                _SIGNATURES[key] = signature(match)
                break
    return _SIGNATURES[key]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--write", action="store_true", help="rewrite the reference")
    group.add_argument("--check", action="store_true", help="verify the checked-in reference")
    parser.add_argument("--output", default=str(OUTPUT))
    arguments = parser.parse_args()
    output = pathlib.Path(arguments.output)

    reference = build_reference()
    if arguments.write:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(reference, encoding="utf-8")
        print(f"api-reference: wrote {output}")
        return 0

    if not output.exists():
        print(f"api-reference: missing {output}; run make api-docs", file=sys.stderr)
        return 1
    current = output.read_text(encoding="utf-8")
    if current == reference:
        print(f"api-reference: {output} matches the headers")
        return 0
    diff = list(
        difflib.unified_diff(
            current.splitlines(),
            reference.splitlines(),
            fromfile=str(output),
            tofile="generated",
            lineterm="",
        )
    )
    print("api-reference: generated reference is stale; run make api-docs", file=sys.stderr)
    for line in diff[:80]:
        print(line, file=sys.stderr)
    if len(diff) > 80:
        print(f"... {len(diff) - 80} more diff lines", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

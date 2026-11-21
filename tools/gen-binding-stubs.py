#!/usr/bin/env python3
"""Generate C++/Python/Java binding *skeletons* from the Pt API surface.

The four-language surface is C plus three hand-maintained wrappers. Adding a
function means writing the C declaration, the baseline entry and three wrappers
by hand; the wrappers carry ownership, lifetime and error conventions that a
generator cannot invent. This tool shortens that loop without replacing it:

* ``api/pt.api`` decides which functions are reviewed public API.
* ``include/pt/*.h`` is the signature, callback, enum and struct source.
* The checked-in binding sources are only read, never written. Missing
  functions are the exact set ``tools/api-audit.py`` reports, computed with the
  same comment stripping and name scan.

Every emitted fragment is a review aid: it has the right signature mapping and
the wrapper-specific boilerplate (C++ ownership copies, Python ctypes prototype
and trampoline, Java MethodHandle/upcall/struct layout), and each one carries
``TODO`` notes where a human must confirm ownership, error and callback
lifetime semantics. Fragments are never merged automatically.

Usage:
    tools/gen-binding-stubs.py --language cpp --missing
    tools/gen-binding-stubs.py --language python --missing --out /tmp/stubs
    tools/gen-binding-stubs.py --check                 # report only
    tools/gen-binding-stubs.py --check --strict        # missing means exit 1

Exit codes:
    0  success; missing wrappers are reported but are not a tool failure
    1  --check --strict and at least one wrapper is missing or cannot be
       generated, or generation itself failed
    2  input, usage or unsafe output path error

Output is deterministic: the same tree produces the same bytes, so a preview
directory can be diffed across runs.
"""

from __future__ import annotations

import argparse
import dataclasses
import importlib.util
import pathlib
import re
import sys
from typing import Iterable, Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
LANGUAGES = ("cpp", "python", "java")
EXTENSIONS = {"cpp": "hpp", "python": "py", "java": "java"}


class InputError(Exception):
    """Bad or inconsistent input; exit status 2."""


class UnsupportedSignature(Exception):
    """The signature needs a construct this generator does not model."""


def load_audit():
    path = ROOT / "tools" / "api-audit.py"
    spec = importlib.util.spec_from_file_location("pt_api_audit", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


AUDIT = load_audit()


# --------------------------------------------------------------------------
# API model
# --------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class TypeInfo:
    raw: str
    base: str
    pointers: int
    is_const: bool


@dataclasses.dataclass(frozen=True)
class Param:
    name: str
    type: TypeInfo


@dataclasses.dataclass(frozen=True)
class Function:
    name: str
    return_type: TypeInfo
    params: tuple[Param, ...]
    header: str


@dataclasses.dataclass(frozen=True)
class Callback:
    name: str
    return_type: TypeInfo
    params: tuple[Param, ...]


@dataclasses.dataclass(frozen=True)
class Struct:
    name: str
    tag: str
    fields: tuple[Param, ...]


@dataclasses.dataclass
class Surface:
    functions: dict[str, Function]
    callbacks: dict[str, Callback]
    structs: dict[str, Struct]
    enums: dict[str, dict[str, int]]
    opaque: set[str]
    aliases: dict[str, str]
    defines: dict[str, int | float]


PARAM_RE = re.compile(
    r"^(?P<type>.*?)\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"(?P<array>(?:\[\s*\d*\s*\])*)$",
)


def normalize(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def type_info(raw: str) -> TypeInfo:
    text = normalize(raw)
    pointers = text.count("*")
    base = re.sub(r"\b(const|struct|enum)\b", " ", text)
    base = base.replace("*", " ")
    base = re.sub(r"\[[^\]]*\]", " ", base)
    base = normalize(base)
    if base.endswith(" const"):
        base = base[: -len(" const")].strip()
    return TypeInfo(
        raw=text,
        base=base,
        pointers=pointers,
        is_const="const" in text.split(),
    )


def parse_param(text: str) -> Optional[Param]:
    part = normalize(text)
    if not part or part == "void" or part == "...":
        return None
    match = PARAM_RE.match(part)
    if not match:
        raise InputError(f"cannot parse parameter: {text!r}")
    name = match.group("name")
    type_text = normalize(match.group("type"))
    array = normalize(match.group("array"))
    if array:
        type_text = f"{type_text} {array}"
    if not type_text:
        raise InputError(f"cannot parse parameter type: {text!r}")
    return Param(name=name, type=type_info(type_text))


def parse_params(args: str) -> tuple[Param, ...]:
    text = normalize(args)
    if not text or text == "void":
        return ()
    params: list[Param] = []
    for part in AUDIT.split_top_level(text):
        parsed = parse_param(part)
        if parsed is not None:
            params.append(parsed)
    return tuple(params)


def parse_surface(header_dir: pathlib.Path) -> Surface:
    functions: dict[str, Function] = {}
    callbacks: dict[str, Callback] = {}
    structs: dict[str, Struct] = {}
    enums: dict[str, dict[str, int]] = {}
    opaque: set[str] = set()
    aliases: dict[str, str] = {}
    defines: dict[str, int | float] = {}

    for header in sorted(header_dir.glob("*.h")):
        raw = header.read_text(encoding="utf-8")
        stripped = AUDIT.strip_comments(raw)

        for match in AUDIT.FUNCTION_RE.finditer(stripped):
            name = match.group("name")
            functions[name] = Function(
                name=name,
                return_type=type_info(match.group("decl")),
                params=parse_params(match.group("args")),
                header=header.name,
            )

        for match in AUDIT.CALLBACK_RE.finditer(stripped):
            name = match.group("name")
            callbacks[name] = Callback(
                name=name,
                return_type=type_info(match.group("ret")),
                params=parse_params(match.group("args")),
            )

        for match in AUDIT.STRUCT_RE.finditer(stripped):
            fields: list[Param] = []
            for field in match.group("body").split(";"):
                parsed = parse_param(field)
                if parsed is not None:
                    fields.append(parsed)
            structs[match.group("name")] = Struct(
                name=match.group("name"),
                tag=match.group("tag"),
                fields=tuple(fields),
            )

        for match in AUDIT.ENUM_RE.finditer(stripped):
            members: dict[str, int] = {}
            previous = -1
            for member in AUDIT.split_top_level(match.group("body")):
                member = member.strip()
                if not member:
                    continue
                if "=" in member:
                    member_name, expression = member.split("=", 1)
                    member_name = member_name.strip()
                    try:
                        value = AUDIT.safe_eval(expression)
                    except (ValueError, SyntaxError):
                        value = previous + 1
                else:
                    member_name = member
                    value = previous + 1
                previous = value
                members[member_name] = int(value)
            enums[match.group("name")] = members

        for match in AUDIT.OPAQUE_RE.finditer(stripped):
            opaque.add(match.group(1))
            opaque.add(match.group(2))

        for match in AUDIT.ALIAS_RE.finditer(stripped):
            aliases[match.group("name")] = normalize(match.group("decl"))

        for match in AUDIT.DEFINE_RE.finditer(stripped):
            name = match.group(1)
            value = match.group(2)
            if name.startswith("pt_version_"):
                continue
            if "(" in value and not value.strip().startswith("("):
                continue
            try:
                defines[name] = AUDIT.safe_eval(value)
            except (ValueError, SyntaxError):
                continue

    opaque.discard("")
    return Surface(
        functions=functions,
        callbacks=callbacks,
        structs=structs,
        enums=enums,
        opaque=opaque,
        aliases=aliases,
        defines=defines,
    )


def load_and_verify_baseline(root: pathlib.Path, surface: Surface) -> dict[str, str]:
    """Require api/pt.api and include/pt/*.h to describe the same surface."""
    api_path = root / "api" / "pt.api"
    if not api_path.exists():
        raise InputError(f"baseline not found: {api_path}")
    baseline = AUDIT.load_baseline(api_path)
    header_surface, _functions = AUDIT.parse_headers(root / "include" / "pt")
    if not baseline:
        raise InputError(f"baseline is empty: {api_path}")
    removed = sorted(set(baseline) - set(header_surface))
    added = sorted(set(header_surface) - set(baseline))
    changed = sorted(
        key for key in set(baseline) & set(header_surface)
        if baseline[key] != header_surface[key]
    )
    if removed or added or changed:
        details = []
        if removed:
            details.append("in baseline but not headers: " + ", ".join(removed[:8]))
        if added:
            details.append("in headers but not baseline: " + ", ".join(added[:8]))
        if changed:
            details.append("signature drift: " + ", ".join(changed[:8]))
        raise InputError(
            "api/pt.api and include/pt disagree; run make api-update first ("
            + "; ".join(details) + ")"
        )
    # The rich model and the audit model must agree about function names too.
    if set(surface.functions) != set(header_surface_keys(header_surface)):
        missing = sorted(set(header_surface_keys(header_surface)) - set(surface.functions))
        raise InputError("internal parser disagreement: " + ", ".join(missing[:8]))
    return baseline


def header_surface_keys(header_surface: dict[str, str]) -> set[str]:
    return {
        key.split(maxsplit=1)[1]
        for key in header_surface
        if key.startswith("func ")
    }


def baseline_function_names(baseline: dict[str, str]) -> set[str]:
    return {
        key.split(maxsplit=1)[1]
        for key in baseline
        if key.startswith("func ")
    }


# --------------------------------------------------------------------------
# Missing-function detection (same rule as tools/api-audit.py)
# --------------------------------------------------------------------------


def binding_missing(
    root: pathlib.Path, language: str, functions: Iterable[str]
) -> list[str]:
    text = ""
    for relative in AUDIT.BINDINGS[language]:
        path = root / relative
        if path.exists():
            text += AUDIT.strip_comments(path.read_text(encoding="utf-8"))
    return [
        name
        for name in sorted(functions)
        if not re.search(rf"\b{re.escape(name)}\b", text)
    ]


def binding_has(language: str, token: str, text: str) -> bool:
    return bool(re.search(rf"\b{re.escape(token)}\b", text))


def binding_text(root: pathlib.Path, language: str) -> str:
    text = ""
    for relative in AUDIT.BINDINGS[language]:
        path = root / relative
        if path.exists():
            text += AUDIT.strip_comments(path.read_text(encoding="utf-8"))
    return text


# --------------------------------------------------------------------------
# Shared analysis
# --------------------------------------------------------------------------

HANDLE_OWNERS: dict[str, tuple[str, str, str]] = {
    "pt_app": ("App", "app", "pt_app_"),
    "pt_window": ("Window", "window", "pt_window_"),
    "pt_popup": ("Popup", "popup", "pt_popup_"),
    "pt_menu": ("Menu", "menu", "pt_menu_"),
    "pt_dialog": ("Dialog", "dialog", "pt_dialog_"),
    "pt_toast": ("Toast", "toast", "pt_toast_"),
    "pt_drop_offer": ("DropOffer", "drop_offer", "pt_drop_offer_"),
    "pt_drop_stream": ("DropStream", "drop_stream", "pt_drop_stream_"),
    "pt_tree_node": ("TreeNode", "tree_node", "pt_tree_node_"),
    "pt_transition": ("Transition", "transition", "pt_transition_"),
}

# Public structs already represented by hand-written wrappers in all three
# bindings. --all should not emit a second copy of them.
STRUCTS_ALREADY_WRAPPED = {
    "pt_drag_data",
    "pt_drop_target",
    "pt_gesture_event",
    "pt_paint_pixels_desc",
    "pt_pointer_event",
    "pt_stylus_event",
    "pt_text_span",
    "pt_transition",
}

WIDGET_PREFIXES = (
    "pt_widget_",
    "pt_text_area_",
    "pt_list_view_",
    "pt_icon_button_",
    "pt_table_",
    "pt_tree_",
    "pt_tabs_",
    "pt_sidebar_",
    "pt_segmented_",
    "pt_combobox_",
    "pt_skeleton_",
    "pt_icon_",
    "pt_button_",
    "pt_toggle_",
    "pt_slider_",
    "pt_progress_",
    "pt_scroll_",
    "pt_grid_",
    "pt_box_",
    "pt_entry_",
    "pt_chip_",
    "pt_row_",
    "pt_column_",
    "pt_label_",
    "pt_checkbox_",
    "pt_radio_",
    "pt_divider_",
    "pt_card_",
    "pt_spacer_",
    "pt_spinner_",
    "pt_wrap_",
)

CPP_WRAPPERS = {
    "pt_widget": "widget",
    "pt_window": "window",
    "pt_app": "app",
    "pt_popup": "popup",
    "pt_menu": "menu",
    "pt_dialog": "dialog",
    "pt_toast": "toast",
    "pt_drop_offer": "drop_offer",
    "pt_drop_stream": "drop_stream",
    "pt_transition": "transition",
}

SCALAR_CPP = {
    "void": "void",
    "bool": "bool",
    "int": "int",
    "uint32_t": "uint32_t",
    "uint64_t": "uint64_t",
    "size_t": "size_t",
    "float": "float",
    "double": "double",
    "pt_color": "pt_color",
}

SCALAR_PY_CTYPES = {
    "bool": "_c_bool",
    "int": "_c_int",
    "uint32_t": "_c_uint32",
    "uint64_t": "_c_uint64",
    "size_t": "_c_size_t",
    "float": "_c_float",
    "double": "_c_double",
    "pt_color": "_c_uint32",
}

SCALAR_PY_TYPE = {
    "bool": "bool",
    "int": "int",
    "uint32_t": "int",
    "uint64_t": "int",
    "size_t": "int",
    "float": "float",
    "double": "float",
    "pt_color": "int",
}

SCALAR_JAVA_TYPE = {
    "bool": "boolean",
    "int": "int",
    "uint32_t": "int",
    "uint64_t": "long",
    "size_t": "long",
    "float": "float",
    "double": "double",
    "pt_color": "int",
}

SCALAR_JAVA_LAYOUT = {
    "bool": "bool",
    "int": "i32",
    "uint32_t": "i32",
    "uint64_t": "i64",
    "size_t": "i64",
    "float": "f32",
    "double": "f64",
    "pt_color": "i32",
}


def widget_prefix(name: str) -> str:
    matches = [prefix for prefix in WIDGET_PREFIXES if name.startswith(prefix)]
    return max(matches, key=len) if matches else "pt_widget_"


def owner_of(
    fn: Function, surface: Optional[Surface] = None
) -> Optional[tuple[str, str, str]]:
    """Return (class, c++ name, C prefix) for the receiver, if there is one."""
    if fn.name.endswith("_create") or not fn.params:
        return None
    first = fn.params[0].type.base
    if first == "pt_widget":
        return ("Widget", "widget", widget_prefix(fn.name))
    owner = HANDLE_OWNERS.get(first)
    if owner:
        return owner
    if surface is not None and first in surface.opaque:
        return (py_struct_name(first), first[len("pt_"):], first + "_")
    return None


def receiver_param(fn: Function, owner: Optional[tuple[str, str, str]]) -> bool:
    return owner is not None and bool(fn.params)


def arg_kind(param: Param, surface: Surface) -> str:
    info = param.type
    base = info.base
    if base in surface.callbacks and info.pointers == 0:
        return "callback"
    if base == "void" and info.pointers >= 1:
        return "void_ptr"
    if base == "char":
        if info.pointers >= 2:
            return "string_array"
        if info.pointers == 1:
            return "string" if info.is_const else "char_ptr"
    if base == "pt_color" and info.pointers >= 1:
        return "color_ptr"
    if base in surface.structs:
        return "struct_ptr" if info.pointers else "struct"
    if base in surface.opaque and info.pointers >= 1:
        return "handle"
    if info.pointers >= 1:
        return "out_ptr"
    if base in surface.enums or base in surface.aliases:
        return "scalar"
    return "scalar"


def collapse_args(
    fn: Function, surface: Surface, strip_owner: bool = True
) -> tuple[list[tuple[str, Param, Optional[Param]]], bool]:
    """Collapse callback+user_data and array+count pairs.

    Returns ((kind, param, companion), ...) where companion is the user_data or
    count parameter that the wrapper absorbs into a higher-level argument.
    """
    params = list(fn.params)
    owner = owner_of(fn, surface)
    if strip_owner and receiver_param(fn, owner):
        params = params[1:]

    collapsed: list[tuple[str, Param, Optional[Param]]] = []
    index = 0
    while index < len(params):
        param = params[index]
        kind = arg_kind(param, surface)
        if (
            kind == "callback"
            and index + 1 < len(params)
            and arg_kind(params[index + 1], surface) == "void_ptr"
            and params[index + 1].name == "user_data"
        ):
            collapsed.append((kind, param, params[index + 1]))
            index += 2
            continue
        if kind == "string_array" and index + 1 < len(params):
            companion = params[index + 1]
            if (
                arg_kind(companion, surface) == "scalar"
                and companion.type.base in {"int", "uint32_t"}
                and "count" in companion.name
            ):
                collapsed.append((kind, param, companion))
                index += 2
                continue
        collapsed.append((kind, param, None))
        index += 1
    return collapsed, bool(owner)


def method_name(cname: str, prefix: str) -> str:
    name = cname[len(prefix):]
    if name.startswith("get_"):
        name = name[4:]
    if name.startswith("set_on_"):
        name = "on_" + name[len("set_on_"):]
    return name


def is_setter(method: str) -> bool:
    return method.startswith("set_") or method.startswith("on_")


MUTATING_VERBS = {
    "add", "begin", "cancel", "clear", "close", "commit", "destroy", "dismiss",
    "hide", "insert", "move", "open", "refresh", "remove", "reposition",
    "restore", "scroll", "select", "set", "show", "toggle", "update",
}


def is_const_getter(method: str) -> bool:
    if is_setter(method):
        return False
    return not any(part in MUTATING_VERBS for part in method.split("_"))


def cpp_scalar_type(info: TypeInfo, surface: Surface) -> str:
    if info.base in SCALAR_CPP:
        return SCALAR_CPP[info.base]
    return info.raw


def py_ctypes_type(info: TypeInfo, surface: Surface) -> str:
    if info.base in SCALAR_PY_CTYPES:
        return SCALAR_PY_CTYPES[info.base]
    return "_c_int"


def py_type_hint(info: TypeInfo, surface: Surface) -> str:
    if info.base in SCALAR_PY_TYPE:
        return SCALAR_PY_TYPE[info.base]
    return "int"


def java_scalar_type(info: TypeInfo, surface: Surface) -> str:
    if info.base in SCALAR_JAVA_TYPE:
        return SCALAR_JAVA_TYPE[info.base]
    return "int"


def java_layout(info: TypeInfo, surface: Surface) -> str:
    if info.base in SCALAR_JAVA_LAYOUT:
        return SCALAR_JAVA_LAYOUT[info.base]
    return "i32"


# --------------------------------------------------------------------------
# C++ rendering
# --------------------------------------------------------------------------


def cpp_stub_header(fn: Function) -> list[str]:
    return [
        f"// Generated binding skeleton for {fn.name}.",
        f"// Source: api/pt.api + include/pt/{fn.header}",
        "// Review aid only: do not overwrite bindings/cpp/pt/pt.hpp with this.",
        "// TODO(binding): confirm ownership, error and callback-lifetime semantics.",
    ]


def cpp_return_decl(fn: Function, surface: Surface) -> str:
    info = fn.return_type
    if info.base in SCALAR_CPP:
        return SCALAR_CPP[info.base]
    if info.base == "char" and info.pointers == 1:
        return "std::string"
    if info.base in surface.structs and info.pointers == 0:
        return info.base
    if info.base in surface.enums:
        return info.base
    if info.base in surface.opaque:
        if info.base == "pt_tree_node":
            return "tree_node *"
        return CPP_WRAPPERS.get(info.base, f"{info.base} *")
    return info.raw


def cpp_owner_arg(
    param: Param, kind: str, surface: Surface
) -> tuple[str, list[str], str, list[str]]:
    """Return (declaration, setup lines, call expression, extra params)."""
    name = param.name
    info = param.type
    if kind == "string":
        return (
            f"std::string_view {name}",
            [f"const std::string {name}_text = copy_string({name});"],
            f"{name}_text.c_str()",
            [],
        )
    if kind == "string_array":
        return (
            f"std::initializer_list<std::string_view> {name}",
            [
                "std::vector<std::string> owned;",
                f"owned.reserve({name}.size());",
                f"for (std::string_view item : {name}) {{",
                "    owned.emplace_back(item);",
                "}",
                "std::vector<const char *> pointers;",
                "pointers.reserve(owned.size());",
                "for (const std::string &item : owned) {",
                "    pointers.push_back(item.c_str());",
                "}",
            ],
            "pointers.data()",
            [],
        )
    if kind == "callback":
        return (
            f"{info.base} callback, void *data = nullptr",
            [],
            "callback, data",
            [],
        )
    if kind == "struct":
        return (f"{info.base} {name}", [], name, [])
    if kind == "handle":
        wrapper = CPP_WRAPPERS.get(info.base)
        if wrapper and info.base in {"pt_app", "pt_window", "pt_widget"}:
            return (f"{wrapper} &{name}", [], f"{name}.get()", [])
        return (f"{info.base} *{name}", [], name, [])
    if kind == "out_ptr":
        return (f"{info.raw} {name}", [], name, [])
    return (f"{cpp_scalar_type(info, surface)} {name}", [], name, [])


def cpp_clipboard_get(fn: Function, owner: tuple[str, str, str]) -> list[str]:
    method = method_name(fn.name, owner[2])
    return [
        "    // TODO(ownership): the caller owns the pixel buffer and releases",
        "    // it with pt_free; confirm the std::vector copy and empty/null case.",
        f"    [[nodiscard]] std::optional<clipboard_image> {method}() const {{",
        "        int width = 0;",
        "        int height = 0;",
        f"        pt_color *pixels = {fn.name}(handle_, &width, &height);",
        "        if (!pixels) {",
        "            return std::nullopt;",
        "        }",
        "        clipboard_image image;",
        "        image.width = width;",
        "        image.height = height;",
        "        if (width > 0 && height > 0) {",
        "            image.pixels.assign(pixels, pixels + width * height);",
        "        }",
        "        pt_free(pixels);",
        "        return image;",
        "    }",
    ]


def cpp_clipboard_set(fn: Function, owner: tuple[str, str, str]) -> list[str]:
    method = method_name(fn.name, owner[2])
    return [
        "    // TODO(ownership): pixels remain caller-owned; Pt copies them.",
        f"    bool {method}(std::span<const pt_color> pixels, int width, int height) {{",
        f"        return {fn.name}(handle_, pixels.data(), width, height);",
        "    }",
    ]


def cpp_stub(fn: Function, surface: Surface) -> str:
    owner = owner_of(fn, surface)
    lines = cpp_stub_header(fn)

    if fn.name == "pt_window_clipboard_get_image_png" and owner:
        return "\n".join(lines + cpp_clipboard_get(fn, owner))
    if fn.name == "pt_window_clipboard_set_image_png" and owner:
        return "\n".join(lines + cpp_clipboard_set(fn, owner))

    if owner is None and fn.name.endswith("_create"):
        return "\n".join(lines + cpp_factory(fn, surface))

    if owner is None:
        return "\n".join(lines + cpp_free_function(fn, surface))

    class_name, cpp_owner, prefix = owner
    method = method_name(fn.name, prefix)
    args, _has_owner = collapse_args(fn, surface)
    if any(kind == "callback" for kind, _param, _companion in args):
        lines.append(
            "// TODO(callback): C++ wrappers pass the C callback directly; add a"
        )
        lines.append(
            "// static trampoline and lifetime slot only if this wrapper adopts std::function."
        )
    declarations: list[str] = []
    setup: list[str] = []
    call_args: list[str] = []
    for kind, param, companion in args:
        declaration, extra_setup, expression, extra = cpp_owner_arg(
            param, kind, surface
        )
        declarations.append(declaration)
        setup.extend(extra_setup)
        call_args.append(expression)
        if kind == "string_array" and companion is not None:
            call_args.append(f"static_cast<int>({param.name}.size())")
        declarations.extend(extra)

    ret_decl = cpp_return_decl(fn, surface)
    return_info = fn.return_type
    const = is_const_getter(method) and cpp_owner != "app"
    fluent = cpp_owner != "app" and is_setter(method)
    ret_prefix = f"{cpp_owner} &" if fluent else ret_decl

    if fn.return_type.base == "void":
        if fluent:
            signature = f"{cpp_owner} &{method}({', '.join(declarations)})"
        else:
            signature = f"void {method}({', '.join(declarations)})"
        signature += " const" if cpp_owner == "app" and is_setter(method) else ""
        body = ["        " + line for line in setup]
        body.extend(
            [f"        {fn.name}(handle_, {', '.join(call_args)});"]
            if call_args
            else [f"        {fn.name}(handle_);"]
        )
        if fluent:
            body.append("        return *this;")
        if fn.name.endswith("_destroy"):
            body.append("        handle_ = nullptr;")
        return "\n".join(
            lines
            + [
                "    // TODO(errors): confirm the NULL-handle and post-destroy contract.",
                f"    {signature} {{",
            ]
            + body
            + ["    }"]
        )

    signature = f"[[nodiscard]] {ret_prefix} {method}({', '.join(declarations)})"
    if const:
        signature += " const"
    body: list[str] = []
    body.extend("        " + line for line in setup)
    call = f"{fn.name}(handle_, {', '.join(call_args)})" if call_args else f"{fn.name}(handle_)"

    if return_info.base == "char" and return_info.pointers == 1:
        if return_info.is_const:
            body.append(f"        const char *value = {call};")
            body.append('        return value ? value : "";')
        else:
            body.append(f"        char *value = {call};")
            body.append("        if (!value) {")
            body.append("            return {};")
            body.append("        }")
            body.append("        std::string copy(value);")
            body.append("        pt_free(value);")
            body.append("        return copy;")
    elif return_info.base in CPP_WRAPPERS:
        wrapper = CPP_WRAPPERS[return_info.base]
        body.append(f"        return {wrapper}({call});")
    elif return_info.base == "pt_tree_node":
        body.append(f"        return {call};")
    else:
        body.append(f"        return {call};")

    return "\n".join(
        lines
        + [
            "    // TODO(ownership): confirm borrowed versus owned return and the",
            "    // error/exception mapping used by this wrapper family.",
            f"    {signature} {{",
        ]
        + body
        + ["    }"]
    )


def cpp_factory(fn: Function, surface: Surface) -> list[str]:
    class_name = CPP_WRAPPERS.get(
        fn.return_type.base, fn.return_type.base[len("pt_"):]
    )
    params: list[str] = []
    call_args: list[str] = []
    for param in fn.params:
        info = param.type
        if info.base in {"pt_window", "pt_widget"}:
            local = "owner" if param.name == CPP_WRAPPERS[info.base] else param.name
            params.append(f"{CPP_WRAPPERS[info.base]} &{local}")
            call_args.append(f"{local}.get()")
        elif info.base in {"pt_app", "pt_menu", "pt_dialog"}:
            params.append(f"{CPP_WRAPPERS[info.base]} &{param.name}")
            call_args.append(f"{param.name}.get()")
        elif info.base == "char" and info.pointers == 1:
            params.append(f"std::string_view {param.name}")
            call_args.append(f"{param.name}_text.c_str()")
        else:
            kind = arg_kind(param, surface)
            declaration, _setup, expression, _extra = cpp_owner_arg(
                param, kind, surface
            )
            params.append(declaration)
            call_args.append(expression)
    body = [
        f"inline {class_name} make_{class_name}({', '.join(params)}) {{",
        "    // TODO(ownership): the holder owns the new handle; confirm whether",
        "    // this wrapper should destroy it or stay non-owning like widget.",
    ]
    for param in fn.params:
        if param.type.base == "char" and param.type.pointers == 1:
            body.append(
                f"    const std::string {param.name}_text = copy_string({param.name});"
            )
    body.append(f"    return {class_name}({fn.name}({', '.join(call_args)}));")
    body.append("}")
    return body


def cpp_free_function(fn: Function, surface: Surface) -> list[str]:
    args, _ = collapse_args(fn, surface)
    declarations: list[str] = []
    setup: list[str] = []
    call_args: list[str] = []
    for kind, param, companion in args:
        declaration, extra_setup, expression, _extra = cpp_owner_arg(
            param, kind, surface
        )
        declarations.append(declaration)
        setup.extend(extra_setup)
        call_args.append(expression)
    ret = cpp_return_decl(fn, surface)
    body = ["inline " + f"{ret} {fn.name}({', '.join(declarations)}) {{"]
    for line in setup:
        body.append("    " + line)
    call = f"{fn.name}({', '.join(call_args)})"
    body.append(f"    return {call};")
    body.append("}")
    return ["// TODO(binding): free function; confirm naming and error mapping."] + body


# --------------------------------------------------------------------------
# Python rendering
# --------------------------------------------------------------------------


PY_CALLBACK_TYPE_NAME = {
    "pt_click_callback": "_CLICK",
    "pt_text_callback": "_TEXT",
    "pt_value_callback": "_VALUE",
    "pt_index_callback": "_INDEX",
    "pt_list_bind_callback": "_BIND",
    "pt_close_callback": "_CLOSE",
    "pt_resize_callback": "_RESIZE",
    "pt_key_callback": "_KEY",
    "pt_timer_callback": "_TIMER",
    "pt_a11y_event_callback": "_A11Y",
    "pt_dialog_callback": "_DIALOG",
    "pt_gesture_callback": "_GESTURE",
    "pt_stylus_callback": "_STYLUS",
    "pt_drop_callback": "_DROP_DATA",
    "pt_table_bind_cell_callback": "_BIND_CELL",
    "pt_table_sort_callback": "_SORT",
    "pt_row_key_callback": "_ROW_KEY",
    "pt_row_text_callback": "_ROW_TEXT",
    "pt_table_sort_columns_callback": "_SORT_COLUMNS",
    "pt_tree_node_callback": "_TREE_NODE",
    "pt_table_cell_edited_callback": "_TABLE_EDITED",
    "pt_table_columns_reordered_callback": "_TABLE_REORDERED",
    "pt_table_row_drag_callback": "_TABLE_ROW_DRAG",
    "pt_tree_rename_callback": "_TREE_RENAME",
    "pt_combobox_text_callback": "_COMBO_TEXT",
    "pt_popup_dismiss_callback": "_POPUP_DISMISS",
    "pt_drop_offer_enter_callback": "_DROP_OFFER_ENTER",
    "pt_drop_offer_drop_callback": "_DROP_OFFER_DROP",
    "pt_drop_stream_callback": "_DROP_STREAM",
    "pt_drag_finished_callback": "_DRAG_FINISHED",
    "pt_paint_callback": "_PAINT",
    "pt_fd_callback": "_FD",
    "pt_ime_commit_callback": "_IME_COMMIT",
    "pt_ime_preedit_callback": "_IME_PREEDIT",
    "pt_focus_callback": "_FOCUS",
    "pt_key_event_callback": "_KEY_EVENT",
    "pt_pointer_callback": "_POINTER",
}


def py_callback_constant(callback: Callback) -> str:
    return PY_CALLBACK_TYPE_NAME.get(callback.name, "_" + callback.name.upper()[3:])


def py_callback_annotation(callback: Callback, surface: Surface) -> str:
    parts: list[str] = []
    for param in callback.params:
        if param.name == "user_data":
            continue
        info = param.type
        wrapper = py_wrapper_name(info.base, surface)
        if wrapper:
            parts.append(f'"{wrapper}"')
        elif info.base == "char" and info.pointers == 1:
            parts.append("str")
        else:
            parts.append(py_type_hint(info, surface))
    return f"Callable[[{', '.join(parts)}], Any]"


def py_stub_header(fn: Function) -> list[str]:
    return [
        f"# Generated binding skeleton for {fn.name}.",
        f"# Source: api/pt.api + include/pt/{fn.header}",
        "# Review aid only: do not overwrite bindings/python/pt.py with this.",
        "# TODO(binding): confirm ownership, error and callback-lifetime semantics.",
    ]


def py_proto(fn: Function, surface: Surface) -> str:
    argtypes: list[str] = []
    args, _ = collapse_args(fn, surface, strip_owner=False)
    for kind, param, companion in args:
        info = param.type
        if kind == "string":
            argtypes.append("_c_char_p")
        elif kind == "string_array":
            argtypes.append("_ctypes.POINTER(_c_char_p)")
        elif kind == "callback":
            callback = surface.callbacks[info.base]
            argtypes.append(py_callback_constant(callback))
            argtypes.append("_c_void_p")
        elif kind == "struct":
            argtypes.append(py_struct_name(info.base))
        elif kind in {"handle", "void_ptr"}:
            argtypes.append("_c_void_p")
        elif kind == "out_ptr":
            argtypes.append(py_out_pointer(info))
        elif kind == "color_ptr":
            argtypes.append("_ctypes.POINTER(_c_uint32)")
        else:
            argtypes.append(py_ctypes_type(info, surface))
        if kind == "string_array" and companion is not None:
            argtypes.append("_c_int")
    restype = py_restype(fn.return_type, surface)
    suffix = ", " + ", ".join(argtypes) if argtypes else ""
    return f'{py_proto_name(fn)} = _proto("{fn.name}", {restype}{suffix})'


def py_struct_name(c_name: str) -> str:
    parts = c_name.split("_")[1:]
    return "".join(part.capitalize() for part in parts)


def py_proto_name(fn: Function) -> str:
    return "_pt_" + fn.name[len("pt_"):]


def py_wrapper_name(base: str, surface: Optional[Surface] = None) -> Optional[str]:
    if base == "pt_widget":
        return "Widget"
    if base == "pt_tree_node":
        return "TreeNode"
    owner = HANDLE_OWNERS.get(base)
    if owner:
        return owner[0]
    if surface is not None and base in surface.opaque:
        return py_struct_name(base)
    return None


def py_out_pointer(info: TypeInfo) -> str:
    mapping = {
        "int": "_c_int",
        "uint32_t": "_c_uint32",
        "uint64_t": "_c_uint64",
        "float": "_c_float",
    }
    inner = mapping.get(info.base, "_c_int")
    return f"_ctypes.POINTER({inner})"


def py_restype(info: TypeInfo, surface: Surface) -> str:
    if info.base == "void" and info.pointers == 0:
        return "None"
    if info.base == "char" and info.pointers == 1:
        return "_c_void_p" if not info.is_const else "_c_char_p"
    if info.base == "pt_color" and info.pointers >= 1:
        return "_c_void_p"
    if info.base in surface.opaque or info.base == "pt_tree_node":
        return "_c_void_p"
    if info.base in surface.structs and info.pointers >= 1:
        return "_c_void_p"
    if info.pointers >= 1:
        return "_c_void_p"
    return py_ctypes_type(info, surface)


def py_return_hint(fn: Function, surface: Surface) -> str:
    info = fn.return_type
    if info.base == "void" and info.pointers == 0:
        return "None"
    if info.base == "char" and info.pointers == 1:
        return "Optional[str]" if info.is_const else "str"
    if info.base == "pt_color" and info.pointers >= 1:
        return "Optional[ClipboardImage]"
    wrapper = py_wrapper_name(info.base)
    if wrapper:
        return f'Optional["{wrapper}"]'
    if info.base in surface.opaque:
        return "Optional[int]"
    return py_type_hint(info, surface)


def py_param_hint(param: Param, kind: str, surface: Surface) -> str:
    info = param.type
    if kind == "string":
        return "str"
    if kind == "string_array":
        return "list[str]"
    if kind == "callback":
        callback = surface.callbacks[info.base]
        return f"Optional[{py_callback_annotation(callback, surface)}]"
    if kind == "struct":
        return py_struct_name(info.base)
    if kind == "handle":
        wrapper = py_wrapper_name(info.base)
        return f'"{wrapper}"' if wrapper else "int"
    if kind in {"void_ptr", "out_ptr", "color_ptr"}:
        return "Any"
    return py_type_hint(info, surface)


def py_owner(fn: Function, surface: Surface) -> Optional[tuple[str, str]]:
    owner = owner_of(fn, surface)
    if owner is None:
        return None
    return owner[0], owner[2]


def py_stub(fn: Function, surface: Surface) -> str:
    owner = py_owner(fn, surface)
    lines = py_stub_header(fn)
    proto = py_proto(fn, surface)
    args, _has_owner = collapse_args(fn, surface)

    if fn.name == "pt_window_clipboard_get_image_png" and owner:
        return "\n".join(lines + [proto, ""] + py_clipboard_get(fn, owner))
    if fn.name == "pt_window_clipboard_set_image_png" and owner:
        return "\n".join(lines + [proto, ""] + py_clipboard_set(fn, owner))

    if fn.name.endswith("_create") and owner is None:
        class_name = py_struct_name(fn.return_type.base) if fn.return_type.base.startswith(
            "pt_"
        ) else "Widget"
        return "\n".join(lines + [proto, ""] + py_factory(fn, surface, class_name))

    if owner is None:
        return "\n".join(lines + [proto, ""] + py_free_function(fn, surface))

    class_name, prefix = owner
    method = method_name(fn.name, prefix)
    signature_params = ["self"]
    for kind, param, companion in args:
        signature_params.append(f"{param.name}: {py_param_hint(param, kind, surface)}")
        if kind == "string_array" and companion is not None:
            continue
    hint = py_return_hint(fn, surface)
    if is_setter(method) and class_name != "App":
        hint = f'"{class_name}"'
    signature = f"    def {method}({', '.join(signature_params)}) -> {hint}:"

    body: list[str] = []
    call_args: list[str] = ["self._ptr"]
    for kind, param, companion in args:
        info = param.type
        if kind == "string":
            call_args.append(f"_string({param.name})")
        elif kind == "string_array":
            body.extend(py_string_array_setup(param.name))
            call_args.append("array")
            if companion is not None:
                call_args.append(f"len({param.name})")
        elif kind == "callback":
            body.extend(py_callback_setup(fn, param, class_name, surface))
            call_args.append("trampoline")
            call_args.append("None")
        elif kind == "struct":
            call_args.append(param.name)
        elif kind == "handle":
            wrapper = py_wrapper_name(info.base)
            if wrapper:
                call_args.append(f"{param.name}._ptr")
            else:
                call_args.append(f"int({param.name})")
        elif kind == "out_ptr":
            call_args.append(param.name)
        elif kind == "color_ptr":
            call_args.append(param.name)
        else:
            if info.base == "bool":
                call_args.append(f"bool({param.name})")
            elif info.base == "float" or info.base == "double":
                call_args.append(f"float({param.name})")
            else:
                call_args.append(f"int({param.name})")

    call = f"{py_proto_name(fn)}({', '.join(call_args)})"
    info = fn.return_type
    if info.base == "void" and info.pointers == 0:
        body.append(f"        {call}")
        if is_setter(method) and class_name != "App":
            body.append("        return self")
    elif info.base == "char" and info.pointers == 1:
        if info.is_const:
            body.append(f"        return _text_out({call})")
        else:
            body.append(f"        pointer = {call}")
            body.append("        if not pointer:")
            body.append('            return ""')
            body.append("        try:")
            body.append(
                '            return _ctypes.string_at(pointer).decode("utf-8")'
            )
            body.append("        finally:")
            body.append("            _pt_free(pointer)")
    elif info.base in surface.opaque:
        wrapper = py_wrapper_name(info.base)
        if wrapper:
            body.append(f"        return {wrapper}._wrap({call})")
        else:
            body.append(f"        return {call}")
    elif info.base in {"bool", "int", "uint32_t", "uint64_t", "size_t"}:
        body.append(f"        return {py_type_hint(info, surface)}({call})")
    elif info.base in {"float", "double"}:
        body.append(f"        return float({call})")
    else:
        body.append(f"        return {call}")

    return "\n".join(
        lines
        + [
            proto,
            "",
            "    # TODO(errors): confirm the NULL/out-of-range and exception contract.",
            signature,
        ]
        + body
    )


def py_string_array_setup(name: str) -> list[str]:
    return [
        f"        array = (_c_char_p * len({name}))(*[_string(value) for value in {name}])",
        "        if not array:",
        "            array = None",
    ]


def py_callback_setup(
    fn: Function, param: Param, class_name: str, surface: Surface
) -> list[str]:
    callback = surface.callbacks[param.type.base]
    constant = py_callback_constant(callback)
    dispatch_args: list[str] = []
    callback_args: list[str] = []
    for index, cb_param in enumerate(callback.params):
        if cb_param.name == "user_data":
            continue
        safe = f"_{cb_param.name}"
        dispatch_args.append(safe)
        wrapper = py_wrapper_name(cb_param.type.base, surface)
        if wrapper:
            callback_args.append(f"{wrapper}._wrap({safe})")
        elif cb_param.type.base == "char" and cb_param.type.pointers == 1:
            callback_args.append(
                f"_ctypes.string_at({safe}).decode(\"utf-8\")"
            )
        elif cb_param.type.base in {"bool"}:
            callback_args.append(f"bool({safe})")
        elif cb_param.type.base in {"float", "double"}:
            callback_args.append(f"float({safe})")
        else:
            callback_args.append(f"int({safe})")
    return_type = callback.return_type
    return_annotation = " -> None" if return_type.base == "void" else " -> Any"
    body = [
        "        if callback is None:",
        f"            {py_proto_name(fn)}(self._ptr, _NULL_{constant[1:]}, None)",
        "            return self",
        f"        def dispatch({', '.join(dispatch_args)}){return_annotation}:",
    ]
    if return_type.base == "void":
        body.append(f"            callback({', '.join(callback_args)})")
    elif return_type.base == "char" and return_type.pointers == 1:
        body.append(
            f"            return _string(callback({', '.join(callback_args)}))"
        )
    else:
        body.append(f"            return callback({', '.join(callback_args)})")
    known_append_classes = {
        "App", "Window", "Menu", "Dialog", "Toast", "DropOffer", "DropStream",
        "TreeNode", "Transition",
    }
    keep = (
        "        self._callbacks.append(trampoline)"
        if class_name in known_append_classes
        else "        self._keep(trampoline)"
    )
    body.extend([f"        trampoline = {constant}(dispatch)", keep])
    return body


def py_clipboard_get(fn: Function, owner: tuple[str, str]) -> list[str]:
    class_name, prefix = owner
    method = method_name(fn.name, prefix)
    return [
        "    # TODO(ownership): read and copy the pixel buffer before pt_free.",
        f"    def {method}(self) -> Optional[ClipboardImage]:",
        "        width = _c_int()",
        "        height = _c_int()",
        f"        pointer = {py_proto_name(fn)}(self._ptr, _ctypes.byref(width),",
        "                               _ctypes.byref(height))",
        "        if not pointer:",
        "            return None",
        "        try:",
        "            count = int(width.value) * int(height.value)",
        "            pixels = list((_c_uint32 * count).from_address(int(pointer)))",
        "        finally:",
        "            _pt_free(pointer)",
        "        return ClipboardImage(pixels, int(width.value), int(height.value))",
    ]


def py_clipboard_set(fn: Function, owner: tuple[str, str]) -> list[str]:
    class_name, prefix = owner
    method = method_name(fn.name, prefix)
    return [
        "    # TODO(ownership): pixels remain caller-owned; Pt copies them.",
        f"    def {method}(self, pixels: list[int], width: int, height: int) -> \"{class_name}\":",
        "        array = (_c_uint32 * len(pixels))(*[int(pixel) for pixel in pixels])",
        f"        {py_proto_name(fn)}(self._ptr, array, int(width), int(height))",
        "        return self",
    ]


def py_factory(fn: Function, surface: Surface, class_name: str) -> list[str]:
    params = ["cls"]
    setup: list[str] = []
    call_args: list[str] = []
    for param in fn.params:
        kind = arg_kind(param, surface)
        params.append(f"{param.name}: {py_param_hint(param, kind, surface)}")
        info = param.type
        if kind == "string":
            call_args.append(f"_string({param.name})")
        elif kind == "handle":
            wrapper = py_wrapper_name(info.base)
            call_args.append(
                f"{param.name}._ptr" if wrapper else f"int({param.name})"
            )
        elif kind == "string_array":
            setup.extend(py_string_array_setup(param.name))
            call_args.append("array")
            call_args.append(f"len({param.name})")
        else:
            call_args.append(param.name)
    body = ["    # TODO(ownership): confirm whether the returned handle is adopted."]
    body.append(f"    @classmethod")
    body.append(f"    def create({', '.join(params)}) -> \"{class_name}\":")
    body.extend(setup)
    body.append(f"        pointer = {py_proto_name(fn)}({', '.join(call_args)})")
    body.append("        if not pointer:")
    body.append("            raise RuntimeError(_error_message())")
    body.append("        return cls(int(pointer))")
    return body


def py_free_function(fn: Function, surface: Surface) -> list[str]:
    args, _ = collapse_args(fn, surface)
    params: list[str] = []
    call_args: list[str] = []
    for kind, param, companion in args:
        params.append(f"{param.name}: {py_param_hint(param, kind, surface)}")
        info = param.type
        if kind == "string":
            call_args.append(f"_string({param.name})")
        elif kind == "string_array":
            call_args.append("None")
            call_args.append(f"len({param.name})")
        else:
            call_args.append(param.name)
    hint = py_return_hint(fn, surface)
    return [
        "def " + f"{method_name(fn.name, 'pt_')}({', '.join(params)}) -> {hint}:",
        f"    return {py_proto_name(fn)}({', '.join(call_args)})",
    ]


# --------------------------------------------------------------------------
# Java rendering
# --------------------------------------------------------------------------


def java_stub_header(fn: Function) -> list[str]:
    return [
        f"// Generated binding skeleton for {fn.name}.",
        f"// Source: api/pt.api + include/pt/{fn.header}",
        "// Review aid only: do not overwrite bindings/java/src/pt/Pt.java with this.",
        "// TODO(binding): confirm ownership, exception and callback-lifetime semantics.",
    ]


def java_method_name(cname: str) -> str:
    return cname[len("pt_"):]


def java_layout_of_type(info: TypeInfo, surface: Surface) -> str:
    if info.base in surface.callbacks and info.pointers == 0:
        return "ptr"
    if info.base in surface.structs and info.pointers == 0:
        return f"{info.base[len('pt_'):]}_layout"
    if info.base == "char" or info.base in surface.opaque or info.pointers >= 1:
        return "ptr"
    return java_layout(info, surface)


def java_return_layout(info: TypeInfo, surface: Surface) -> str:
    if info.base == "void" and info.pointers == 0:
        return "void"
    if info.base == "char" or info.base in surface.opaque or info.pointers >= 1:
        return "ptr"
    if info.base in surface.structs and info.pointers == 0:
        return f"{info.base[len('pt_'):]}_layout"
    return java_layout(info, surface)


def java_return_type(fn: Function, surface: Surface) -> str:
    info = fn.return_type
    if info.base == "void" and info.pointers == 0:
        return "void"
    if info.base == "char" and info.pointers == 1:
        return "String"
    if info.base == "pt_color" and info.pointers >= 1:
        return "ClipboardImage"
    if info.base in surface.structs and info.pointers == 0:
        return py_struct_name(info.base)
    if info.base in surface.opaque or info.base == "pt_tree_node":
        return "MemorySegment"
    return java_scalar_type(info, surface)


def java_callback_name(callback_type: str) -> str:
    # pt_popup_dismiss_callback -> PopupDismissHandler (matching the file's
    # hand-written Handler naming).
    stem = callback_type[len("pt_"):]
    if stem.endswith("_callback"):
        stem = stem[: -len("_callback")]
    parts = [part.capitalize() for part in stem.split("_")]
    return "".join(parts) + "Handler"


def java_callback_handle_type(callback: Callback) -> list[str]:
    types: list[str] = []
    for param in callback.params:
        info = param.type
        if info.base in SCALAR_JAVA_TYPE:
            types.append(SCALAR_JAVA_TYPE[info.base])
        elif info.base == "char" and info.pointers == 1:
            types.append("MemorySegment")
        elif info.base in {"float"}:
            types.append("float")
        elif isinstance(info.base, str) and info.base.startswith("pt_"):
            types.append("MemorySegment")
        else:
            types.append("MemorySegment")
    return types


def java_stub(fn: Function, surface: Surface) -> str:
    lines = java_stub_header(fn)
    if fn.name == "pt_window_clipboard_get_image_png":
        return "\n".join(lines + java_clipboard_get(fn))
    if fn.name == "pt_window_clipboard_set_image_png":
        return "\n".join(lines + java_clipboard_set(fn))

    args, _ = collapse_args(fn, surface, strip_owner=False)
    method = java_method_name(fn.name)
    descriptor_args: list[str] = []
    method_params: list[str] = []
    call_args: list[str] = []
    setup: list[str] = []
    needs_arena = False
    passes_arena = any(kind == "callback" for kind, _p, _c in args)

    for kind, param, companion in args:
        info = param.type
        descriptor_args.append(java_layout_of_type(info, surface))
        if companion is not None:
            descriptor_args.append(java_layout_of_type(companion.type, surface))
        if kind == "string":
            method_params.append(f"String {param.name}")
            call_args.append(f"cstring(arena, {param.name})")
            needs_arena = True
        elif kind == "string_array":
            method_params.append(f"String[] {param.name}")
            setup.extend(java_string_array_setup(param.name))
            call_args.append("array")
            if companion is not None:
                call_args.append(f"{param.name}.length")
            needs_arena = True
        elif kind == "callback":
            callback = surface.callbacks[info.base]
            method_params.append(f"{java_callback_name(info.base)} handler")
            method_params.append("Arena arena")
            setup.append(
                f"MemorySegment stub = {java_upcall_name(info.base)}(handler, arena);"
            )
            call_args.append("stub")
            call_args.append("MemorySegment.NULL")
        elif kind == "struct":
            method_params.append(f"{py_struct_name(info.base)} {param.name}")
            setup.append(
                f"MemorySegment {param.name}_value = "
                f"{info.base[len('pt_'):]}(arena, {param.name});"
            )
            call_args.append(f"{param.name}_value")
            needs_arena = True
        elif kind == "handle":
            method_params.append(f"MemorySegment {param.name}")
            call_args.append(param.name)
        elif kind == "void_ptr":
            method_params.append(f"MemorySegment {param.name}")
            call_args.append(param.name)
        elif kind == "out_ptr":
            method_params.append(f"MemorySegment {param.name}")
            call_args.append(param.name)
        elif kind == "color_ptr":
            method_params.append(f"int[] {param.name}")
            setup.extend(java_pixel_array_setup(param.name))
            call_args.append(f"{param.name}_value")
            needs_arena = True
        else:
            method_params.append(f"{java_scalar_type(info, surface)} {param.name}")
            call_args.append(param.name)

    ret_layout = java_return_layout(fn.return_type, surface)
    if ret_layout == "void":
        descriptor = f"FunctionDescriptor.ofVoid({', '.join(descriptor_args)})"
        ret_type = "void"
    elif descriptor_args:
        descriptor = f"FunctionDescriptor.of({ret_layout}, {', '.join(descriptor_args)})"
        ret_type = java_return_type(fn, surface)
    else:
        descriptor = f"FunctionDescriptor.of({ret_layout})"
        ret_type = java_return_type(fn, surface)

    bind = f"private static final MethodHandle {method} = bind(\"{fn.name}\", {descriptor});"
    method_signature = f"public static {ret_type} {method}({', '.join(method_params)})"

    body: list[str] = []
    body.extend("        " + line for line in setup)
    call = f"invoke({method}, {', '.join(call_args)})" if call_args else f"invoke({method})"
    info = fn.return_type
    if ret_type == "void":
        body.append(f"        {call};")
    elif ret_type == "String":
        if info.is_const:
            body.append(f"        MemorySegment text = (MemorySegment) {call};")
            body.append("        return text.address() == 0 ? \"\" : text.reinterpret(")
            body.append("                Long.MAX_VALUE).getString(0);")
        else:
            body.append(f"        MemorySegment text = (MemorySegment) {call};")
            body.append("        if (text.address() == 0) {")
            body.append("            return \"\";")
            body.append("        }")
            body.append("        try {")
            body.append("            return text.reinterpret(Long.MAX_VALUE).getString(0);")
            body.append("        } finally {")
            body.append("            invoke(free, text);")
            body.append("        }")
    elif ret_type in {"boolean", "int", "long", "float", "double"}:
        body.append(f"        return ({ret_type}) {call};")
    else:
        body.append(f"        return ({ret_type}) {call};")

    wrapped = needs_arena and not passes_arena
    if wrapped:
        body = ["        try (Arena arena = Arena.ofConfined()) {"] + [
            "    " + line for line in body
        ] + ["        }"]

    return "\n".join(
        lines
        + [
            f"    {bind}",
            "",
            "    // TODO(errors): confirm Java exception/NullPointer mapping.",
            f"    {method_signature} {{",
        ]
        + body
        + ["    }"]
    )


def java_string_array_setup(name: str) -> list[str]:
    return [
        f"MemorySegment array = arena.allocate(ValueLayout.ADDRESS, {name}.length);",
        "for (int index = 0; index < "
        f"{name}.length; ++index) {{",
        "    array.setAtIndex(ValueLayout.ADDRESS, index,",
        f"            {name}[index] == null ? MemorySegment.NULL",
        f"                    : cstring(arena, {name}[index]));",
        "}",
    ]


def java_pixel_array_setup(name: str) -> list[str]:
    return [
        f"MemorySegment {name}_value = arena.allocate(ValueLayout.JAVA_INT, {name}.length);",
        f"for (int index = 0; index < {name}.length; ++index) {{",
        f"    {name}_value.setAtIndex(ValueLayout.JAVA_INT, index, {name}[index]);",
        "}",
    ]


def java_upcall_name(callback_type: str) -> str:
    stem = callback_type[len("pt_"):]
    if stem.endswith("_callback"):
        stem = stem[: -len("_callback")]
    return stem + "_upcall"


def java_clipboard_get(fn: Function) -> list[str]:
    method = java_method_name(fn.name)
    return [
        f"    private static final MethodHandle {method} = bind(\"{fn.name}\",",
        "            FunctionDescriptor.of(ptr, ptr, ptr, ptr));",
        "",
        "    // TODO(ownership): copy the pixel buffer before free; confirm width,",
        "    // height and null handling against the header contract.",
        f"    public static ClipboardImage {method}(MemorySegment window) {{",
        "        try (Arena arena = Arena.ofConfined()) {",
        "            MemorySegment width = arena.allocate(ValueLayout.JAVA_INT);",
        "            MemorySegment height = arena.allocate(ValueLayout.JAVA_INT);",
        f"            MemorySegment pixels = (MemorySegment) invoke({method}, window,",
        "                    width, height);",
        "            if (pixels.address() == 0) {",
        "                return null;",
        "            }",
        "            int w = width.get(ValueLayout.JAVA_INT, 0);",
        "            int h = height.get(ValueLayout.JAVA_INT, 0);",
        "            int[] copy = new int[Math.max(0, w * h)];",
        "            MemorySegment view = pixels.reinterpret((long) copy.length",
        "                    * Integer.BYTES);",
        "            for (int index = 0; index < copy.length; ++index) {",
        "                copy[index] = view.get(ValueLayout.JAVA_INT,",
        "                        (long) index * Integer.BYTES);",
        "            }",
        "            invoke(free, pixels);",
        "            return new ClipboardImage(copy, w, h);",
        "        }",
        "    }",
    ]


def java_clipboard_set(fn: Function) -> list[str]:
    method = java_method_name(fn.name)
    return [
        f"    private static final MethodHandle {method} = bind(\"{fn.name}\",",
        "            FunctionDescriptor.of(bool, ptr, ptr, i32, i32));",
        "",
        "    // TODO(ownership): pixels remain caller-owned; Pt copies them.",
        f"    public static boolean {method}(MemorySegment window, int[] pixels,",
        "            int width, int height) {",
        "        try (Arena arena = Arena.ofConfined()) {",
        "            MemorySegment data = arena.allocate(ValueLayout.JAVA_INT,",
        "                    pixels.length);",
        "            for (int index = 0; index < pixels.length; ++index) {",
        "                data.setAtIndex(ValueLayout.JAVA_INT, index, pixels[index]);",
        "            }",
        f"            return (boolean) invoke({method}, window, data, width, height);",
        "        }",
        "    }",
    ]


# --------------------------------------------------------------------------
# Shared scaffolding for each language
# --------------------------------------------------------------------------


def used_callback_types(functions: Iterable[Function], surface: Surface) -> list[Callback]:
    used: dict[str, Callback] = {}
    for fn in functions:
        for param in fn.params:
            if param.type.base in surface.callbacks:
                used[param.type.base] = surface.callbacks[param.type.base]
    return [used[name] for name in sorted(used)]


def used_struct_types(functions: Iterable[Function], surface: Surface) -> list[Struct]:
    used: dict[str, Struct] = {}
    for fn in functions:
        for info in [fn.return_type] + [param.type for param in fn.params]:
            if info.base in surface.structs:
                used[info.base] = surface.structs[info.base]
    return [used[name] for name in sorted(used)]


def used_enum_types(functions: Iterable[Function], surface: Surface) -> list[str]:
    used: set[str] = set()
    for fn in functions:
        for info in [fn.return_type] + [param.type for param in fn.params]:
            if info.base in surface.enums:
                used.add(info.base)
    return sorted(used)


def generated_handle_types(functions: list[Function], surface: Surface) -> list[str]:
    """Opaque handle families that need a new wrapper class in this stub set."""
    types: set[str] = set()
    if any(fn.name.startswith("pt_popup_") for fn in functions):
        types.add("pt_popup")
    for fn in functions:
        base = fn.return_type.base
        if (
            fn.name.endswith("_create")
            and base in surface.opaque
            and base not in HANDLE_OWNERS
        ):
            types.add(base)
    return sorted(types)


def cpp_context(
    functions: list[Function],
    surface: Surface,
    scaffold: Optional[list[Function]] = None,
) -> str:
    dependencies = scaffold if scaffold is not None else functions
    lines = [
        "// Generated shared scaffolding for the C++ stub set.",
        "// TODO(binding): merge only what is still absent from bindings/cpp/pt/pt.hpp.",
        "// Requires the existing pt::copy_string helper, <optional>, <span> and <vector>.",
        "",
    ]
    names = {fn.name for fn in dependencies}
    for base in generated_handle_types(dependencies, surface):
        wrapper = CPP_WRAPPERS.get(base, base[len("pt_"):])
        lines.extend(
            [
                f"class {wrapper} {{",
                "  public:",
                f"    {wrapper}() = default;",
                f"    explicit {wrapper}({base} *handle) : handle_(handle) {{}}",
                "",
                f"    [[nodiscard]] {base} *get() const {{ return handle_; }}",
                "    [[nodiscard]] bool valid() const { return handle_ != nullptr; }",
                "",
                "  private:",
                f"    // TODO(ownership): decide whether this wrapper destroys the {wrapper}.",
                f"    {base} *handle_ = nullptr;",
                "};",
                "",
            ]
        )
    if "pt_window_clipboard_get_image_png" in names:
        lines.extend(
            [
                "struct clipboard_image {",
                "    // TODO(ownership): pixels is a copy; the native buffer is freed.",
                "    std::vector<pt_color> pixels;",
                "    int width = 0;",
                "    int height = 0;",
                "};",
                "",
            ]
        )
    return "\n".join(lines).rstrip() + "\n"


def python_context(
    functions: list[Function],
    surface: Surface,
    existing: str,
    scaffold: Optional[list[Function]] = None,
) -> str:
    dependencies = scaffold if scaffold is not None else functions
    lines = [
        "# Generated shared scaffolding for the Python stub set.",
        "# TODO(binding): merge only what is still absent from bindings/python/pt.py.",
        "",
    ]
    names = {fn.name for fn in dependencies}
    for base in generated_handle_types(dependencies, surface):
        wrapper = py_struct_name(base)
        if binding_has("python", wrapper, existing):
            continue
        lines.extend(
            [
                f"class {wrapper}:",
                f'    """Generated skeleton for {base} handles."""',
                "",
                "    def __init__(self, pointer: int) -> None:",
                "        if not pointer:",
                f'            raise RuntimeError("Pt returned a null {base}")',
                "        self._ptr = int(pointer)",
                "        self._callbacks: list[Any] = []",
                "",
                "    def _keep(self, callback: Any) -> None:",
                "        self._callbacks.append(callback)",
                "",
                "    @classmethod",
                f'    def _wrap(cls, pointer: Optional[int]) -> Optional["{wrapper}"]:',
                "        return None if not pointer else cls(int(pointer))",
                "",
                "    @property",
                "    def raw(self) -> int:",
                "        return self._ptr",
                "",
                f"    # TODO(ownership): confirm who destroys the {base} handle and when",
                "    # the wrapper must drop it.",
                "",
            ]
        )
    if "pt_window_clipboard_get_image_png" in names or (
        "pt_window_clipboard_set_image_png" in names
    ):
        lines.extend(
            [
                "class ClipboardImage:",
                '    """Pixel buffer copied from pt_window_clipboard_get_image_png."""',
                "",
                "    def __init__(self, pixels: list[int], width: int, height: int) -> None:",
                "        self.pixels = pixels",
                "        self.width = width",
                "        self.height = height",
                "",
            ]
        )
    for struct in used_struct_types(dependencies, surface):
        wrapper = py_struct_name(struct.name)
        if struct.name in STRUCTS_ALREADY_WRAPPED:
            continue
        if binding_has("python", wrapper, existing):
            continue
        fields = []
        for field in struct.fields:
            fields.append(
                f'    ("{field.name}", {py_field_ctype(field.type)}),'
            )
        lines.extend(
            [
                f"class {wrapper}(_ctypes.Structure):",
                f'    """Generated layout for {struct.name}; review against tests/test_abi.c."""',
                "",
                "    _fields_ = [",
            ]
            + fields
            + [
                "    ]",
                "",
            ]
        )
    for callback in used_callback_types(dependencies, surface):
        constant = py_callback_constant(callback)
        if binding_has("python", constant, existing):
            continue
        argtypes = [_c_param_type(param, surface) for param in callback.params]
        lines.extend(
            [
                f"{constant} = _ctypes.CFUNCTYPE({py_restype(callback.return_type, surface)}, "
                + ", ".join(argtypes)
                + ")",
                f"_NULL_{constant[1:]} = {constant}(0)",
                "",
            ]
        )
    for enum_type in used_enum_types(dependencies, surface):
        missing = [
            (name, value)
            for name, value in sorted(surface.enums[enum_type].items())
            if not binding_has("python", name[len("pt_"):], existing)
        ]
        if missing:
            lines.append(f"# {enum_type} constants")
            for name, value in missing:
                lines.append(f"{name[len('pt_'):]} = {value}")
            lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def py_field_ctype(info: TypeInfo) -> str:
    if info.base in SCALAR_PY_CTYPES:
        return SCALAR_PY_CTYPES[info.base]
    if info.base == "char" and "[" in info.raw:
        return "_c_char * 64"
    return "_c_void_p"


def _c_param_type(param: Param, surface: Surface) -> str:
    info = param.type
    if info.base in SCALAR_PY_CTYPES:
        return SCALAR_PY_CTYPES[info.base]
    if info.base == "char" and info.pointers == 1:
        return "_c_char_p"
    if info.base in surface.structs:
        return py_struct_name(info.base)
    return "_c_void_p"


def java_context(
    functions: list[Function],
    surface: Surface,
    existing: str,
    scaffold: Optional[list[Function]] = None,
) -> str:
    dependencies = scaffold if scaffold is not None else functions
    lines = [
        "// Generated shared scaffolding for the Java stub set.",
        "// TODO(binding): merge only what is still absent from bindings/java/src/pt/Pt.java.",
        "",
    ]
    names = {fn.name for fn in dependencies}
    if "pt_window_clipboard_get_image_png" in names or (
        "pt_window_clipboard_set_image_png" in names
    ):
        lines.extend(
            [
                "public record ClipboardImage(int[] pixels, int width, int height) {}",
                "",
            ]
        )
    structs = used_struct_types(dependencies, surface)
    for struct in structs:
        wrapper = py_struct_name(struct.name)
        if struct.name in STRUCTS_ALREADY_WRAPPED:
            continue
        if binding_has("java", wrapper, existing):
            continue
        record_fields = ", ".join(
            f"{java_scalar_type(field.type, surface)} {field.name}"
            for field in struct.fields
        )
        layout_fields = ", ".join(
            java_layout(field.type, surface) for field in struct.fields
        )
        setter_lines = []
        offset = 0
        for field in struct.fields:
            layout = java_layout(field.type, surface)
            if layout in {"f32", "i32", "bool"}:
                size = 4
            elif layout == "i64":
                size = 8
            elif layout == "f64":
                size = 8
            else:
                size = 8
            set_line = (
                f"segment.set(ValueLayout.JAVA_{java_value_name(layout)}, "
                f"{offset}, anchor.{field.name}());"
            )
            if layout == "bool":
                set_line = (
                    f"segment.set(ValueLayout.JAVA_BOOLEAN, {offset}, "
                    f"anchor.{field.name}());"
                )
            setter_lines.append(set_line)
            offset += size
        lines.extend(
            [
                f"public record {wrapper}({record_fields}) {{}}",
                f"private static final MemoryLayout {struct.name[len('pt_'):]}_layout =",
                f"        MemoryLayout.structLayout({layout_fields}).withName(\"{struct.name}\");",
                "",
                f"private static MemorySegment {struct.name[len('pt_'):]}(Arena arena,",
                f"        {wrapper} anchor) {{",
                f"    MemorySegment segment = arena.allocate("
                f"{struct.name[len('pt_'):]}_layout);",
            ]
        )
        lines.extend("    " + line for line in setter_lines)
        lines.extend(["    return segment;", "}", ""])

    for enum_type in used_enum_types(dependencies, surface):
        missing = [
            (name, value)
            for name, value in sorted(surface.enums[enum_type].items())
            if not binding_has("java", name[len("pt_"):], existing)
        ]
        if missing:
            lines.append(f"/* {enum_type} constants */")
            for name, value in missing:
                lines.append(
                    f"public static final int {name[len('pt_'):]} = {value};"
                )
            lines.append("")

    for callback in used_callback_types(dependencies, surface):
        interface_name = java_callback_name(callback.name)
        if binding_has("java", interface_name, existing):
            continue
        arg_types = java_callback_handle_type(callback)
        params = ", ".join(
            f"MemorySegment arg{index}" if typ == "MemorySegment" else f"{typ} arg{index}"
            for index, typ in enumerate(arg_types)
        )
        ret = java_return_type_for_callback(callback)
        if any(
            param.type.base == "char" and param.type.pointers == 1
            for param in callback.params
        ):
            lines.append(
                "// TODO(callback): text arguments are raw MemorySegment here; add a"
            )
            lines.append(
                "// nativeText bridge if the public handler should take String."
            )
        lines.extend(
            [
                "@FunctionalInterface",
                f"public interface {interface_name} {{",
                f"    {ret} handle({params});",
                "}",
                "",
                f"private static final FunctionDescriptor "
                f"{java_descriptor_name(callback.name)} =",
                "        FunctionDescriptor."
                + (
                    f"ofVoid({', '.join(java_callback_layouts(callback))});"
                    if ret == "void"
                    else f"of({java_callback_return_layout(callback)}, "
                    f"{', '.join(java_callback_layouts(callback))});"
                ),
                "",
                f"private static MemorySegment {java_upcall_name(callback.name)}("
                f"{interface_name} handler, Arena arena) {{",
                "    try {",
                "        MethodHandle target = MethodHandles.lookup().findVirtual(",
                f"                {interface_name}.class, \"handle\",",
                f"                MethodType.methodType({java_method_type_void(callback)}, "
                + ", ".join(java_callback_method_types(callback))
                + "));",
                f"        return linker.upcallStub(target.bindTo(handler), "
                f"{java_descriptor_name(callback.name)}, arena);",
                "    } catch (ReflectiveOperationException error) {",
                f"        throw new IllegalArgumentException(\"cannot create "
                f"{callback.name} callback\", error);",
                "    }",
                "}",
                "",
            ]
        )
    return "\n".join(lines).rstrip() + "\n"


def java_value_name(layout: str) -> str:
    return {
        "f32": "FLOAT",
        "i32": "INT",
        "i64": "LONG",
        "f64": "DOUBLE",
        "bool": "BOOLEAN",
    }.get(layout, "INT")


def java_descriptor_name(callback_type: str) -> str:
    stem = callback_type[len("pt_"):]
    if stem.endswith("_callback"):
        stem = stem[: -len("_callback")]
    return stem + "_descriptor"


def java_callback_layouts(callback: Callback) -> list[str]:
    layouts = []
    for param in callback.params:
        if param.type.base in SCALAR_JAVA_LAYOUT:
            layouts.append(SCALAR_JAVA_LAYOUT[param.type.base])
        else:
            layouts.append("ptr")
    return layouts


def java_callback_return_layout(callback: Callback) -> str:
    info = callback.return_type
    if info.base == "void":
        return "void"
    if info.base in SCALAR_JAVA_LAYOUT:
        return SCALAR_JAVA_LAYOUT[info.base]
    return "ptr"


def java_return_type_for_callback(callback: Callback) -> str:
    info = callback.return_type
    if info.base == "void":
        return "void"
    if info.base == "char" and info.pointers == 1:
        return "MemorySegment"
    if info.base in SCALAR_JAVA_TYPE:
        return SCALAR_JAVA_TYPE[info.base]
    return "MemorySegment"


def java_method_type_void(callback: Callback) -> str:
    return "void.class"


def java_callback_method_types(callback: Callback) -> list[str]:
    types = []
    for param in callback.params:
        info = param.type
        if info.base == "bool":
            types.append("boolean.class")
        elif info.base in {"int", "uint32_t"}:
            types.append("int.class")
        elif info.base in {"uint64_t", "size_t"}:
            types.append("long.class")
        elif info.base in {"float"}:
            types.append("float.class")
        elif info.base in {"double"}:
            types.append("double.class")
        else:
            types.append("MemorySegment.class")
    return types


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------


def render_language(
    language: str,
    functions: list[Function],
    surface: Surface,
    existing: str,
    scaffold: Optional[list[Function]] = None,
) -> str:
    context = {
        "cpp": cpp_context,
        "python": python_context,
        "java": java_context,
    }[language]
    render = {"cpp": cpp_stub, "python": py_stub, "java": java_stub}[language]
    if language == "python":
        head = context(functions, surface, existing, scaffold)
    elif language == "java":
        head = context(functions, surface, existing, scaffold)
    else:
        head = context(functions, surface, scaffold)
    chunks = [head.rstrip()]
    for fn in sorted(functions, key=lambda item: item.name):
        chunks.append(render(fn, surface).rstrip())
    return "\n\n".join(chunks) + "\n"


def ensure_safe_out(root: pathlib.Path, out: pathlib.Path) -> None:
    resolved = out.resolve()
    bindings = (root / "bindings").resolve()
    if resolved == bindings or bindings in resolved.parents:
        raise InputError(
            "refusing to write under bindings/; choose a preview directory such as "
            "/tmp/opencode/stubs-preview"
        )
    if resolved.exists() and not resolved.is_dir():
        raise InputError(f"output path is not a directory: {resolved}")


def write_preview(
    root: pathlib.Path,
    out: pathlib.Path,
    language: str,
    functions: list[Function],
    surface: Surface,
    existing: str,
    scaffold: Optional[list[Function]] = None,
) -> int:
    target = out / language
    target.mkdir(parents=True, exist_ok=True)
    extension = EXTENSIONS[language]
    context = {
        "cpp": cpp_context,
        "python": python_context,
        "java": java_context,
    }[language]
    if language == "python":
        head = context(functions, surface, existing, scaffold)
    elif language == "java":
        head = context(functions, surface, existing, scaffold)
    else:
        head = context(functions, surface, scaffold)
    (target / f"00-context.{extension}").write_text(head, encoding="utf-8")
    render = {"cpp": cpp_stub, "python": py_stub, "java": java_stub}[language]
    for fn in sorted(functions, key=lambda item: item.name):
        (target / f"{fn.name}.{extension}").write_text(
            render(fn, surface) + "\n", encoding="utf-8"
        )
    return len(functions) + 1


def check_report(
    languages: list[str],
    surface: Surface,
    missing: dict[str, list[str]],
    generated: dict[str, list[str]],
    ungeneratable: dict[str, list[str]],
    unreviewed: dict[str, list[str]],
) -> str:
    lines = [
        "gen-binding-stubs: check",
        f"surface: {len(surface.functions)} functions",
        "",
    ]
    total_missing = 0
    for language in languages:
        total_missing += len(missing[language])
        lines.append(
            f"{language:6s} missing {len(missing[language]):3d}, "
            f"generatable {len(generated[language]):3d}, "
            f"skipped {len(ungeneratable[language]):3d}, "
            f"unreviewed {len(unreviewed[language]):3d}"
        )
        if ungeneratable[language]:
            lines.append(
                "  cannot generate: " + ", ".join(ungeneratable[language])
            )
        if unreviewed[language]:
            lines.append(
                "  not in api/pt.api: " + ", ".join(unreviewed[language])
            )
    lines.append("")
    lines.append(
        "result: MISSING" if total_missing else "result: COMPLETE"
    )
    lines.append(
        "note: missing wrappers are reported but are not a failure unless --strict."
    )
    return "\n".join(lines) + "\n"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--language",
        choices=LANGUAGES,
        default=None,
        help="one binding language to generate (default: all)",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--missing",
        action="store_true",
        help="generate only functions absent from the binding (default)",
    )
    mode.add_argument(
        "--all",
        action="store_true",
        help="generate skeletons for every reviewed function",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="directory for deterministic per-function preview files; never bindings/",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report missing and generatable counts without writing files",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="with --check, exit 1 when wrappers are missing or ungeneratable",
    )
    parser.add_argument(
        "--root",
        default=str(ROOT),
        help="tree containing api/pt.api, include/pt and bindings/ (default: repo root)",
    )
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    root = pathlib.Path(arguments.root).resolve()
    languages = [arguments.language] if arguments.language else list(LANGUAGES)

    if arguments.check and arguments.out:
        print("gen-binding-stubs: --check does not write files; drop --out", file=sys.stderr)
        return 2

    try:
        surface = parse_surface(root / "include" / "pt")
        baseline = load_and_verify_baseline(root, surface)
        reviewed = baseline_function_names(baseline)
    except InputError as error:
        print(f"gen-binding-stubs: {error}", file=sys.stderr)
        return 2

    functions = sorted(surface.functions)
    missing: dict[str, list[str]] = {}
    generated: dict[str, list[str]] = {}
    ungeneratable: dict[str, list[str]] = {}
    unreviewed: dict[str, list[str]] = {}
    existing_text: dict[str, str] = {}

    for language in languages:
        missing[language] = binding_missing(root, language, functions)
        existing_text[language] = binding_text(root, language)
        unreviewed[language] = [
            name for name in missing[language] if name not in reviewed
        ]
        selected = (
            [surface.functions[name] for name in functions]
            if arguments.all
            else [
                surface.functions[name]
                for name in missing[language]
                if name in reviewed
            ]
        )
        generated[language] = []
        ungeneratable[language] = []
        for fn in selected:
            try:
                stub = {"cpp": cpp_stub, "python": py_stub, "java": java_stub}[
                    language
                ](fn, surface)
                if "TODO" not in stub:
                    raise UnsupportedSignature(fn.name, "no TODO marker generated")
                generated[language].append(fn.name)
            except UnsupportedSignature:
                ungeneratable[language].append(fn.name)

    if arguments.check:
        report = check_report(
            languages,
            surface,
            missing,
            generated,
            ungeneratable,
            unreviewed,
        )
        print(report, end="")
        if arguments.strict and any(
            missing[language] or ungeneratable[language]
            for language in languages
        ):
            return 1
        return 0

    try:
        if arguments.out:
            out = pathlib.Path(arguments.out)
            ensure_safe_out(root, out)
            written = 0
            for language in languages:
                selected = (
                    [surface.functions[name] for name in functions]
                    if arguments.all
                    else [
                        surface.functions[name]
                        for name in missing[language]
                        if name in reviewed
                    ]
                )
                available = [
                    fn for fn in selected if fn.name in generated[language]
                ]
                scaffold = [
                    surface.functions[name]
                    for name in missing[language]
                    if name in reviewed
                ]
                written += write_preview(
                    root,
                    out,
                    language,
                    available,
                    surface,
                    existing_text[language],
                    scaffold,
                )
            print(
                f"gen-binding-stubs: wrote {written} skeleton file(s) under {out}"
            )
        else:
            blocks: list[str] = []
            for language in languages:
                selected = (
                    [surface.functions[name] for name in functions]
                    if arguments.all
                    else [
                        surface.functions[name]
                        for name in missing[language]
                        if name in reviewed
                    ]
                )
                available = [
                    fn for fn in selected if fn.name in generated[language]
                ]
                scaffold = [
                    surface.functions[name]
                    for name in missing[language]
                    if name in reviewed
                ]
                blocks.append(
                    render_language(
                        language,
                        available,
                        surface,
                        existing_text[language],
                        scaffold,
                    )
                )
            print("\n".join(blocks), end="")
    except InputError as error:
        print(f"gen-binding-stubs: {error}", file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
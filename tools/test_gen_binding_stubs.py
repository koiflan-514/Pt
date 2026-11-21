#!/usr/bin/env python3
"""Self-tests for tools/gen-binding-stubs.py.

Run with:

    python3 tools/test_gen_binding_stubs.py

The suite builds a small fixture tree (api/pt.api, include/pt/*.h and three
binding sources), then checks:

* the rich header parser recovers parameter names and types;
* missing-function detection matches the rule used by tools/api-audit.py;
* each language emits the expected ownership/trampoline/struct scaffolding;
* output is byte-for-byte deterministic;
* --check and --strict exit codes, and the bindings/ write guard;
* on the real tree, the generated missing set equals api-audit's coverage.
"""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import re
import tempfile
import unittest
import importlib.util

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "gen-binding-stubs.py"


def load_module(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


GEN = load_module("gen_binding_stubs", TOOL)
AUDIT = load_module("pt_api_audit_for_stub_test", ROOT / "tools" / "api-audit.py")

FIXTURE_HEADER = """\
#ifndef pt_demo_h
#define pt_demo_h

typedef struct pt_demo pt_demo;

typedef enum pt_demo_mode {
    pt_demo_mode_a = 0,
    pt_demo_mode_b = 1
} pt_demo_mode;

typedef struct pt_demo_anchor {
    float x;
    float y;
    float width;
    float height;
} pt_demo_anchor;

typedef struct pt_demo_event {
    pt_demo_mode mode;
    float amount;
} pt_demo_event;

typedef void (*pt_demo_event_callback)(pt_demo *demo, void *user_data);

pt_api pt_demo *pt_demo_create(pt_demo *parent, const char *title);
pt_api void pt_demo_destroy(pt_demo *demo);
pt_api void pt_demo_set_mode(pt_demo *demo, pt_demo_mode mode);
pt_api pt_demo_mode pt_demo_get_mode(const pt_demo *demo);
pt_api void pt_demo_set_on_event(pt_demo *demo, pt_demo_event_callback callback,
                                 void *user_data);
pt_api char *pt_demo_serialize(const pt_demo *demo);
pt_api const char *pt_demo_get_name(const pt_demo *demo);
pt_api bool pt_demo_show(pt_demo *demo, pt_demo_anchor anchor);
pt_api uint32_t pt_demo_event_count(const pt_demo_event *event);

#endif
"""

FIXTURE_CPP = """
namespace pt {
inline void present() { (void)pt_demo_create; (void)pt_demo_get_mode; }
}
"""

FIXTURE_PYTHON = """
_present = ("pt_demo_create", "pt_demo_serialize", "pt_demo_destroy")
"""

FIXTURE_JAVA = """
final class Present { String[] names = {"pt_demo_destroy"}; }
"""

FIXTURE_API_NAMES = [
    "pt_demo_create",
    "pt_demo_destroy",
    "pt_demo_set_mode",
    "pt_demo_get_mode",
    "pt_demo_set_on_event",
    "pt_demo_serialize",
    "pt_demo_get_name",
    "pt_demo_show",
    "pt_demo_event_count",
]

class Fixture:
    def __init__(self) -> None:
        self.root = pathlib.Path(tempfile.mkdtemp(prefix="stub-fixture-", dir="/tmp/opencode"))
        (self.root / "include" / "pt").mkdir(parents=True)
        (self.root / "api").mkdir()
        (self.root / "bindings" / "cpp" / "pt").mkdir(parents=True)
        (self.root / "bindings" / "python").mkdir(parents=True)
        (self.root / "bindings" / "java" / "src" / "pt").mkdir(parents=True)
        (self.root / "include" / "pt" / "demo.h").write_text(
            FIXTURE_HEADER, encoding="utf-8"
        )
        lines = AUDIT.baseline_lines(self.root / "include" / "pt")
        (self.root / "api" / "pt.api").write_text(
            "\n".join(lines) + "\n", encoding="utf-8"
        )
        (self.root / "bindings" / "cpp" / "pt" / "pt.hpp").write_text(
            FIXTURE_CPP, encoding="utf-8"
        )
        (self.root / "bindings" / "python" / "pt.py").write_text(
            FIXTURE_PYTHON, encoding="utf-8"
        )
        (self.root / "bindings" / "java" / "src" / "pt" / "Pt.java").write_text(
            FIXTURE_JAVA, encoding="utf-8"
        )

    def close(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)


class GenBindingStubsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.fixture = Fixture()
        cls.surface = GEN.parse_surface(cls.fixture.root / "include" / "pt")
        cls.baseline = AUDIT.load_baseline(cls.fixture.root / "api" / "pt.api")
        cls.functions = sorted(cls.surface.functions)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.fixture.close()

    def run_tool(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(TOOL), "--root", str(self.fixture.root), *arguments],
            check=False,
            capture_output=True,
            text=True,
        )

    # -- parser ---------------------------------------------------------

    def test_parser_recovers_names_and_types(self) -> None:
        show = self.surface.functions["pt_demo_show"]
        self.assertEqual([param.name for param in show.params], ["demo", "anchor"])
        self.assertEqual(show.params[1].type.base, "pt_demo_anchor")
        self.assertEqual(show.params[1].type.pointers, 0)

        callback = self.surface.functions["pt_demo_set_on_event"]
        self.assertEqual(
            [param.name for param in callback.params], ["demo", "callback", "user_data"]
        )
        self.assertEqual(callback.params[1].type.base, "pt_demo_event_callback")
        self.assertIn("pt_demo_event_callback", self.surface.callbacks)
        self.assertIn("pt_demo_anchor", self.surface.structs)
        self.assertIn("pt_demo_mode", self.surface.enums)
        self.assertIn("pt_demo", self.surface.opaque)

    # -- missing detection ----------------------------------------------

    def test_missing_matches_audit_rule(self) -> None:
        expected_counts = {"cpp": 7, "python": 6, "java": 8}
        for language, count in expected_counts.items():
            text = GEN.binding_text(self.fixture.root, language)
            expected = [
                name
                for name in self.functions
                if not re.search(rf"\b{re.escape(name)}\b", text)
            ]
            actual = GEN.binding_missing(
                self.fixture.root, language, self.functions
            )
            self.assertEqual(actual, sorted(expected), language)
            self.assertEqual(len(actual), count, language)

    def test_missing_matches_baseline_api(self) -> None:
        reviewed = GEN.baseline_function_names(self.baseline)
        self.assertEqual(reviewed, set(FIXTURE_API_NAMES))

    # -- generated content ----------------------------------------------

    def test_cpp_signatures_and_ownership(self) -> None:
        serialize = GEN.cpp_stub(self.surface.functions["pt_demo_serialize"], self.surface)
        self.assertIn("std::string serialize() const", serialize)
        self.assertIn("char *value = pt_demo_serialize(handle_)", serialize)
        self.assertIn("pt_free(value)", serialize)
        self.assertIn("TODO(ownership)", serialize)

        show = GEN.cpp_stub(self.surface.functions["pt_demo_show"], self.surface)
        self.assertIn("bool show(pt_demo_anchor anchor)", show)

        callback = GEN.cpp_stub(
            self.surface.functions["pt_demo_set_on_event"], self.surface
        )
        self.assertIn(
            "demo &on_event(pt_demo_event_callback callback, void *data = nullptr)",
            callback,
        )

    def test_python_prototype_trampoline_and_struct(self) -> None:
        serialize = GEN.py_stub(self.surface.functions["pt_demo_serialize"], self.surface)
        self.assertIn('_pt_demo_serialize = _proto("pt_demo_serialize"', serialize)
        self.assertIn("_ctypes.string_at(pointer)", serialize)
        self.assertIn("_pt_free(pointer)", serialize)

        show = GEN.py_stub(self.surface.functions["pt_demo_show"], self.surface)
        self.assertIn("def show(self, anchor: DemoAnchor) -> bool:", show)
        self.assertIn("_pt_demo_show(self._ptr, anchor)", show)

        callback = GEN.py_stub(
            self.surface.functions["pt_demo_set_on_event"], self.surface
        )
        self.assertIn("trampoline = _DEMO_EVENT_CALLBACK(dispatch)", callback)
        self.assertIn("self._keep(trampoline)", callback)
        context = GEN.python_context(
            [self.surface.functions[name] for name in FIXTURE_API_NAMES],
            self.surface,
            "",
        )
        self.assertIn("_DEMO_EVENT_CALLBACK = _ctypes.CFUNCTYPE(", context)
        self.assertIn("class Demo:", context)
        self.assertIn("class DemoAnchor(_ctypes.Structure):", context)

    def test_java_binding_upcall_and_struct(self) -> None:
        serialize = GEN.java_stub(
            self.surface.functions["pt_demo_serialize"], self.surface
        )
        self.assertIn('bind("pt_demo_serialize"', serialize)
        self.assertIn("public static String demo_serialize(MemorySegment demo)", serialize)
        self.assertIn("invoke(free, text)", serialize)

        show = GEN.java_stub(self.surface.functions["pt_demo_show"], self.surface)
        self.assertIn("public static boolean demo_show(MemorySegment demo, DemoAnchor anchor)", show)
        self.assertIn("demo_anchor_layout", show)
        self.assertIn("demo_anchor(arena, anchor)", show)

        callback = GEN.java_stub(
            self.surface.functions["pt_demo_set_on_event"], self.surface
        )
        self.assertIn("DemoEventHandler handler, Arena arena", callback)
        self.assertIn("demo_event_upcall(handler, arena)", callback)

    def test_every_stub_carries_a_todo(self) -> None:
        renderers = {"cpp": GEN.cpp_stub, "python": GEN.py_stub, "java": GEN.java_stub}
        for language, renderer in renderers.items():
            for name in FIXTURE_API_NAMES:
                stub = renderer(self.surface.functions[name], self.surface)
                self.assertIn("TODO", stub, f"{language} {name}")

    # -- determinism and driver -----------------------------------------

    def test_rendering_is_deterministic(self) -> None:
        for language in ("cpp", "python", "java"):
            existing = GEN.binding_text(self.fixture.root, language)
            selected = [self.surface.functions[name] for name in self.functions]
            first = GEN.render_language(language, selected, self.surface, existing)
            second = GEN.render_language(language, selected, self.surface, existing)
            self.assertEqual(first, second, language)

    def test_preview_files_are_identical_across_runs(self) -> None:
        outputs = []
        for suffix in ("a", "b"):
            out = pathlib.Path(
                tempfile.mkdtemp(prefix=f"stub-preview-{suffix}-", dir="/tmp/opencode")
            )
            result = self.run_tool("--out", str(out))
            self.assertEqual(result.returncode, 0, result.stderr)
            outputs.append(out)
        try:
            for language in ("cpp", "python", "java"):
                first = sorted((outputs[0] / language).glob("*"))
                second = sorted((outputs[1] / language).glob("*"))
                self.assertEqual(
                    [path.name for path in first], [path.name for path in second]
                )
                for one, two in zip(first, second):
                    self.assertEqual(
                        one.read_bytes(), two.read_bytes(), str(one.name)
                    )
        finally:
            for out in outputs:
                shutil.rmtree(out, ignore_errors=True)

    def test_check_exit_codes(self) -> None:
        report = self.run_tool("--check")
        self.assertEqual(report.returncode, 0, report.stderr)
        self.assertIn("cpp    missing   7", report.stdout)
        self.assertIn("python missing   6", report.stdout)
        self.assertIn("java   missing   8", report.stdout)
        self.assertIn("result: MISSING", report.stdout)

        strict = self.run_tool("--check", "--strict")
        self.assertEqual(strict.returncode, 1, strict.stderr)

    def test_refuses_to_write_into_bindings(self) -> None:
        result = self.run_tool(
            "--language", "cpp", "--out", str(self.fixture.root / "bindings" / "cpp")
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("refusing to write under bindings/", result.stderr)

    def test_check_rejects_out(self) -> None:
        result = self.run_tool("--check", "--out", "/tmp/opencode/never")
        self.assertEqual(result.returncode, 2)
        self.assertIn("--check does not write files", result.stderr)

    def test_all_mode_covers_the_reviewed_surface(self) -> None:
        result = self.run_tool("--all", "--language", "cpp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout.count("Generated binding skeleton for"),
            len(FIXTURE_API_NAMES),
        )


class RealTreeTests(unittest.TestCase):
    """Guards the generated preview against the checked-in binding sources."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.surface = GEN.parse_surface(ROOT / "include" / "pt")
        cls.functions = sorted(cls.surface.functions)
        cls.coverage = AUDIT.binding_coverage(set(cls.functions))

    def test_missing_sets_match_api_audit(self) -> None:
        for language in ("cpp", "python", "java"):
            generated = GEN.binding_missing(
                ROOT, language, self.functions
            )
            self.assertEqual(generated, self.coverage[language], language)

    def test_missing_surface_tracks_the_live_tree(self) -> None:
        """The expected set is computed from the tree, not a frozen wave list.

        A later wave that adds reviewed functions extends the set; once the
        bindings are fully populated it becomes empty and both assertions
        still hold.
        """
        for language in ("cpp", "python", "java"):
            generated = set(
                GEN.binding_missing(ROOT, language, self.functions)
            )
            expected = set(self.coverage[language])
            self.assertEqual(generated, expected, language)

    def test_strict_check_follows_the_missing_set(self) -> None:
        missing = self.coverage["python"]
        result = subprocess.run(
            [sys.executable, str(TOOL), "--check", "--strict", "--language", "python"],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertIn(f"missing {len(missing):3d}", result.stdout)
        if missing:
            self.assertEqual(result.returncode, 1)
            self.assertIn("result: MISSING", result.stdout)
        else:
            self.assertEqual(result.returncode, 0)
            self.assertIn("result: COMPLETE", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
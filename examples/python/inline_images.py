#!/usr/bin/env python3
"""Pt inline images - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

``TextImage`` carries the byte offset of one U+FFFC placeholder, the raw
``IconImage`` pointer, logical width/height and alignment, plus bounded alt text.
``set_text_images`` copies the list and takes its own image references;
``text_image`` returns a descriptor whose pointer is borrowed; ``insert_image``
inserts at the caret and returns the new byte offset. The example embeds a PNG,
sets two placeholders with different alignment and inserts a third.
"""

from __future__ import annotations

import base64
import os

import pt

ICON_PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAADuklEQVR42u3XW2vbMBjG8X2d"
    "ntdt3aHr2nXb1woYDAKDwRAwAYNBIBAIDIZgCBgMBoHBkO+RpmnXraftTvvfNbC0TVv3Zkzw"
    "3iWvfyR6H8kvXvxfD1ynvV33vffBnfXeU+/cj95basf97L1x573X7tkefOLtuVPvIwXAA+AB"
    "8AB4ADwAHgDvNfXKXXjb3UFm/r6b+Z/ciQ/AB+AD8AH4AHwAPgAfgA/AB+AD8Lfdpf+S2noa"
    "5FgcuJkAIAAIAAKAACAACAACgAAgAAgAAoAAIAAIAGLLXYnNxyGmwWd3HAAIAAQAAgABgGD3"
    "1obnAYAAQAAgABAACDbddbDxMMQ0PHTTEEAIIAQQAgj3lm5yEQIIAYQAQgDhBrW+3PePoi9u"
    "GgGIAEQAov1H/4+XEYAIQAQgWne/orX7ex31AfQB9AH0D568m6/6APoA+gD69wAm8Vd3FAOI"
    "AcQA4g4AMYAYQAwgXnO/49Xbe04GAAYABgAGnzub5esBgAGAAYDBLYBJ8s1NEgAJgARA0iEg"
    "AZAASAAkq9TK370nKYAUQAogPbz34WcpOZCSAyk5kO7c+/nrFEAKIAWQLgJIABKABCBvADPJ"
    "GErGUJIDkhyQBJEkiCQACUACkASRJIgkOSDJAckYSsZQ3iThtQQgAUgAchFAAVAAFAA1B1AA"
    "FAAFQAFQABQABUABUAAUAAVAAVAAFAAFQM0BFAAFQAFQiwAagAagAeg5gAagAWgAGoAGoAFo"
    "ABqABqABaAAagAagAWgAeg6gAWgAGoBeBDAADAADwMwBDAADwAAwAAwAA8AAMAAMAAPAADAA"
    "DAADwAAwcwADwAAwAMwiQAYgA5AByJbYhBmADEAGIFtiE2YAMgAZgGwRIAeQA8gB5Ixh3uEY"
    "5oxhDiAHkAPIV27JgiGAIYAhgGGHgCGAIYAhgOFdSVgAKAAUAAqiuOggiguiuABQACgAFKt3"
    "9zwaARgBGAEYdQAYARgBGAEYLXMalgBKACWAkuO4fMJxXHIclwBKACWAcm25XtMKQAWgAlAx"
    "hhVjWD3gQlIxhhVjWAGoAFQAqvUH3opqADWAGkANoCYHanKgvuNKVpMDNTlQA6gB1ABqAPXG"
    "437FYwvAArAALAALwBJEliCy5IAlByw5YAkiSxBZABaABWABWAB282n7aNYAaAA0ABoADYAG"
    "QAOgAdAAaAA0ABoADYAGQAOg2eru/eCkBdACaAG0AFoALYAWQAugBdACaAG028/3hnQ6BjAG"
    "MAYwBjAGMAYwBjB+xlezf3b9AeJbDQsb/1nuAAAAAElFTkSuQmCC"
)

INITIAL_TEXT = "Build: \uFFFC passed \uFFFC pending"
FIRST_START = 7
SECOND_START = 18


def make_image(
    start: int,
    icon: pt.IconImage,
    width: float,
    height: float,
    align: int,
    alt: bytes,
) -> pt.TextImage:
    return pt.TextImage(
        start=start,
        image=icon.raw,
        width=width,
        height=height,
        align=align,
        alt=alt,
    )


def main() -> int:
    app = pt.App("dev.pt.examples.inline_images.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)

    window = pt.Window(app, "Pt Inline Images - Python", 720, 480)
    root = window.root
    root.set_padding(24, 24, 24, 24).set_gap(12)

    pt.label(root, "PYTHON BINDING  ·  U+FFFC  ·  SIDE TABLE").set_font_size(
        11
    ).set_font_weight(pt.font_weight_semibold).set_foreground(pt.rgb(0x7AA2F7))

    pt.label(
        root,
        "The text holds two object replacement characters. One image sits on the "
        "baseline, the other is centred; a third is inserted at the caret.",
    ).set_font_size(13).set_wrap(True).set_foreground(pt.rgb(0x9AA7B8))

    area = pt.text_area(root, "Build log").set_size(600, 140)
    area.text = INITIAL_TEXT

    icon = pt.IconImage.load_memory(ICON_PNG)
    if icon is None:
        print("pt: could not load the embedded PNG", flush=True)
        return 1

    # start 0 points at 'B', not a placeholder, so the setter drops that entry.
    area.set_text_images(
        [
            make_image(
                FIRST_START, icon, 20, 20, pt.text_image_align_baseline, b"Passed"
            ),
            make_image(
                SECOND_START, icon, 24, 24, pt.text_image_align_middle, b"Pending"
            ),
            make_image(0, icon, 16, 16, pt.text_image_align_baseline, b"Invalid"),
        ]
    )

    length = len(area.text.encode("utf-8"))
    area.set_selection(length, length)
    inserted_start = area.insert_image(
        make_image(0, icon, 18, 18, pt.text_image_align_baseline, b"Working")
    )
    icon.destroy()

    count = area.text_image_count()
    first = area.text_image(0)
    second = area.text_image(1)

    failures = 0
    if inserted_start != 29:
        failures += 1
    if count != 3 or first is None or second is None:
        failures += 1
    if first is None or first.align != pt.text_image_align_baseline or first.alt != b"Passed":
        failures += 1
    if second is None or second.align != pt.text_image_align_middle or second.alt != b"Pending":
        failures += 1

    summary = (
        f"entries={count}  inserted_at={inserted_start}  "
        f"first={first.align if first else -1}/"
        f"{(first.alt.decode('utf-8', 'replace') if first else '')}  "
        f"second={second.align if second else -1}/"
        f"{(second.alt.decode('utf-8', 'replace') if second else '')}  "
        f"text_bytes={len(area.text.encode('utf-8'))}"
    )
    pt.label(root, summary).set_font_size(12).set_foreground(pt.rgb(0x9ECE6A)).set_wrap(
        True
    )
    pt.label(
        root,
        "The text still contains EF BF BC; the side table is an index over it. "
        "Selection and bidi offsets are unchanged.",
    ).set_font_size(12).set_wrap(True).set_foreground(pt.rgb(0x6F7C8F))

    pt.button(root, "Quit").set_button_variant(pt.button_ghost).on_click(window.close)
    window.show()

    if os.environ.get("PT_HEADLESS", "") not in ("", "0"):
        print(
            "inline_images: "
            f"count={count} inserted_start={inserted_start} "
            f"first_align={first.align if first else -1} "
            f"second_align={second.align if second else -1} "
            f"second_alt={(second.alt.decode('utf-8', 'replace') if second else '')} "
            f"text_bytes={len(area.text.encode('utf-8'))}",
            flush=True,
        )
        status = app.run_for(250)
    else:
        app.run()
        status = 0
    return 1 if failures else status


if __name__ == "__main__":
    raise SystemExit(main())

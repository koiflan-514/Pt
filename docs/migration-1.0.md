# Migrating to Pt 1.0

This guide is for applications written against 0.8, 0.9 or 0.10. There is
exactly **one** source and binary break in 1.0: the two drag helpers that 0.9
deprecated were removed by the freeze. Everything else in this document is an
addition, a default-value change or a behavior that was already documented in
the release notes.

If you are starting a new application, read
[quickstart.md](quickstart.md); this document is a delta.

## Version and object name

1.0 is a new release line:

- `PT_VERSION_MAJOR/MINOR/PATCH` and `pt_version_string` report `1.0.0`.
- The SONAME changes from `libpt.so.0` to `libpt.so.1`, because the frozen ABI
  is a deliberate subset of 0.10 rather than a superset. A binary linked
  against `libpt.so.0` is not loadable against the new library.
- Rebuild and relink every consumer. For an installed prefix:

  ```sh
  pkg-config --modversion pt          # 1.0.0
  pkg-config --cflags --libs pt
  ```

- The static archive, pkg-config file, headers, C++ header and Python/Java
  binding sources stay in the same prefix layout. No include path moved.

## The 1.0 removals

`pt_window_begin_text_drag` and `pt_window_begin_uri_drag` are gone from the C
headers, the library, and the C++, Python and Java wrappers.
`pt_window_begin_drag` is the only drag source. It has been available since
0.8 and covers both cases through the MIME types the caller offers.

### C

Before (0.8–0.10):

```c
/* While a pointer press is active. */
pt_window_begin_text_drag(window, "line one\nline two");
pt_window_begin_uri_drag(window, uris, uri_count);
```

After (1.0):

```c
pt_drag_data text_offer = {
    .mime_type = "text/plain;charset=utf-8",
    .data = "line one\nline two",
    .size = 0, /* 0 means NUL-terminated */
};
pt_window_begin_drag(window, &text_offer, 1);
```

A URI list is caller-composed now. The old helper joined the URIs with CRLF and
skipped empty entries and entries containing `\r` or `\n`; the replacement does
the same if you keep that policy:

```c
static bool begin_uri_drag(pt_window *window, const char *const *uris, uint32_t count) {
    size_t total = 1;
    for (uint32_t i = 0; i < count; ++i) {
        const char *uri = uris[i];
        if (!uri || !*uri || strpbrk(uri, "\r\n")) {
            continue;
        }
        total += strlen(uri) + 2;
    }
    char *list = malloc(total);
    if (!list) {
        return false;
    }
    char *write = list;
    for (uint32_t i = 0; i < count; ++i) {
        const char *uri = uris[i];
        if (!uri || !*uri || strpbrk(uri, "\r\n")) {
            continue;
        }
        size_t length = strlen(uri);
        memcpy(write, uri, length);
        write += length;
        *write++ = '\r';
        *write++ = '\n';
    }
    *write = '\0';
    pt_drag_data offer = {.mime_type = "text/uri-list", .data = list, .size = 0};
    bool started = pt_window_begin_drag(window, &offer, 1);
    free(list); /* Pt copies the payload before returning. */
    return started;
}
```

### C++

Before:

```cpp
window.begin_text_drag("line one\nline two");
window.begin_uri_drag({"file:///home/user/notes.txt"});
```

After:

```cpp
window.begin_drag({pt::drag_data{"text/plain;charset=utf-8", "line one\nline two", 0}});
window.begin_drag({pt::drag_data{"text/uri-list", "file:///home/user/notes.txt\r\n", 0}});
```

`pt::window::begin_drag` accepts a `std::span<const pt_drag_data>` or an
initializer list; multiple entries are one drag offering several MIME types.

### Python

Before:

```python
window.begin_text_drag("line one\nline two")
window.begin_uri_drag(["file:///home/user/notes.txt"])
```

After:

```python
window.begin_drag([("text/plain;charset=utf-8", "line one\nline two")])
window.begin_drag([("text/uri-list", "file:///home/user/notes.txt\r\n")])
```

Each entry may be a `(mime_type, data)` pair, a `(mime_type, data, size)`
triple, a mapping or a `pt.DragData`. The old methods no longer raise
`DeprecationWarning`; they are absent.

### Java

Before:

```java
Pt.window_begin_text_drag(window, "line one\nline two");
Pt.window_begin_uri_drag(window, new String[] {"file:///home/user/notes.txt"});
```

After:

```java
Pt.window_begin_drag(window, new Pt.DragData[] {
    new Pt.DragData("text/plain;charset=utf-8", "line one\nline two", 0)
});
Pt.window_begin_drag(window, new Pt.DragData[] {
    new Pt.DragData("text/uri-list", "file:///home/user/notes.txt\r\n", 0)
});
```

### Why this was a removal rather than a keep

Removal after 1.0 would have required a major release, so carrying the two
names through `1.0.0` would have kept them for the entire 1.x series. The
freeze executed the window recorded at 0.9.0 instead. `make api-update`
recorded the change as a two-entry baseline diff, and the library now exports
412 symbols, all wrapped in all three bindings.

## 0.9: hard-coded colors became tokens

0.9 did not remove a color API. It moved the built-in paints from copied
literals to a semantic table, added 14 roles and 10 metrics, and added the
public resolution helpers. The practical migration for a custom widget is:

Before (0.8), pinning a literal copy of the palette onto built-in widgets:

```c
pt_widget_set_background(card, 0x11151EFFu);
pt_widget_set_foreground(title, 0xE9EEF7FFu);
```

After, prefer no pin at all so the widget follows the live theme:

```c
/* No call: the card and label use pt_theme_surface and pt_theme_text. */
```

When an application genuinely needs the value at runtime — to build a custom
canvas-like surface of its own, or to store a color for later — read it from
the app or window instead of copying the table:

```c
pt_color surface = pt_app_get_theme_color(app, pt_theme_surface);
pt_color outline = pt_window_get_theme_color(window, pt_theme_outline);
float radius = pt_window_get_theme_metric(window, pt_theme_metric_radius_medium);
```

The 0.8 token set keeps its numeric values. The 0.9 additions are the
container, outline, disabled-text, pressed-accent, soft-status, scrim,
skeleton and selection-text roles, plus the metric table. The theming guide
lists all of them: [theming.md](theming.md).

Two behavior notes:

- The 1.0 default palette is **not** the 0.9 palette. The accents moved
  (dark `5B8CFF` → `3568E0`, light `2F6BFF` → `2559E6`) and the light status
  colors moved (`DC3D48` → `B92C38`, `16A34A` → `0F7A37`, `B7790A` → `8A5A00`)
  because the 0.9 values measured between 3.2:1 and 4.4:1 against their
  surfaces. If you pinned a literal to match the old theme, re-check it; if you
  call `pt_app_set_accent_color`, the derived roles follow your color and the
  fix does not apply to you.
- `pt_app_set_accent_color` derives `accent_hover`, `accent_pressed`,
  `accent_text`, `accent_soft`, `focus` and `selection` from the accent and the
  active scheme. If you also call `pt_app_set_theme_color` for one of those
  roles, order matters: the last call wins, and a later
  `pt_app_set_color_scheme` resets all of them along with every other app
  override.

Window overrides are new in 0.9 and unchanged in 1.0:

```c
pt_window_set_theme_color(window, pt_theme_accent, pt_rgb(0x9ECE6A));
pt_window_set_theme_metric(window, pt_theme_metric_radius_medium, 12.0f);
pt_window_clear_theme_overrides(window);
```

## 0.10: the selection vocabulary

0.10 added 13 functions that give lists, tables and trees one multi-row
selection model. Nothing was removed, so a 0.9 application keeps compiling; the
migration is about using the set instead of faking it.

Before (0.9), a table-only range and a single primary row:

```c
pt_table_set_selected(table, row);
/* No supported way to ask "which rows are highlighted?" */
```

After (1.0), the same calls on a list, a table or a tree:

```c
pt_list_view_select_range(list, 2, 5);       /* inclusive, clamped */
pt_list_view_select_toggle(list, 7);         /* Ctrl-click semantics */
uint32_t rows[16];
uint32_t count = pt_list_view_get_selection(list, rows, 16);
bool is_selected = pt_list_view_is_row_selected(list, 7);
pt_list_view_clear_selection(list);

/* The table and tree mirrors are pt_table_select_toggle,
 * pt_tree_select_range, pt_tree_select_toggle, pt_tree_is_selected,
 * pt_tree_get_selection and pt_tree_clear_selection. */
```

Contract in one paragraph: ranges are inclusive and clamped; the setter
replaces the set and leaves the anchor and the primary row at the first row;
toggling moves the primary row, and toggling the primary off moves it to the
next selected row; `clear` leaves the view at `pt_list_index_none`. The set is
separate from the primary row, so `pt_list_view_get_selected` still names the
caret row. The public set helpers are silent so an application can rebuild a
selection without echoing; the keyboard, pointer and marquee gestures report
the new primary row through `on_select`. `tests/test_selection.c` drives one
gesture script through all three views.

The accessibility model gained `selectable` and `multi_selectable` in the same
release, so a screen reader can offer "add to selection" on an unselected row.
The accessibility guide describes the mapping: [accessibility.md](accessibility.md).

## 1.0 additions that need no migration

- Every public header now has a `Header contract` block with the threading
  rule, ownership, lifetimes and error behavior. The rules are the ones the
  existing functions already followed; the prose makes them reviewable. The
  generated reference renders each block as its module preamble.
- `tests/test_abi.c` pins every public enum value, state bit, sentinel and
  struct layout. Application code is unaffected; it exists so a future release
  fails its own build before it can renumber something.
- SONAME, version macros and pkg-config agree on `1.0.0` / `libpt.so.1`.
- The generated API reference and the compatibility policy describe the frozen
  surface: [api-reference.md](api-reference.md), [api-policy.md](api-policy.md).

## Migrating a build

1. Read the removal section; replace the two calls.
2. Rebuild against the 1.0 headers with the `werror=1` settings you use, so a
   stale prototype is a compile error rather than a runtime surprise.
3. Relink and redeploy the `libpt.so.1` object; remove any pin that names
   `libpt.so.0`.
4. Run your tests once in dark and once in light to review the palette change
   if you ship a UI screenshot or a contrast-sensitive surface.
5. If you enabled the AT-SPI bridge, re-run your assistive-technology smoke
   test; the text interfaces on labels and text areas are part of 1.0.

## If you stay on 0.x

0.10 remains the last 0.x release and is not maintained with fixes. There is no
compatibility shim: the two removed wrappers cannot be restored without
reintroducing the symbols the freeze removed. Pinning `libpt.so.0` keeps an
unmodified 0.x application running on the old library, but new work should
target 1.0.

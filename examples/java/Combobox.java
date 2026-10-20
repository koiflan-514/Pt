/*
 * Pt combobox and multi-row selection: pick a region, filter a font list and
 * drive range/toggle selection on a virtualized list.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import pt.Pt;

public final class Combobox {
    private Combobox() {}

    /* label, borrowed payload */
    private static final String[][] PLACES = {
        {"Europe/Berlin", "UTC+1 · 148 stations"},
        {"Europe/Lisbon", "UTC+0 · 63 stations"},
        {"Asia/Tokyo", "UTC+9 · 211 stations"},
        {"Asia/Kolkata", "UTC+5:30 · 174 stations"},
        {"America/Denver", "UTC-7 · 92 stations"},
        {"Pacific/Auckland", "UTC+13 · 41 stations"},
    };

    private static final String[] FONTS = {
        "JetBrains Mono", "DejaVu Sans", "Noto Sans CJK", "Inter", "Source Serif",
        "IBM Plex Mono", "Cascadia Code", "Roboto Flex", "Terminus",
    };

    private static final String[] FILES = {
        "api/pt/pt.h", "src/combobox.c", "src/tree.c", "src/table.c", "src/widget.c",
        "bindings/cpp/pt", "bindings/python", "bindings/java", "tests/test_a11y.c",
        "docs/bindings.md",
    };

    @SuppressWarnings("restricted")
    private static String text(MemorySegment segment) {
        return segment.address() == 0 ? "" : segment.reinterpret(Long.MAX_VALUE).getString(0);
    }

    private static boolean headless() {
        String value = System.getenv("PT_HEADLESS");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    private static void fillRegions(MemorySegment box, Arena arena) {
        Pt.combobox_clear(box);
        for (int index = 0; index < PLACES.length; index++) {
            Pt.combobox_insert_item(box, index, PLACES[index][0]);
            /* The payload lives in the arena, which outlives the widget. */
            Pt.combobox_set_item_data(box, index, arena.allocateFrom(PLACES[index][1]));
        }
        /* Disabled rows stay visible but cannot be highlighted or chosen. */
        Pt.combobox_set_item_disabled(box, 1, true);
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.examples.combobox.java");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x4C8DFF));

            MemorySegment window = Pt.window_create(app, "Pt Combobox", 880, 600);
            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment eyebrow = Pt.label(root, "COMBOBOX  ·  SELECTION  ·  0.10");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x4C8DFF));
            MemorySegment heading = Pt.label(root, "One field, a list of choices");
            Pt.widget_set_font_size(heading, 30.0f);
            Pt.widget_set_font_weight(heading, Pt.font_weight_semibold);
            MemorySegment intro = Pt.label(root,
                    "A combobox is a closed pick list built on the same overlay primitives as "
                    + "the rest of the toolkit. Filterable mode puts a real editor in the field. "
                    + "Selection in the data views is a set, so ranges and toggles behave the "
                    + "same in a list, a table and a tree.");
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA4B2));

            MemorySegment status = Pt.label(root, "Ready");
            Pt.widget_set_font_size(status, 12.0f);
            Pt.widget_set_foreground(status, Pt.rgb(0x8B98AA));
            Pt.widget_set_a11y_name(status, "Status");

            MemorySegment boxes = Pt.row(root);
            Pt.widget_set_gap(boxes, 14.0f);

            MemorySegment regionCard = Pt.card(boxes);
            Pt.widget_set_grow(regionCard, 1.0f);
            Pt.widget_set_padding(regionCard, 16.0f, 16.0f, 16.0f, 16.0f);
            Pt.widget_set_gap(regionCard, 10.0f);
            Pt.label(regionCard, "Select only");
            MemorySegment regionBox = Pt.combobox(regionCard);
            Pt.widget_set_min_width(regionBox, 220.0f);
            Pt.combobox_set_placeholder(regionBox, "Choose a region");
            Pt.combobox_set_popup_max_height(regionBox, 190.0f);
            Pt.widget_set_a11y_name(regionBox, "Region");
            Pt.widget_set_a11y_description(regionBox,
                    "Closed list of regions; rows carry a borrowed payload pointer.");
            MemorySegment detail = Pt.label(regionCard, "Pick a region to see its payload.");
            Pt.widget_set_font_size(detail, 12.0f);
            Pt.widget_set_wrap(detail, true);
            Pt.widget_set_foreground(detail, Pt.rgb(0x8B98AA));
            fillRegions(regionBox, arena);

            Pt.IndexHandler chooseRegion = (widget, index, data) -> {
                String name = Pt.combobox_get_item_text(widget, index);
                MemorySegment payload = Pt.combobox_get_item_data(widget, index);
                if (payload.address() == 0) {
                    Pt.widget_set_text(detail, "That row carries no payload.");
                    Pt.widget_set_text(status, "Chose an item without data");
                    return;
                }
                Pt.widget_set_text(detail, name + " · " + text(payload));
                Pt.widget_set_text(status, "Selected " + name);
                Pt.window_show_toast(window, "Selected " + name, Pt.toast_info, 2000);
            };
            Pt.combobox_set_on_change(regionBox, chooseRegion, arena);

            Pt.ClickHandler togglePopup = (widget, data) -> {
                if (Pt.combobox_is_open(regionBox)) {
                    Pt.combobox_close(regionBox);
                    Pt.widget_set_text(status, "Popup closed");
                    return;
                }
                Pt.combobox_open(regionBox);
                Pt.widget_set_text(status, "Popup open · max height "
                        + (int) Pt.combobox_get_popup_max_height(regionBox) + " px");
            };
            Pt.ClickHandler selectByCode = (widget, data) -> {
                /* Programmatic selection is silent, so the status line is written here. */
                Pt.combobox_set_selected(regionBox, 4);
                chooseRegion.handle(regionBox, 4, MemorySegment.NULL);
                Pt.widget_set_text(detail, "Set from code: no on_change fired.");
            };
            Pt.ClickHandler reloadItems = (widget, data) -> {
                fillRegions(regionBox, arena);
                Pt.widget_set_text(status,
                        "Reloaded " + Pt.combobox_get_item_count(regionBox) + " regions");
            };

            MemorySegment regionTools = Pt.wrap(regionCard);
            Pt.wrap_set_gap(regionTools, 8.0f, 8.0f);
            MemorySegment popupButton = Pt.button(regionTools, "Toggle popup");
            Pt.button_set_variant(popupButton, Pt.button_secondary);
            Pt.button_set_on_click(popupButton, togglePopup, arena);
            MemorySegment codeButton = Pt.button(regionTools, "Select by code");
            Pt.button_set_variant(codeButton, Pt.button_ghost);
            Pt.button_set_on_click(codeButton, selectByCode, arena);
            MemorySegment reloadButton = Pt.button(regionTools, "Reload items");
            Pt.button_set_variant(reloadButton, Pt.button_ghost);
            Pt.button_set_on_click(reloadButton, reloadItems, arena);

            MemorySegment fontCard = Pt.card(boxes);
            Pt.widget_set_grow(fontCard, 1.0f);
            Pt.widget_set_padding(fontCard, 16.0f, 16.0f, 16.0f, 16.0f);
            Pt.widget_set_gap(fontCard, 10.0f);
            Pt.label(fontCard, "Filterable");
            MemorySegment fontBox = Pt.combobox(fontCard);
            Pt.widget_set_min_width(fontBox, 220.0f);
            Pt.combobox_set_mode(fontBox, Pt.combobox_filterable);
            Pt.combobox_set_placeholder(fontBox, "Type to filter fonts");
            Pt.widget_set_a11y_name(fontBox, "Font family");
            for (String font : FONTS) {
                Pt.combobox_add_item(fontBox, font);
            }
            int[] typed = {0};
            Pt.TextHandler fontText = (widget, value, data) -> {
                typed[0]++;
                Pt.widget_set_text(status, "Typed \"" + text(value) + "\" · keystroke "
                        + typed[0]);
            };
            Pt.combobox_set_on_text_change(fontBox, fontText, arena);
            Pt.combobox_set_on_change(fontBox, (widget, index, data) -> Pt.widget_set_text(status,
                    "Font chosen: " + Pt.combobox_get_text(widget)), arena);

            Pt.ClickHandler switchMode = (widget, data) -> {
                boolean wasFilterable = Pt.combobox_get_mode(fontBox) == Pt.combobox_filterable;
                Pt.combobox_set_mode(fontBox, wasFilterable ? Pt.combobox_select_only
                        : Pt.combobox_filterable);
                Pt.widget_set_text(status,
                        wasFilterable ? "Font box is now select only" : "Font box is now filterable");
            };
            Pt.ClickHandler removeFirst = (widget, data) -> {
                if (Pt.combobox_get_item_count(fontBox) == 0) {
                    Pt.widget_set_text(status, "No items left in the font list");
                    return;
                }
                String first = Pt.combobox_get_item_text(fontBox, 0);
                Pt.combobox_remove_item(fontBox, 0);
                Pt.widget_set_text(status, "Removed \"" + first + "\" · "
                        + Pt.combobox_get_item_count(fontBox) + " left");
            };

            MemorySegment fontTools = Pt.wrap(fontCard);
            Pt.wrap_set_gap(fontTools, 8.0f, 8.0f);
            MemorySegment modeButton = Pt.button(fontTools, "Switch mode");
            Pt.button_set_variant(modeButton, Pt.button_secondary);
            Pt.button_set_on_click(modeButton, switchMode, arena);
            MemorySegment trimButton = Pt.button(fontTools, "Remove first item");
            Pt.button_set_variant(trimButton, Pt.button_ghost);
            Pt.button_set_on_click(trimButton, removeFirst, arena);

            MemorySegment listCard = Pt.card(root);
            Pt.widget_set_grow(listCard, 1.0f);
            Pt.widget_set_padding(listCard, 16.0f, 16.0f, 16.0f, 16.0f);
            Pt.widget_set_gap(listCard, 10.0f);
            MemorySegment list = Pt.list_view(listCard);
            Pt.widget_set_grow(list, 1.0f);
            Pt.widget_set_min_height(list, 190.0f);
            Pt.widget_set_a11y_name(list, "Files");
            Pt.list_view_set_row_height(list, 34.0f);
            Pt.list_view_set_row_count(list, FILES.length);
            Pt.list_view_set_on_bind_row(list, (handle, row, index, data) -> {
                if (index >= FILES.length) {
                    Pt.widget_set_text(row, "");
                    return;
                }
                Pt.widget_set_text(row, FILES[index]);
                Pt.widget_set_icon(row, Pt.icon_document);
            }, arena);

            Runnable describe = () -> {
                int[] held = Pt.list_view_get_selection(list);
                StringBuilder indexes = new StringBuilder();
                for (int row : held) {
                    indexes.append(indexes.isEmpty() ? "" : ", ").append(row);
                }
                Pt.widget_set_text(status, "Selection has " + held.length + " of " + FILES.length
                        + " rows [" + (indexes.isEmpty() ? "none" : indexes) + "] · primary "
                        + (Pt.list_view_get_selected(list) == Pt.list_index_none ? "cleared"
                                : "set"));
            };
            Pt.list_view_set_on_select(list, (handle, index, data) -> describe.run(), arena);

            Pt.ClickHandler selectRange = (widget, data) -> {
                Pt.list_view_select_range(list, 2, 4);
                Pt.widget_set_text(status, "Range 2-4 applied");
                describe.run();
            };
            Pt.ClickHandler toggleRow = (widget, data) -> {
                int row = Pt.list_view_is_row_selected(list, 6) ? 3 : 6;
                Pt.list_view_select_toggle(list, row);
                Pt.widget_set_text(status, "Toggled row " + row);
                describe.run();
            };
            Pt.ClickHandler clearRows = (widget, data) -> {
                Pt.list_view_clear_selection(list);
                Pt.widget_set_text(status, "Selection cleared");
                describe.run();
            };

            MemorySegment rowTools = Pt.wrap(listCard);
            Pt.wrap_set_gap(rowTools, 8.0f, 8.0f);
            MemorySegment rangeButton = Pt.button(rowTools, "Range 2-4");
            Pt.button_set_variant(rangeButton, Pt.button_primary);
            Pt.button_set_on_click(rangeButton, selectRange, arena);
            MemorySegment toggleButton = Pt.button(rowTools, "Toggle 6");
            Pt.button_set_variant(toggleButton, Pt.button_secondary);
            Pt.button_set_on_click(toggleButton, toggleRow, arena);
            MemorySegment clearButton = Pt.button(rowTools, "Clear");
            Pt.button_set_variant(clearButton, Pt.button_ghost);
            Pt.button_set_on_click(clearButton, clearRows, arena);

            MemorySegment footer = Pt.label(root,
                    "The popup flips above the field near the bottom of the window. "
                    + "Marquee-drag or Shift-click a row to extend the selection set.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_wrap(footer, true);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            Pt.window_show(window);
            Pt.window_focus(window, regionBox);

            if (!headless()) {
                int running = Pt.app_run(app);
                Pt.app_destroy(app);
                System.exit(running);
            }
            /* Headless: drive the handlers the buttons would call. */
            Pt.widget_set_text(status, "role " + Pt.widget_get_a11y_role(regionBox)
                    + " · expandable " + ((Pt.widget_get_a11y_state(regionBox)
                            & Pt.a11y_state_expandable) != 0));
            togglePopup.handle(MemorySegment.NULL, MemorySegment.NULL);
            chooseRegion.handle(regionBox, 0, MemorySegment.NULL);
            selectByCode.handle(MemorySegment.NULL, MemorySegment.NULL);
            switchMode.handle(MemorySegment.NULL, MemorySegment.NULL);
            fontText.handle(fontBox, arena.allocateFrom("jet"), MemorySegment.NULL);
            removeFirst.handle(MemorySegment.NULL, MemorySegment.NULL);
            reloadItems.handle(MemorySegment.NULL, MemorySegment.NULL);
            selectRange.handle(MemorySegment.NULL, MemorySegment.NULL);
            toggleRow.handle(MemorySegment.NULL, MemorySegment.NULL);
            toggleRow.handle(MemorySegment.NULL, MemorySegment.NULL);
            clearRows.handle(MemorySegment.NULL, MemorySegment.NULL);
            int running = Pt.app_run_for(app, 250);
            Pt.app_destroy(app);
            System.exit(running);
        }
    }
}

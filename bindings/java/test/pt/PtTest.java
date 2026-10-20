/*
 * Headless tests for the Pt Java (FFM) binding.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

package pt;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class PtTest {
    private PtTest() {}

    private static int checks = 0;
    private static int failures = 0;

    private static void check(boolean condition, String label) {
        checks += 1;
        if (!condition) {
            failures += 1;
            System.err.println("FAIL: " + label);
        }
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.tests.java");
            check(app.address() != 0, "app_create");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_light);
            Pt.app_set_accent_color(app, Pt.rgb(0x4C9AFF));
            Pt.app_set_font_family(app, "monospace");
            Pt.app_set_reduced_motion(app, false);
            check(!Pt.app_get_reduced_motion(app), "reduced motion off");
            Pt.app_set_reduced_motion(app, true);
            check(Pt.app_get_reduced_motion(app), "reduced motion on");
            Pt.app_set_reduced_motion(app, false);
            check(Pt.app_get_a11y_status(app) == Pt.a11y_status_disabled, "a11y disabled");
            boolean a11y_enabled = Pt.app_enable_a11y(app);
            check(a11y_enabled == (Pt.app_get_a11y_status(app) == Pt.a11y_status_active),
                    "a11y enable status");
            check(Pt.app_get_a11y_status(app) != Pt.a11y_status_disabled, "a11y started");
            Pt.app_disable_a11y(app);
            check(Pt.app_get_a11y_status(app) == Pt.a11y_status_disabled, "a11y stopped");
            check(Math.abs(Pt.ease(Pt.easing_linear, 0.5f) - 0.5f) < 0.0001f, "linear ease");
            check(Math.abs(Pt.ease_value(Pt.easing_linear, 10.0f, 30.0f, 0.25f) - 15.0f)
                    < 0.0001f, "ease value");
            check(Pt.app_get_theme_color(app, Pt.theme_accent) == Pt.rgb(0x4C9AFF),
                    "theme accent");
            check(Pt.app_get_theme_color(app, Pt.theme_control)
                    != Pt.app_get_theme_color(app, Pt.theme_track), "theme tokens");

            MemorySegment window = Pt.window_create(app, "java binding", 420, 320);
            check(window.address() != 0, "window_create");
            check(Pt.window_get_width(window) == 420, "window width");
            check(Pt.window_get_height(window) == 320, "window height");
            check(Math.abs(Pt.app_get_theme_metric(app, Pt.theme_metric_radius_small)
                    - 6.0f) < 0.001f, "radius metric");
            Pt.app_set_theme_metric(app, Pt.theme_metric_chip_height, 30.0f);
            check(Math.abs(Pt.app_get_theme_metric(app, Pt.theme_metric_chip_height)
                    - 30.0f) < 0.001f, "app metric override");
            Pt.window_set_theme_color(window, Pt.theme_background, 0x102030FF);
            Pt.window_set_theme_metric(window, Pt.theme_metric_focus_width, 3.0f);
            check(Pt.window_get_theme_color(window, Pt.theme_background) == 0x102030FF,
                    "window color override");
            check(Math.abs(Pt.window_get_theme_metric(window, Pt.theme_metric_focus_width)
                    - 3.0f) < 0.001f, "window metric override");
            check(Pt.app_get_theme_color(app, Pt.theme_background) != 0x102030FF,
                    "window override isolation");
            Pt.window_clear_theme_overrides(window);
            check(Pt.window_get_theme_color(window, Pt.theme_background)
                    == Pt.app_get_theme_color(app, Pt.theme_background),
                    "window override clear");

            MemorySegment root = Pt.window_root(window);
            check(root.address() != 0, "window root");
            MemorySegment column = Pt.column(root);
            Pt.widget_set_gap(column, 8.0f);
            Pt.widget_set_padding(column, 12.0f, 12.0f, 12.0f, 12.0f);

            MemorySegment label = Pt.label(column, "hello");
            check(Pt.widget_get_text(label).equals("hello"), "label text");
            Pt.widget_set_text(label, "changed");
            check(Pt.widget_get_text(label).equals("changed"), "label set_text");
            Pt.widget_set_font_size(label, 18.0f);
            Pt.widget_set_font_weight(label, Pt.font_weight_bold);
            Pt.widget_set_text(label, "styled label");
            Pt.TextSpan[] spans = {new Pt.TextSpan(0, 6, Pt.font_weight_bold,
                    Pt.font_slant_italic, 0xFF2288FF,
                    Pt.text_span_underline | Pt.text_span_has_weight
                            | Pt.text_span_has_slant | Pt.text_span_has_color
                            | Pt.text_span_has_family | Pt.text_span_has_size,
                    "DejaVu Serif", 24.0f)};
            check(Pt.widget_set_text_spans(label, spans), "set text spans");
            check(Pt.widget_get_text_span_count(label) == 1, "text span count");
            Pt.TextSpan stored = Pt.widget_get_text_span(label, 0);
            check(stored != null, "get text span");
            check(stored != null && stored.start() == 0 && stored.length() == 6,
                    "text span range");
            check(stored != null && stored.weight() == Pt.font_weight_bold
                    && stored.slant() == Pt.font_slant_italic, "text span attrs");
            check(stored != null && stored.color() == 0xFF2288FF, "text span color");
            check(stored != null && stored.family().equals("DejaVu Serif")
                    && Math.abs(stored.size() - 24.0f) < 0.0001f, "text span font");
            check(Pt.widget_get_text_span(label, 1) == null, "text span bounds");
            Pt.widget_clear_text_spans(label);
            check(Pt.widget_get_text_span_count(label) == 0, "clear text spans");
            Pt.widget_set_text(label, "changed");

            MemorySegment button = Pt.button(column, "Press");
            Pt.button_set_variant(button, Pt.button_primary);
            Pt.button_set_on_click(button, (widget, data) -> { }, arena);
            Pt.widget_set_busy(button, true);
            check(Pt.widget_get_busy(button), "widget busy");
            Pt.widget_set_busy(button, false);
            check(!Pt.widget_get_busy(button), "widget idle");
            Pt.widget_set_invalid(button, true);
            check(Pt.widget_get_invalid(button), "widget invalid");
            Pt.widget_set_invalid(button, false);
            check(!Pt.widget_get_invalid(button), "widget valid");

            MemorySegment chip = Pt.chip(column, "filter");
            Pt.chip_set_removable(chip, true);
            Pt.chip_set_selected(chip, true);
            Pt.chip_set_icon(chip, Pt.icon_check);
            check(Pt.chip_get_removable(chip) && Pt.chip_get_selected(chip), "chip state");
            Pt.chip_set_on_click(chip, (widget, data) -> { }, arena);
            Pt.chip_set_on_remove(chip, (widget, data) -> { }, arena);
            check(Pt.skeleton(column).address() != 0, "skeleton");

            MemorySegment edit_table = Pt.table(column);
            Pt.table_set_column_count(edit_table, 3);
            Pt.table_set_column_title(edit_table, 0, "Name");
            Pt.table_set_row_count(edit_table, 5);
            final int[] edit = {-1, -1};
            final String[] edit_text = {""};
            Pt.table_set_on_cell_edited(edit_table, (target, row, column_index, text, data) -> {
                edit[0] = row;
                edit[1] = column_index;
                edit_text[0] = text;
            }, arena);
            Pt.table_set_editable(edit_table, true);
            check(Pt.table_begin_edit(edit_table, 1, 0), "begin cell edit");
            check(Pt.table_is_editing(edit_table), "table editing");
            check(Pt.table_commit_edit(edit_table), "commit cell edit");
            check(!Pt.table_is_editing(edit_table), "table edit finished");
            check(edit[0] == 1 && edit[1] == 0, "cell edited callback");
            Pt.table_select_range(edit_table, 1, 3);
            check(Pt.table_get_selection_count(edit_table) == 3, "selection count");
            int[] selected = Pt.table_get_selection(edit_table);
            check(selected.length == 3 && selected[0] == 1 && selected[2] == 3,
                    "selection rows");
            check(Pt.table_is_row_selected(edit_table, 2), "row selected");
            Pt.table_select_toggle(edit_table, 4);
            check(Pt.table_get_selection_count(edit_table) == 4, "table toggle added");
            check(Pt.table_get_selected(edit_table) == 4, "table primary toggled");
            Pt.table_select_toggle(edit_table, 2);
            check(Pt.table_get_selection_count(edit_table) == 3, "table toggle removed");
            check(!Pt.table_is_row_selected(edit_table, 2), "row deselected");
            Pt.table_clear_selection(edit_table);
            check(Pt.table_get_selection_count(edit_table) == 0, "selection cleared");
            final int[] reorder = {-1, -1};
            Pt.table_set_on_columns_reordered(edit_table, (target, from, to, data) -> {
                reorder[0] = from;
                reorder[1] = to;
            }, arena);
            Pt.table_set_on_row_drag(edit_table, (target, row, data) -> { }, arena);
            Pt.table_set_columns_reorderable(edit_table, true);
            check(Pt.table_get_columns_reorderable(edit_table), "columns reorderable");
            check(Pt.table_move_column(edit_table, 0, 2), "move column");
            check(reorder[0] == 0 && reorder[1] == 2, "reorder callback");

            MemorySegment rename_tree = Pt.tree(column);
            MemorySegment rename_node = Pt.tree_add_node(rename_tree, MemorySegment.NULL, "before");
            Pt.tree_set_renamable(rename_tree, true);
            check(Pt.tree_get_renamable(rename_tree), "tree renamable");
            final String[] renamed = {""};
            Pt.tree_set_on_rename(rename_tree, (target, target_node, text, data) -> {
                renamed[0] = text;
            }, arena);
            check(Pt.tree_begin_rename(rename_tree, rename_node), "begin rename");
            check(Pt.tree_is_renaming(rename_tree), "tree renaming");
            check(Pt.tree_commit_rename(rename_tree), "commit rename");
            check(!Pt.tree_is_renaming(rename_tree), "rename finished");
            check(renamed[0].equals("before"), "rename callback");
            check(!Pt.tree_commit_rename(rename_tree), "commit without editor");
            check(Pt.tree_get_selection_count(rename_tree) == 1, "tree selection count");
            check(Pt.tree_is_selected(rename_tree, rename_node), "tree node selected");
            check(Pt.tree_get_selection(rename_tree).length == 1, "tree selection list");
            Pt.tree_select_toggle(rename_tree, rename_node);
            check(Pt.tree_get_selection_count(rename_tree) == 0, "tree toggle cleared");
            Pt.tree_select_range(rename_tree, 0, 0);
            check(Pt.tree_get_selection_count(rename_tree) == 1, "tree range selected");
            Pt.tree_clear_selection(rename_tree);
            check(Pt.tree_get_selection_count(rename_tree) == 0, "tree selection cleared");
            check(Pt.tree_get_selected(rename_tree).address() == 0, "tree primary cleared");

            MemorySegment entry = Pt.entry(column, "placeholder");
            Pt.entry_set_password(entry, true);
            Pt.entry_set_on_change(entry, (widget, text, data) -> { }, arena);
            Pt.widget_set_input_purpose(entry, Pt.input_purpose_email);
            check(Pt.widget_get_input_purpose(entry) == Pt.input_purpose_email,
                    "input purpose");

            MemorySegment toggle = Pt.toggle(column);
            Pt.toggle_set_checked(toggle, true);
            check(Pt.toggle_get_checked(toggle), "toggle checked");
            Pt.toggle_set_checked(toggle, false);
            check(!Pt.toggle_get_checked(toggle), "toggle unchecked");

            MemorySegment slider = Pt.slider(column);
            Pt.slider_set_value(slider, 0.375f);
            check(Math.abs(Pt.slider_get_value(slider) - 0.375f) < 0.0001f, "slider value");

            MemorySegment checkbox = Pt.checkbox(column, "Subscribe");
            Pt.widget_set_checked(checkbox, true);
            check(Pt.widget_get_checked(checkbox), "checkbox checked");
            Pt.widget_set_on_checked(checkbox, (widget, value, data) -> { }, arena);

            MemorySegment radio = Pt.radio(column, "Comfort");
            MemorySegment other = Pt.radio(column, "Compact");
            Pt.widget_set_checked(radio, true);
            check(Pt.widget_get_checked(radio), "radio checked");
            Pt.widget_set_checked(other, true);
            check(!Pt.widget_get_checked(radio), "radio exclusivity");

            MemorySegment progress = Pt.progress(column);
            Pt.progress_set_value(progress, 0.625f);
            check(Math.abs(Pt.progress_get_value(progress) - 0.625f) < 0.0001f,
                    "progress value");
            Pt.progress_set_indeterminate(progress, true);
            check(Pt.progress_get_indeterminate(progress), "progress indeterminate");
            check(Pt.spinner(column).address() != 0, "spinner");

            MemorySegment segmented = Pt.segmented(column,
                    new String[] {"Dense", "Cozy", "Compact"});
            Pt.segmented_set_selected(segmented, 1);
            check(Pt.segmented_get_selected(segmented) == 1, "segmented selected");
            Pt.segmented_set_on_change(segmented, (widget, index, data) -> { }, arena);

            Pt.widget_set_text(entry, "alpha beta gamma");
            Pt.entry_select_all(entry);
            int[] selection = Pt.entry_get_selection(entry);
            check(selection != null && selection[0] == 0 && selection[1] == 16,
                    "entry select all");
            Pt.entry_set_selection(entry, 10, 6);
            selection = Pt.entry_get_selection(entry);
            check(selection != null && selection[0] == 6 && selection[1] == 10,
                    "entry selection order");
            check(!Pt.entry_undo(entry), "entry undo empty");
            check(!Pt.entry_redo(entry), "entry redo empty");

            check(Pt.entry_get_drag_enabled(entry), "entry drag enabled by default");
            Pt.entry_set_drag_enabled(entry, false);
            check(!Pt.entry_get_drag_enabled(entry), "entry drag disabled");
            Pt.entry_set_drag_enabled(entry, true);
            check(!Pt.widget_get_drop_hover(entry), "drop hover idle");
            Pt.widget_set_drop_target(entry, (widget, mime, data) -> true,
                    (widget, x, y, data) -> { }, (widget, data) -> { },
                    (widget, mime, text, x, y, data) -> { }, arena);
            Pt.widget_clear_drop_target(entry);

            Pt.window_set_gesture_handler(window, (w, event, data) -> { }, arena);
            Pt.window_set_long_press_delay(window, 320);
            check(Pt.window_get_momentum_scrolling(window), "momentum default");
            Pt.window_set_momentum_scrolling(window, false);
            check(!Pt.window_get_momentum_scrolling(window), "momentum off");
            Pt.window_set_momentum_scrolling(window, true);
            check(!Pt.window_begin_drag(window, new Pt.DragData[] {
                    new Pt.DragData("text/plain;charset=utf-8", "dragged", 0L)}),
                    "drag source headless");
            check(Pt.window_get_popup_policy(window) == Pt.popup_policy_auto,
                    "popup policy default");
            Pt.window_set_popup_policy(window, Pt.popup_policy_window);
            check(Pt.window_get_popup_policy(window) == Pt.popup_policy_window,
                    "popup policy set");
            Pt.window_set_popup_policy(window, Pt.popup_policy_auto);
            check(!Pt.window_get_drag_active(window), "drag idle");
            check(Pt.window_get_drag_timeout(window) == 0, "drag timeout default");
            Pt.window_set_drag_timeout(window, 1500);
            check(Pt.window_get_drag_timeout(window) == 1500, "drag timeout set");
            Pt.window_cancel_drag(window);
            check(!Pt.window_get_drag_active(window), "drag cancel");
            Pt.window_set_drag_timeout(window, 0);

            Pt.widget_set_tooltip(button, "Press me");
            Pt.widget_set_tooltip_delay(button, 0);
            check(Pt.widget_get_a11y_role(label) == Pt.a11y_role_label, "a11y label role");
            check(Pt.widget_get_a11y_name(label).equals("changed"), "a11y label name");
            check((Pt.widget_get_a11y_state(label) & Pt.a11y_state_visible) != 0,
                    "a11y visible state");
            Pt.widget_set_a11y_name(label, "Greeting");
            Pt.widget_set_a11y_description(label, "Title text");
            check(Pt.widget_get_a11y_name(label).equals("Greeting"), "a11y name override");
            check(Pt.widget_get_a11y_description(label).equals("Title text"),
                    "a11y description override");
            Pt.A11yText label_text = Pt.widget_get_a11y_text(label);
            check(label_text != null && label_text.text().equals("changed")
                    && label_text.caret() == 0, "a11y label text");
            check(Pt.widget_get_a11y_text(column) == null, "a11y text only for text widgets");
            Pt.entry_set_password(entry, false);
            Pt.A11yText entry_text = Pt.widget_get_a11y_text(entry);
            check(entry_text != null && entry_text.text().equals("alpha beta gamma"),
                    "a11y entry text");
            Pt.entry_set_password(entry, true);
            check(Pt.widget_get_a11y_text(entry) == null, "a11y password hidden");
            check(Pt.widget_get_a11y_value(slider) != null
                    && Math.abs(Pt.widget_get_a11y_value(slider) - 0.375f) < 0.0001f,
                    "a11y slider value");
            Pt.widget_set_checked(checkbox, true);
            check(Pt.widget_get_a11y_value(checkbox) != null
                    && Pt.widget_get_a11y_value(checkbox) > 0.5f, "a11y checkbox value");
            Pt.widget_set_checked(checkbox, false);

            final int[] menu_clicks = {0};
            MemorySegment menu = Pt.menu_create(window);
            Pt.menu_add_item(menu, "Open", "Ctrl+O", (widget, data) -> menu_clicks[0]++, arena);
            Pt.menu_add_separator(menu);
            Pt.menu_add_item(menu, "Quit", null, null, arena);
            Pt.menu_show_at(menu, 20.0f, 20.0f);
            check(Pt.menu_is_open(menu), "menu open");
            Pt.menu_dismiss(menu);
            check(!Pt.menu_is_open(menu), "menu dismissed");
            check(menu_clicks[0] == 0, "menu callback deferred");
            Pt.menu_destroy(menu);

            final int[] closed = {0};
            final int[] action = {0};
            MemorySegment dialog = Pt.dialog_create(window, "Rename", "Choose a label");
            Pt.dialog_add_action(dialog, "Cancel", false, (widget, data) -> action[0]++, arena);
            Pt.dialog_add_action(dialog, "Save", true, null, arena);
            Pt.dialog_set_on_close(dialog, (handle, data) -> closed[0]++, arena);
            Pt.dialog_show(dialog);
            check(Pt.dialog_is_open(dialog), "dialog open");
            Pt.dialog_set_title(dialog, "Rename file");
            Pt.dialog_set_message(dialog, "Pick a name");
            Pt.dialog_close(dialog);
            check(!Pt.dialog_is_open(dialog), "dialog closed");
            check(closed[0] == 1, "dialog close handler");
            check(action[0] == 0, "dialog action untouched");
            Pt.dialog_destroy(dialog);

            MemorySegment toast = Pt.window_show_toast(window, "Saved", Pt.toast_success, 0);
            check(toast.address() != 0 && Pt.toast_is_visible(toast), "toast visible");
            Pt.toast_dismiss(toast);
            check(!Pt.toast_is_visible(toast), "toast dismissed");

            final int[] a11y_events = {0};
            Pt.app_set_a11y_handler(app, (source, widget, event, data) -> {
                if (event == Pt.a11y_event_text_changed) {
                    a11y_events[0]++;
                }
            }, arena);
            Pt.widget_set_text(entry, "logged");
            check(a11y_events[0] == 1, "a11y text event");
            Pt.app_set_a11y_handler(app, null, arena);

            check(Pt.window_clipboard_set(window, "copied text"), "clipboard set");
            check(Pt.window_clipboard_get(window).equals("copied text"), "clipboard get");
            Pt.widget_set_checked(checkbox, false);
            check(!Pt.widget_get_checked(checkbox), "checkbox cleared");

            MemorySegment card = Pt.card(column);
            Pt.widget_set_radius(card, 12.0f);
            Pt.widget_set_background(card, Pt.rgb(0x161D28));
            MemorySegment row = Pt.row(card);
            Pt.box_set_align(row, Pt.align_center);
            Pt.box_set_justify(row, Pt.justify_center);
            Pt.spacer(row);
            Pt.divider(row);
            MemorySegment scroll = Pt.scroll(column);
            Pt.widget_set_height(scroll, 60.0f);
            Pt.scroll_set_offset(scroll, 32.0f);
            check(Pt.scroll_get_offset(scroll) >= 0.0f, "scroll offset");
            check(Pt.scroll_get_max_offset(scroll) >= 0.0f, "scroll max offset");

            MemorySegment notes = Pt.text_area(column, "Notes");
            check(notes.address() != 0, "text area create");
            Pt.widget_set_text(notes, "alpha\nbeta");
            Pt.text_area_set_placeholder(notes, "Write");
            Pt.text_area_set_on_change(notes, (widget, text, data) -> { }, arena);
            Pt.text_area_select_all(notes);
            int[] area_selection = Pt.text_area_get_selection(notes);
            check(area_selection != null && area_selection[0] == 0 && area_selection[1] == 10,
                    "text area select all");
            Pt.text_area_set_selection(notes, 6, 3);
            area_selection = Pt.text_area_get_selection(notes);
            check(area_selection != null && area_selection[0] == 3 && area_selection[1] == 6,
                    "text area selection order");
            check(!Pt.text_area_undo(notes), "text area undo empty");
            check(!Pt.text_area_redo(notes), "text area redo empty");
            Pt.text_area_set_grow_to_content(notes, true);
            check(Pt.text_area_get_grow_to_content(notes), "text area grow");
            Pt.text_area_set_grow_to_content(notes, false);
            Pt.text_area_scroll_to_caret(notes);
            check(Pt.widget_get_a11y_role(notes) == Pt.a11y_role_text_frame,
                    "text area role");

            MemorySegment wrap = Pt.wrap(column);
            Pt.wrap_set_gap(wrap, 8.0f, 6.0f);
            MemorySegment tile = Pt.card(wrap);
            Pt.widget_set_size(tile, 80.0f, 40.0f);
            Pt.widget_set_align_self(tile, Pt.align_start);

            MemorySegment grid = Pt.grid(column, 3);
            Pt.grid_set_gap(grid, 10.0f, 12.0f);
            check(Pt.grid_get_columns(grid) == 3, "grid columns");
            Pt.grid_set_columns(grid, 2);
            check(Pt.grid_get_columns(grid) == 2, "grid columns updated");
            MemorySegment wide = Pt.card(grid);
            Pt.widget_set_grid_span(wide, 2);
            check(Pt.widget_get_grid_span(wide) == 2, "grid span");

            MemorySegment list = Pt.list_view(column);
            Pt.list_view_set_row_height(list, 36.0f);
            check(Math.abs(Pt.list_view_get_row_height(list) - 36.0f) < 0.0001f,
                    "list row height");
            Pt.list_view_set_row_count(list, 50);
            check(Pt.list_view_get_row_count(list) == 50, "list row count");
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list no selection");
            Pt.list_view_set_on_bind_row(list, (handle, row_widget, index, data) -> {
            }, arena);
            Pt.list_view_set_on_select(list, (widget, index, data) -> { }, arena);
            Pt.list_view_set_on_activate(list, (widget, index, data) -> { }, arena);
            Pt.list_view_set_selected(list, 7);
            check(Pt.list_view_get_selected(list) == 7, "list selected");
            Pt.list_view_scroll_to_row(list, 49);
            Pt.list_view_refresh(list);
            Pt.list_view_set_row_key_handler(list,
                    (view, row_index, data) -> 1000L + row_index * 10L, arena);
            check(Pt.list_view_get_selected_key(list) == 1070L, "list selected key");
            Pt.list_view_set_row_count(list, 50);
            check(Pt.list_view_get_selected(list) == 7, "list keyed selection preserved");
            Pt.list_view_set_selected(list, 3);
            check(Pt.list_view_get_selected_key(list) == 1030L, "list key follows selection");
            check(!Pt.list_view_set_selected_key(list, 999999L), "list unknown key");
            check(Pt.list_view_get_selected(list) == 3, "list selection unchanged");
            check(Pt.list_view_set_selected_key(list, 1040L), "list select by key");
            check(Pt.list_view_get_selected(list) == 4, "list key row");
            check(Pt.list_view_get_selected_key(list) == 1040L, "list key value");
            Pt.list_view_refresh_rows(list, 0, 4);
            check(Pt.list_view_get_row_count(list) == 50, "list refresh rows");
            check(Pt.list_view_set_selected_key(list, Pt.row_key_none), "list clear key");
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list cleared selection");
            check(Pt.list_view_get_selected_key(list) == Pt.row_key_none,
                    "list cleared key");
            check(Pt.list_view_set_selected_key(list, 1030L), "list reselect key");
            Pt.list_view_set_row_count(list, 2);
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list keyed selection cleared");
            Pt.list_view_set_selected(list, 1);
            check(Pt.list_view_get_selected_key(list) == 1010L, "list in-range key");
            Pt.list_view_set_row_count(list, 2);
            check(Pt.list_view_get_selected(list) == 1, "list in-range key preserved");
            Pt.list_view_set_row_text_handler(list, (view, row_index, data) -> "item " + row_index,
                    arena);
            Pt.list_view_set_selected(list, Pt.list_index_none);
            Pt.list_view_set_row_count(list, 2);
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list selection cleared");
            check(Pt.widget_get_a11y_role(list) == Pt.a11y_role_list, "list role");
            Pt.list_view_set_row_count(list, 8);
            check(Pt.list_view_get_selection_count(list) == 0, "list no multi selection");
            check(Pt.list_view_get_selection(list).length == 0, "list empty selection");
            Pt.list_view_select_range(list, 2, 4);
            check(Pt.list_view_get_selection_count(list) == 3, "list range count");
            int[] list_rows = Pt.list_view_get_selection(list);
            check(list_rows.length == 3 && list_rows[0] == 2 && list_rows[2] == 4,
                    "list range rows");
            check(Pt.list_view_is_row_selected(list, 3), "list row selected");
            check(!Pt.list_view_is_row_selected(list, 5), "list row unselected");
            check(Pt.list_view_get_selected(list) == 2, "list primary row");
            Pt.list_view_select_toggle(list, 6);
            check(Pt.list_view_get_selection(list).length == 4, "list toggled on");
            Pt.list_view_select_toggle(list, 3);
            check(Pt.list_view_get_selection(list).length == 3, "list toggled off");
            Pt.list_view_clear_selection(list);
            check(Pt.list_view_get_selection_count(list) == 0, "list selection cleared");
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list primary cleared");

            MemorySegment icon = Pt.icon(column, Pt.icon_search);
            check(Pt.widget_get_icon(icon) == Pt.icon_search, "icon glyph");
            check(Math.abs(Pt.icon_get_size(icon) - 20.0f) < 0.0001f, "icon default size");
            Pt.widget_set_icon(icon, Pt.icon_bell);
            Pt.icon_set_size(icon, 28.0f);
            check(Pt.widget_get_icon(icon) == Pt.icon_bell, "icon set");
            check(Math.abs(Pt.icon_get_size(icon) - 28.0f) < 0.0001f, "icon size set");
            check(Pt.widget_get_a11y_role(icon) == Pt.a11y_role_icon, "icon role");
            MemorySegment icon_button = Pt.icon_button(column, Pt.icon_plus, "Add item");
            check(Pt.widget_get_a11y_name(icon_button).equals("Add item"),
                    "icon button name");
            check(Pt.widget_get_a11y_role(icon_button) == Pt.a11y_role_push_button,
                    "icon button role");

            MemorySegment table = Pt.table(column);
            Pt.widget_set_height(table, 180.0f);
            check(Pt.widget_get_a11y_role(table) == Pt.a11y_role_table, "table role");
            Pt.table_set_column_count(table, 3);
            check(Pt.table_get_column_count(table) == 3, "table column count");
            Pt.table_set_column_title(table, 0, "Name");
            Pt.table_set_column_title(table, 1, "Kind");
            Pt.table_set_column_width(table, 0, 140.0f);
            Pt.table_set_column_align(table, 1, Pt.text_align_center);
            Pt.table_set_column_resizable(table, 2, false);
            check(Math.abs(Pt.table_get_column_width(table, 0) - 140.0f) < 0.0001f,
                    "table column width");
            Pt.table_set_header_height(table, 34.0f);
            Pt.table_set_row_count(table, 40);
            Pt.table_set_row_height(table, 32.0f);
            check(Pt.table_get_row_count(table) == 40, "table row count");
            check(Math.abs(Pt.table_get_row_height(table) - 32.0f) < 0.0001f,
                    "table row height");
            Pt.table_set_on_bind_cell(table, (t, cell, row_index, column_index, data) -> {
            }, arena);
            Pt.table_set_on_select(table, (widget, index, data) -> { }, arena);
            Pt.table_set_on_activate(table, (widget, index, data) -> { }, arena);
            Pt.table_set_on_sort(table, (widget, column_index, direction, data) -> {
            }, arena);
            check(Pt.table_get_selected(table) == Pt.list_index_none,
                    "table no selection");
            Pt.table_set_selected(table, 3);
            check(Pt.table_get_selected(table) == 3, "table selection");
            Pt.table_set_sort(table, 1, Pt.sort_ascending);
            int[] table_sort = Pt.table_get_sort(table);
            check(table_sort[0] == Pt.sort_ascending && table_sort[1] == 1, "table sort");
            check(!Pt.table_get_column_hidden(table, 1), "column visible");
            Pt.table_set_column_hidden(table, 1, true);
            check(Pt.table_get_column_hidden(table, 1), "column hidden");
            Pt.table_set_column_hidden(table, 1, false);
            check(!Pt.table_get_column_hidden(table, 1), "column restored");
            check(!Pt.table_get_column_hidden(table, 7), "column hidden out of range");
            Pt.table_set_on_sort_columns(table, (handle, count, columns, directions, data) -> {
            }, arena);
            Pt.table_set_sort_columns(table, new int[] {2, 0},
                    new int[] {Pt.sort_descending, Pt.sort_ascending});
            int[] sort_keys = Pt.table_get_sort_columns(table);
            check(sort_keys.length == 4 && sort_keys[0] == 2
                    && sort_keys[1] == Pt.sort_descending && sort_keys[2] == 0
                    && sort_keys[3] == Pt.sort_ascending, "table sort columns");
            table_sort = Pt.table_get_sort(table);
            check(table_sort[0] == Pt.sort_descending && table_sort[1] == 2,
                    "table primary sort");
            Pt.table_set_sort_columns(table, new int[] {1, 1, 7},
                    new int[] {Pt.sort_ascending, Pt.sort_descending, Pt.sort_ascending});
            sort_keys = Pt.table_get_sort_columns(table);
            check(sort_keys.length == 2 && sort_keys[0] == 1
                    && sort_keys[1] == Pt.sort_ascending, "table sort filters");
            Pt.table_set_row_key_handler(table,
                    (view, row_index, data) -> 1000L + row_index * 10L, arena);
            check(Pt.table_get_selected(table) == 3, "table selection before keys");
            check(Pt.table_get_selected_key(table) == 1030L, "table selected key");
            Pt.table_set_row_count(table, 40);
            check(Pt.table_get_selected(table) == 3, "table keyed selection preserved");
            Pt.table_set_selected(table, 0);
            check(Pt.table_get_selected_key(table) == 1000L, "table key follows row");
            check(!Pt.table_set_selected_key(table, 999999L), "table unknown key");
            check(Pt.table_get_selected(table) == 0, "table selection unchanged");
            check(Pt.table_set_selected_key(table, 1050L), "table select by key");
            check(Pt.table_get_selected(table) == 5, "table key row");
            check(Pt.table_get_selected_key(table) == 1050L, "table key value");
            check(Pt.table_set_selected_key(table, Pt.row_key_none), "table clear key");
            check(Pt.table_get_selected(table) == Pt.list_index_none,
                    "table cleared selection");
            check(Pt.table_set_selected_key(table, 1010L), "table reselect key");
            Pt.table_refresh_rows(table, 0, 3);
            check(Pt.table_get_row_count(table) == 40, "table refresh rows");
            Pt.table_set_row_text_handler(table, (view, row_index, data) -> "row " + row_index,
                    arena);
            Pt.table_set_selected(table, 3);
            Pt.table_scroll_to_row(table, 39);
            Pt.table_scroll_to_column(table, 2);
            Pt.table_refresh(table);
            Pt.table_set_row_count(table, 2);
            check(Pt.table_get_selected(table) == Pt.list_index_none,
                    "table selection clamped");

            MemorySegment tree = Pt.tree(column);
            check(Pt.widget_get_a11y_role(tree) == Pt.a11y_role_tree, "tree role");
            MemorySegment alpha = Pt.tree_add_node(tree, null, "alpha");
            MemorySegment beta = Pt.tree_add_node(tree, null, "beta");
            MemorySegment leaf = Pt.tree_add_node(tree, alpha, "leaf");
            check(Pt.tree_get_root_count(tree) == 2, "tree roots");
            check(Pt.tree_node_get_text(alpha).equals("alpha"), "tree node text");
            check(Pt.tree_node_has_children(alpha), "tree node children");
            check(Pt.tree_node_get_child_count(alpha) == 1, "tree child count");
            check(Pt.tree_node_get_child(alpha, 0).address() == leaf.address(),
                    "tree child handle");
            check(Pt.tree_node_parent(leaf).address() == alpha.address(), "tree parent");
            check(Pt.tree_node_get_depth(leaf) == 1, "tree depth");
            MemorySegment payload = arena.allocate(4);
            Pt.tree_node_set_data(leaf, payload);
            check(Pt.tree_node_get_data(leaf).address() == payload.address(),
                    "tree node data");
            check(Pt.tree_get_visible_count(tree) == 2, "tree visible collapsed");
            Pt.tree_node_set_expanded(alpha, true);
            check(Pt.tree_node_get_expanded(alpha), "tree expanded");
            check(Pt.tree_get_visible_count(tree) == 3, "tree visible expanded");
            check(Pt.tree_get_visible_node(tree, 1).address() == leaf.address(),
                    "tree visible node");
            Pt.tree_set_on_select(tree, (handle, node, data) -> { }, arena);
            Pt.tree_set_on_activate(tree, (handle, node, data) -> { }, arena);
            Pt.tree_set_selected(tree, leaf);
            check(Pt.tree_get_selected(tree).address() == leaf.address(), "tree selected");
            Pt.tree_set_row_height(tree, 30.0f);
            Pt.tree_set_indent(tree, 16.0f);
            Pt.tree_scroll_to_node(tree, beta);
            Pt.tree_refresh(tree);
            Pt.tree_node_set_text(beta, "beta renamed");
            check(Pt.tree_node_get_text(beta).equals("beta renamed"), "tree rename");
            Pt.tree_clear(tree);
            check(Pt.tree_get_root_count(tree) == 0, "tree cleared");

            MemorySegment tabs = Pt.tabs(column);
            MemorySegment first_page = Pt.tabs_add_page(tabs, "General");
            MemorySegment second_page = Pt.tabs_add_page(tabs, "Advanced");
            check(Pt.tabs_get_page_count(tabs) == 2, "tabs page count");
            check(Pt.tabs_get_page_title(tabs, 0).equals("General"), "tabs title");
            Pt.tabs_set_page_title(tabs, 1, "Options");
            check(Pt.tabs_get_page_title(tabs, 1).equals("Options"), "tabs rename");
            check(Pt.tabs_get_active(tabs) == 0, "tabs active default");
            Pt.tabs_set_active(tabs, 1);
            check(Pt.tabs_get_active(tabs) == 1, "tabs active set");
            Pt.tabs_set_on_change(tabs, (widget, index, data) -> { }, arena);
            Pt.tabs_set_strip_height(tabs, 40.0f);
            check(Pt.tabs_get_page(tabs, 0).address() == first_page.address(),
                    "tabs page handle");
            check(Pt.tabs_get_page(tabs, 2).address() == 0, "tabs out of range");
            Pt.tabs_remove_page(tabs, 0);
            check(Pt.tabs_get_page_count(tabs) == 1, "tabs removed");
            check(Pt.tabs_get_page_title(tabs, 0).equals("Options"), "tabs shift");
            check(Pt.tabs_get_active(tabs) == 0, "tabs active clamped");
            check(second_page.address() != first_page.address(), "tabs pages distinct");

            MemorySegment sidebar = Pt.sidebar(column);
            MemorySegment home = Pt.sidebar_add_item(sidebar, Pt.icon_home, "Home");
            Pt.sidebar_add_item(sidebar, Pt.icon_folder, "Files");
            check(Pt.sidebar_get_item_count(sidebar) == 2, "sidebar count");
            check(Pt.sidebar_get_active(sidebar) == Pt.list_index_none,
                    "sidebar no active");
            Pt.sidebar_set_active(sidebar, 1);
            check(Pt.sidebar_get_active(sidebar) == 1, "sidebar active");
            Pt.sidebar_set_compact(sidebar, true);
            check(Pt.sidebar_get_compact(sidebar), "sidebar compact");
            Pt.sidebar_set_compact(sidebar, false);
            Pt.sidebar_set_header(sidebar, "Workspace");
            check(Pt.sidebar_get_header(sidebar).equals("Workspace"), "sidebar header");
            Pt.sidebar_set_on_select(sidebar, (widget, index, data) -> { }, arena);
            check(Pt.widget_get_a11y_name(home).equals("Home"), "sidebar item name");
            check(Pt.sidebar_get_item(sidebar, 1).address() != 0, "sidebar item handle");

            MemorySegment combo = Pt.combobox(column);
            check(Pt.combobox_get_item_count(combo) == 0, "combo empty");
            check(Pt.combobox_get_selected(combo) == Pt.list_index_none, "combo no choice");
            check(Pt.combobox_get_text(combo).isEmpty(), "combo no text");
            check(Pt.combobox_get_mode(combo) == Pt.combobox_select_only, "combo mode default");
            check(!Pt.combobox_is_open(combo), "combo closed");
            check(Math.abs(Pt.combobox_get_popup_max_height(combo) - 260.0f) < 0.0001f,
                    "combo popup default height");
            Pt.combobox_add_item(combo, "Red");
            Pt.combobox_add_item(combo, "Green");
            Pt.combobox_insert_item(combo, 1, "Yellow");
            check(Pt.combobox_get_item_count(combo) == 3, "combo item count");
            check(Pt.combobox_get_item_text(combo, 1).equals("Yellow"), "combo item text");
            Pt.combobox_set_item_text(combo, 2, "Blue");
            check(Pt.combobox_get_item_text(combo, 2).equals("Blue"), "combo item rename");
            Pt.combobox_set_placeholder(combo, "Colour");
            check(Pt.combobox_get_placeholder(combo).equals("Colour"), "combo placeholder");
            try (Arena combo_box_arena = Arena.ofConfined()) {
                MemorySegment box = combo_box_arena.allocate(ValueLayout.JAVA_INT, 0x1234);
                Pt.combobox_set_item_data(combo, 2, box);
                check(Pt.combobox_get_item_data(combo, 2).address() == box.address(),
                        "combo item data");
                check(Pt.combobox_get_item_data(combo, 0).address() == 0,
                        "combo empty item data");
            }
            check(!Pt.combobox_get_item_disabled(combo, 1), "combo item enabled");
            Pt.combobox_set_item_disabled(combo, 1, true);
            check(Pt.combobox_get_item_disabled(combo, 1), "combo item disabled");
            Pt.combobox_set_item_disabled(combo, 1, false);
            final int[] combo_choice = {Pt.list_index_none};
            final int[] combo_typed = {0};
            Pt.combobox_set_on_change(combo, (widget, index, data) -> combo_choice[0] = index,
                    arena);
            Pt.combobox_set_on_text_change(combo, (widget, text, data) -> {
                if (text.address() != 0) {
                    combo_typed[0] += 1;
                }
            }, arena);
            Pt.combobox_set_selected(combo, 2);
            check(Pt.combobox_get_selected(combo) == 2, "combo selection");
            check(Pt.combobox_get_text(combo).equals("Blue"), "combo text follows selection");
            check(combo_choice[0] == Pt.list_index_none, "combo silent set_selected");
            Pt.combobox_set_mode(combo, Pt.combobox_filterable);
            check(Pt.combobox_get_mode(combo) == Pt.combobox_filterable, "combo filterable");
            Pt.combobox_set_text(combo, "gr");
            check(Pt.combobox_get_text(combo).equals("gr"), "combo typed text");
            check(combo_typed[0] == 0, "combo silent set_text");
            Pt.combobox_open(combo);
            check(Pt.combobox_is_open(combo), "combo popup open");
            Pt.combobox_set_popup_max_height(combo, 120.0f);
            check(Math.abs(Pt.combobox_get_popup_max_height(combo) - 120.0f) < 0.0001f,
                    "combo popup height");
            Pt.combobox_close(combo);
            check(!Pt.combobox_is_open(combo), "combo popup closed");
            check(Pt.widget_get_a11y_role(combo) == Pt.a11y_role_combo_box, "combo role");
            Pt.combobox_remove_item(combo, 0);
            check(Pt.combobox_get_item_count(combo) == 2, "combo item removed");
            check(Pt.combobox_get_item_text(combo, 0).equals("Yellow"), "combo removal shifts");
            Pt.combobox_clear(combo);
            check(Pt.combobox_get_item_count(combo) == 0, "combo cleared");
            check(Pt.combobox_get_selected(combo) == Pt.list_index_none,
                    "combo selection cleared");

            String uri_text = "file:///tmp/a.txt\r\n# comment\r\nfile:///tmp/b%20c.txt\r\n";
            check(Pt.uri_list_count(uri_text) == 2, "uri list count");
            check(Pt.uri_list_get(uri_text, 0).equals("file:///tmp/a.txt"), "uri list get");
            check(Pt.uri_list_get(uri_text, 2) == null, "uri list out of range");
            check(Pt.uri_list(uri_text).length == 2, "uri list parse");
            check(!Pt.window_begin_drag(window, new Pt.DragData[] {
                    new Pt.DragData("text/uri-list", "file:///tmp/a.txt\r\n", 0L)}),
                    "uri drag headless");
            Pt.window_set_drag_image(window, new int[] {0xFF0000FF, 0x00FF00FF}, 1, 2);
            Pt.window_clear_drag_image(window);
            boolean drag_image_rejected = false;
            try {
                Pt.window_set_drag_image(window, new int[] {0xFF0000FF}, 2, 2);
            } catch (IllegalArgumentException expected) {
                drag_image_rejected = true;
            }
            check(drag_image_rejected, "drag image size validation");
            Pt.window_set_stylus_handler(window, (handle, event, data) -> { }, arena);
            check(Pt.stylus_phase_down == 2 && Pt.stylus_tool_eraser == 1,
                    "stylus constants");
            check(Pt.a11y_state_expandable == 1 << 10, "a11y expandable state");

            check(Pt.drag_action_copy == 1 && Pt.drag_action_move == 2 && Pt.drag_action_ask == 4
                    && (Pt.drag_action_copy | Pt.drag_action_move) == 3, "drag action flags");
            check(Pt.drop_stream_data == 0 && Pt.drop_stream_done == 1
                    && Pt.drop_stream_cancelled == 2 && Pt.drop_stream_error == 3,
                    "drop stream states");
            check(!Pt.window_begin_drag(window,
                    new Pt.DragData[] {new Pt.DragData("text/plain", "structured", 0L)}),
                    "structured drag headless");
            check(!Pt.window_begin_drag(window, new Pt.DragData[0]), "structured drag empty");
            check(!Pt.window_begin_drag(window, new Pt.DragData[] {
                    new Pt.DragData("text/plain", "structured", 10L),
                    new Pt.DragData("text/uri-list", "file:///tmp/a.txt", 0L)}),
                    "structured drag multi type headless");
            check(Pt.window_get_drag_result(window) == Pt.drag_action_none,
                    "drag result idle");
            final int[] finished_action = {Pt.drag_action_none};
            Pt.window_set_drag_finished_handler(window,
                    (handle, dropped, chosen, data) -> finished_action[0] = chosen, arena);
            Pt.window_set_drag_finished_handler(window, null, arena);
            check(finished_action[0] == Pt.drag_action_none, "drag finished untouched");

            check(Pt.drop_offer_mime_count(MemorySegment.NULL) == 0, "offer count null");
            check(Pt.drop_offer_mime(MemorySegment.NULL, 0) == null, "offer mime null");
            check(!Pt.drop_offer_has_mime(MemorySegment.NULL, "text/plain"),
                    "offer has mime null");
            check(!Pt.drop_offer_has_mime(MemorySegment.NULL, null),
                    "offer has mime null text");
            check(Pt.drop_offer_selected_mime(MemorySegment.NULL) == null,
                    "offer selected mime null");
            check(Pt.drop_offer_actions(MemorySegment.NULL) == 0, "offer actions null");
            check(Pt.drop_offer_read(MemorySegment.NULL, "text/plain", 0L,
                    (handle, stream, state, chunk, mime, data) -> { }, arena).address() == 0,
                    "offer read null");
            check(Pt.drop_stream_mime(MemorySegment.NULL) == null, "stream mime null");
            check(Pt.drop_stream_size(MemorySegment.NULL) == 0L, "stream size null");
            check(!Pt.drop_stream_active(MemorySegment.NULL), "stream active null");
            Pt.drop_stream_cancel(MemorySegment.NULL);
            check(!Pt.window_get_drag_active(window), "drag still idle after null streams");

            boolean offer_target_accepted = true;
            try {
                Pt.widget_set_drop_target(button, null, (widget, x, y, data) -> { },
                        (widget, data) -> { }, null,
                        (widget, offer, data) -> Pt.drop_offer_mime(offer, 0),
                        (widget, offer, mime, x, y, data) -> { }, arena);
                Pt.widget_clear_drop_target(button);
            } catch (RuntimeException | LinkageError error) {
                offer_target_accepted = false;
            }
            check(offer_target_accepted, "structured drop target registers and clears");

            try (Pt.Transition transition = new Pt.Transition(0.0f)) {
                transition.to(10.0f, 100, Pt.easing_linear);
                check(transition.active(), "transition active");
                transition.advance(0.05f);
                check(Math.abs(transition.value() - 5.0f) < 0.01f, "transition midpoint");
                transition.to(20.0f, 100, Pt.easing_linear);
                transition.advance(0.05f);
                check(transition.value() > 5.0f, "transition retarget");
                transition.set(30.0f);
                check(!transition.active()
                        && Math.abs(transition.value() - 30.0f) < 0.0001f,
                        "transition set");
            }

            final boolean[] fired = {false};
            Pt.app_add_timeout(app, 5, (target, data) -> fired[0] = true, arena);
            final boolean[] cancelled = {false};
            long cancel_id = Pt.app_add_timeout(app, 40,
                    (target, data) -> cancelled[0] = true, arena);
            Pt.app_cancel_timeout(app, cancel_id);

            Pt.window_show(window);
            check(Pt.app_run_for(app, 80) == 0, "run_for");
            check(fired[0], "timer fired");
            check(!cancelled[0], "timer cancelled");
            check(Pt.window_is_open(window), "window open");

            final boolean[] allowed = {false};
            Pt.window_set_close_handler(window, (target, data) -> allowed[0], arena);
            Pt.window_close(window);
            check(Pt.window_is_open(window), "close veto");

            allowed[0] = true;
            Pt.window_close(window);
            Pt.app_destroy(app);

            if (failures != 0) {
                System.err.println("java binding: " + checks + " checks, " + failures
                        + " failure(s)");
                System.exit(1);
            }
            System.out.println("java binding: " + checks + " checks, 0 failures");
        }
    }
}

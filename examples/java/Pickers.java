/*
 * Pt pickers: a searchable select, a date picker, a number picker and a colour
 * picker in one window, with a change log.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (src/pt/Pt.java, include/pt/picker.h):
 *
 *  - Pickers are editor fields with a popup surface except the number picker.
 *    The committed value is what the on_change handler reports;
 *    programmatic setters are silent.
 *  - PtDate is a record (year, month, day); year zero is the cleared value.
 *    date_picker_set_date takes the record and returns false for invalid dates.
 *  - The select reports list_index_none for "no selection"; the filter query
 *    is live and independent of the committed index.
 *  - The colour picker parses #RRGGBB/#RRGGBBAA and keeps its presets bounded
 *    by Pt.color_max_presets.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import pt.Pt;
import pt.Pt.PtDate;

public final class Pickers {
    private Pickers() {}

    private static final String[] CITIES = {
        "Amsterdam", "Barcelona", "Berlin", "Bordeaux", "Bristol", "Brussels",
        "Copenhagen", "Dublin", "Edinburgh", "Lisbon", "London", "Madrid",
        "Milan", "Munich", "Oslo", "Paris", "Porto", "Prague", "Rome",
        "Stockholm", "Vienna", "Warsaw", "Zurich",
    };

    private static boolean headless() {
        String value = System.getenv("PT_HEADLESS");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    private static void log(MemorySegment status, int[] changes, String line) {
        changes[0]++;
        String text = "#" + changes[0] + " · " + line;
        Pt.widget_set_text(status, text);
        System.out.println("pickers: " + text);
    }

    private static MemorySegment fieldBlock(MemorySegment parent, String title) {
        MemorySegment block = Pt.column(parent);
        Pt.widget_set_gap(block, 6.0f);
        MemorySegment label = Pt.label(block, title);
        Pt.widget_set_font_size(label, 12.0f);
        Pt.widget_set_font_weight(label, Pt.font_weight_semibold);
        return block;
    }

    private static void note(MemorySegment block, String text) {
        MemorySegment note = Pt.label(block, text);
        Pt.widget_set_font_size(note, 11.0f);
        Pt.widget_set_wrap(note, true);
        Pt.widget_set_foreground(note, Pt.rgb(0x8B98AA));
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.examples.pickers.java");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x4C8DFF));

            MemorySegment window = Pt.window_create(app, "Pt Pickers", 760, 760);
            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment eyebrow = Pt.label(root, "PICKERS  ·  1.2  ·  JAVA");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x4C8DFF));

            MemorySegment heading = Pt.label(root, "Four fields, one focus chain");
            Pt.widget_set_font_size(heading, 28.0f);
            Pt.widget_set_font_weight(heading, Pt.font_weight_semibold);

            MemorySegment intro = Pt.label(root,
                    "Every control is reachable with Tab. F4 or Alt+Down opens a popup; "
                    + "Down/Up move the highlight, Enter commits and Escape restores "
                    + "the committed value.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            MemorySegment card = Pt.card(root);
            Pt.widget_set_grow(card, 1.0f);
            Pt.widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
            Pt.widget_set_gap(card, 14.0f);

            MemorySegment status = Pt.label(root, "");
            Pt.widget_set_font_size(status, 12.0f);
            Pt.widget_set_wrap(status, true);
            Pt.widget_set_foreground(status, Pt.rgb(0x9ECE6A));
            Pt.widget_set_a11y_name(status, "Status");
            int[] changes = {0};

            MemorySegment selectBlock = fieldBlock(card, "Searchable select");
            MemorySegment select = Pt.select_create(selectBlock);
            Pt.widget_set_grow(select, 1.0f);
            Pt.widget_set_min_width(select, 260.0f);
            Pt.select_set_placeholder(select, "Type to search a city");
            Pt.select_set_max_popup_rows(select, 6);
            Pt.widget_set_a11y_name(select, "City");
            for (String city : CITIES) {
                Pt.select_add_item(select, city);
            }
            note(selectBlock, "The filter is ASCII-case-insensitive over the item text. "
                    + "Programmatic selection stays silent; the handler below runs only on "
                    + "commitment.");

            MemorySegment dateBlock = fieldBlock(card, "Date picker");
            MemorySegment date = Pt.date_picker_create(dateBlock);
            Pt.widget_set_grow(date, 1.0f);
            Pt.date_picker_set_format(date, Pt.date_format_iso);
            Pt.widget_set_a11y_name(date, "Date");
            note(dateBlock, "Six fixed weeks; today is highlighted. PtDate.empty() is the "
                    + "cleared value.");

            MemorySegment numberBlock = fieldBlock(card, "Number picker");
            MemorySegment number = Pt.number_picker_create(numberBlock);
            Pt.widget_set_grow(number, 1.0f);
            Pt.number_picker_set_range(number, 0.0, 100.0);
            Pt.number_picker_set_step(number, 1.0);
            Pt.number_picker_set_placeholder(number, "0 – 100");
            Pt.widget_set_a11y_name(number, "Count");
            note(numberBlock, "No popup: Up/Down step by the increment and the field takes "
                    + "typed values. step_by is the programmatic path that reports on_change.");

            MemorySegment colorBlock = fieldBlock(card, "Colour picker");
            MemorySegment color = Pt.color_picker_create(colorBlock);
            Pt.widget_set_grow(color, 1.0f);
            Pt.color_picker_set_hex(color, "#4C8DFF");
            Pt.color_picker_add_preset(color, Pt.rgb(0x4C8DFF));
            Pt.color_picker_add_preset(color, Pt.rgb(0x9ECE6A));
            Pt.color_picker_add_preset(color, Pt.rgb(0xE0AF68));
            Pt.color_picker_add_preset(color, Pt.rgb(0xF7768E));
            Pt.widget_set_a11y_name(color, "Accent");
            note(colorBlock, "Accepts #RRGGBB and #RRGGBBAA. The palette popup is capped at "
                    + Pt.color_max_presets + " presets.");

            Pt.IndexHandler onSelect = (widget, index, data) -> {
                if (index == Pt.list_index_none) {
                    log(status, changes, "select cleared");
                    return;
                }
                log(status, changes, "select committed item " + index + " \""
                        + Pt.select_get_item_text(widget, index) + "\"");
            };
            Pt.DateChangeHandler onDate = (picker, value, data) -> {
                if (value.isEmpty()) {
                    log(status, changes, "date cleared");
                    return;
                }
                log(status, changes, String.format("date committed %04d-%02d-%02d",
                        value.year(), value.month(), value.day()));
            };
            Pt.NumberChangeHandler onNumber = (picker, value, data) -> log(status, changes,
                    String.format("number committed %.3f", value));
            Pt.ColorChangeHandler onColor = (picker, value, data) -> log(status, changes,
                    String.format("colour committed 0x%08X", value));

            Pt.select_set_on_change(select, onSelect, arena);
            Pt.date_picker_set_on_change(date, onDate, arena);
            Pt.number_picker_set_on_change(number, onNumber, arena);
            Pt.color_picker_set_on_change(color, onColor, arena);

            MemorySegment tools = Pt.wrap(card);
            Pt.wrap_set_gap(tools, 8.0f, 8.0f);

            Pt.ClickHandler filterBerlin = (widget, data) -> {
                Pt.select_set_query(select, "ber");
                log(status, changes, "filter \"ber\" matches "
                        + Pt.select_get_match_count(select));
            };
            Pt.ClickHandler commitThird = (widget, data) -> {
                Pt.select_set_selected(select, 2);
                Pt.widget_set_text(status, "Selected item 2 from code; no on_change fired.");
            };
            Pt.ClickHandler stepNumberHandler = (widget, data) -> {
                Pt.number_picker_step_by(number, 2.0);
            };
            Pt.ClickHandler parseHexHandler = (widget, data) -> {
                boolean ok = Pt.color_picker_set_hex(color, "#ff8800");
                log(status, changes, (ok ? "accepted" : "rejected") + " #ff8800 · now "
                        + Pt.color_picker_get_hex(color));
            };
            Pt.ClickHandler clearDateHandler = (widget, data) -> {
                Pt.date_picker_clear(date);
                Pt.widget_set_text(status, "Date cleared; on_change is silent for this setter.");
            };

            MemorySegment filterButton = Pt.button(tools, "Filter “ber”");
            Pt.button_set_variant(filterButton, Pt.button_secondary);
            Pt.button_set_on_click(filterButton, filterBerlin, arena);
            MemorySegment commitButton = Pt.button(tools, "Select item 2");
            Pt.button_set_variant(commitButton, Pt.button_secondary);
            Pt.button_set_on_click(commitButton, commitThird, arena);
            MemorySegment stepButton = Pt.button(tools, "Step +2");
            Pt.button_set_variant(stepButton, Pt.button_primary);
            Pt.button_set_on_click(stepButton, stepNumberHandler, arena);
            MemorySegment hexButton = Pt.button(tools, "Parse #ff8800");
            Pt.button_set_variant(hexButton, Pt.button_ghost);
            Pt.button_set_on_click(hexButton, parseHexHandler, arena);
            MemorySegment clearButton = Pt.button(tools, "Clear date");
            Pt.button_set_variant(clearButton, Pt.button_ghost);
            Pt.button_set_on_click(clearButton, clearDateHandler, arena);

            MemorySegment footer = Pt.label(root,
                    "The committed value is the single source of truth: the popups edit a draft "
                    + "and hand it back, so on_change always sees an accepted value.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_wrap(footer, true);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            Pt.window_show(window);
            Pt.window_focus(window, select);

            /* A stable starting point for both paths. */
            Pt.select_set_selected(select, 0);
            Pt.date_picker_set_date(date, Pt.date_today());
            Pt.number_picker_set_value(number, 42.0);

            int status_code;
            if (!headless()) {
                status_code = Pt.app_run(app);
                Pt.app_destroy(app);
                System.exit(status_code);
            }

            log(status, changes, "select items=" + Pt.select_get_item_count(select)
                    + " max_rows=" + Pt.select_get_max_popup_rows(select)
                    + " placeholder=\"" + Pt.select_get_placeholder(select) + "\"");
            filterBerlin.handle(MemorySegment.NULL, MemorySegment.NULL);
            Pt.select_clear_selection(select);
            Pt.select_set_query(select, "");
            Pt.select_open(select);
            log(status, changes, "select popup open=" + Pt.select_is_open(select));
            commitThird.handle(MemorySegment.NULL, MemorySegment.NULL);
            Pt.select_close(select);
            onSelect.handle(select, Pt.select_get_selected(select), MemorySegment.NULL);

            PtDate today = Pt.date_picker_get_date(date);
            log(status, changes, "date " + today.year() + "-" + today.month() + "-"
                    + today.day() + " valid=" + Pt.date_is_valid(today) + " format="
                    + Pt.date_picker_get_format(date));
            onDate.handle(date, today, MemorySegment.NULL);
            Pt.date_picker_set_date(date, new PtDate(2027, 1, 15));
            onDate.handle(date, Pt.date_picker_get_date(date), MemorySegment.NULL);

            stepNumberHandler.handle(MemorySegment.NULL, MemorySegment.NULL);
            log(status, changes, "number " + Pt.number_picker_get_value(number) + " has_value="
                    + Pt.number_picker_has_value(number) + " text=\""
                    + Pt.number_picker_get_text(number) + "\"");
            onNumber.handle(number, Pt.number_picker_get_value(number), MemorySegment.NULL);

            parseHexHandler.handle(MemorySegment.NULL, MemorySegment.NULL);
            log(status, changes, "colour hex=" + Pt.color_picker_get_hex(color) + " presets="
                    + Pt.color_picker_get_preset_count(color));
            Pt.color_picker_open(color);
            onColor.handle(color, Pt.color_picker_get_color(color), MemorySegment.NULL);
            Pt.color_picker_close(color);

            clearDateHandler.handle(MemorySegment.NULL, MemorySegment.NULL);
            status_code = Pt.app_run_for(app, 250);
            Pt.app_destroy(app);
            System.exit(status_code);
        }
    }
}
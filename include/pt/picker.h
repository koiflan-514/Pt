/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_picker_h
#define pt_picker_h

#include <pt/widget.h>

/* Header contract
 *
 * Overview. The 1.2 pickers are editor fields with a popup surface: a
 * searchable select, a date picker, a number picker (spin box) and a colour
 * picker. They are built on an entry, the application-owned pt_popup surface
 * (include/pt/popup.h) and the existing grid/list primitives, so the popup
 * placement, dismiss and headless fallback rules of pt_popup apply unchanged.
 * A picker is a combo-box widget at the core (pt_widget_get_kind reports
 * pt_widget_combobox) because that is the shipped control kind with the
 * keyboard, pointer, measurement and accessibility hooks the framework
 * dispatches to. Applications must use only the functions in this header on a
 * picker; calling the pt_combobox_* item or mode API on it is a no-op.
 *
 * Relationship to pt_combobox_filterable. A filterable combo box filters its
 * own option model into an in-window overlay that is rebuilt on every
 * keystroke and closes when an option is chosen. The searchable select is the
 * popup-based form: its list lives in a real pt_popup surface (native where the
 * compositor allows it), it can carry item data, it reports "no matches",
 * caps the candidate rows, and it can clear the committed value. Neither
 * replaces the other: the filterable combo box stays the light in-window
 * chooser, the select is the searchable list with an app-owned popup.
 *
 * Threading. UI thread only, including getters, setters, open/close and
 * accessibility queries.
 *
 * Ownership. A constructor appends the picker to parent and transfers ownership
 * to the parent's subtree; a NULL parent returns a detached control owned by
 * the caller (destroy it with pt_widget_destroy). The control owns its entry,
 * its stepper buttons, its popup and every row/cell it materializes. Text and
 * handles returned by getters are borrowed and invalidated by the next mutation
 * of that control. Strings passed to setters and callbacks are copied. Item data
 * pointers in pt_select are borrowed from the application and must stay valid
 * while the item exists.
 *
 * Errors. Setters are no-ops for a NULL or mismatched widget and for invalid
 * arguments; the text-commit setters return false and record pt_get_last_error.
 * A rejected text commit leaves the previous value and the previous display
 * text untouched (the field is restored). Constructors return NULL and set the
 * error only for allocation failure; a detached picker is legal and opens its
 * popup once it has a window.
 *
 * Callbacks. All callbacks run on the UI thread after the control state has
 * been updated. The widget payload is the picker; callbacks may read it, may
 * open or close a popup, and may destroy the picker. When a callback destroys
 * the picker, the picker does not touch it afterwards; the popup is closed or
 * destroyed as part of that destruction. Do not assume the picker is attached
 * after the callback returns.
 *
 * Focus. The focusable target is the entry inside the control, matching the
 * filterable combo box. While a popup is open the entry keeps focus, so typing
 * still filters a select or edits a date/number/colour text. Dismissing the
 * popup (outside click, Escape, Tab, choosing a value) restores focus to the
 * entry. Tab closes the popup and continues in the normal focus order.
 *
 * Accessibility. Each control carries an accessible name (a sensible default
 * per kind; applications override it with pt_widget_set_a11y_name), the combo
 * box role, the expanded state while its popup is open (number pickers have no
 * popup and report no expandable state) and a numeric value: select exposes the
 * selected item index (-1 for none), date exposes YYYYMMDD (0 when empty),
 * number exposes the value (only while it has one) and colour exposes its RGB
 * part normalised to [0, 1], because the accessibility Value interface carries
 * one float and alpha would not survive it. Popup rows and calendar/palette
 * cells expose names and the selected state; a select row that carries the
 * committed value also exposes the current state through the built-in check
 * glyph.
 *
 * Limits. The select model is capped at PT_SELECT_MAX_ITEMS items and a popup
 * exposes at most PT_PICKER_MAX_CANDIDATES matching rows. The colour palette is
 * capped at PT_COLOR_MAX_PRESETS presets. A calendar always renders six weeks
 * (42 cells) so month navigation cannot grow the tree without bound.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of items a searchable select can hold. Adding beyond the cap
 * is rejected with pt_set_error and leaves the model unchanged. Since 1.2. */
#define PT_SELECT_MAX_ITEMS 1024u

/* Maximum number of candidate rows one picker popup exposes, and the upper
 * bound accepted by pt_select_set_max_popup_rows. Since 1.2. */
#define PT_PICKER_MAX_CANDIDATES 256u

/* Maximum number of presets a colour picker palette can hold. Adding beyond the
 * cap is rejected with pt_set_error. Since 1.2. */
#define PT_COLOR_MAX_PRESETS 64u

/* ---------- searchable select ---------- */

/* Creates a searchable select: an entry that filters a popup list as the user
 * types. The popup opens on the first keystroke, on Down/Up, F4, Alt+Down,
 * Alt+Up and a press on the field or its chevron. Typing filters by
 * ASCII-case-insensitive substring, the arrow keys move the highlight, Enter
 * commits it, Escape closes and restores the committed value, Home/End jump to
 * the first/last match, Page Up/Page Down step by five, and Delete with an
 * empty query clears the committed value. The chosen label fills the entry.
 * The popup reports an unmatched query with a disabled "No matches" row.
 * Since 1.2. */
pt_api pt_widget *pt_select_create(pt_widget *parent);

/* Sets the placeholder shown while the entry is empty. Copied. Since 1.2. */
pt_api void pt_select_set_placeholder(pt_widget *select, const char *text);
pt_api const char *pt_select_get_placeholder(const pt_widget *select);

/* Appends or inserts an item. index is clamped to the current count. The text
 * is copied; data is borrowed. Adding past PT_SELECT_MAX_ITEMS is rejected.
 * Inserting above the selection keeps it on the same item. Since 1.2. */
pt_api void pt_select_add_item(pt_widget *select, const char *text);
pt_api void pt_select_insert_item(pt_widget *select, uint32_t index, const char *text);
pt_api void pt_select_add_item_with_data(pt_widget *select, const char *text, void *data);

/* Removes one item or the whole model. Removing the selected item clears the
 * selection and the field. Since 1.2. */
pt_api void pt_select_remove_item(pt_widget *select, uint32_t index);
pt_api void pt_select_clear(pt_widget *select);

pt_api uint32_t pt_select_get_item_count(const pt_widget *select);
pt_api const char *pt_select_get_item_text(const pt_widget *select, uint32_t index);
pt_api void *pt_select_get_item_data(const pt_widget *select, uint32_t index);
pt_api void pt_select_set_item_data(pt_widget *select, uint32_t index, void *data);

/* The committed item index, or pt_list_index_none. pt_select_set_selected is
 * silent (it never fires on_change) and an out-of-range index clears the
 * selection, matching pt_combobox_set_selected. pt_select_clear_selection is
 * the explicit clear path. Since 1.2. */
pt_api uint32_t pt_select_get_selected(const pt_widget *select);
pt_api void pt_select_set_selected(pt_widget *select, uint32_t index);
pt_api void pt_select_clear_selection(pt_widget *select);

/* The current filter text. The field owns the live text; this getter returns
 * the same string. pt_select_set_query copies and filters without opening the
 * popup. Since 1.2. */
pt_api const char *pt_select_get_query(const pt_widget *select);
pt_api void pt_select_set_query(pt_widget *select, const char *text);

/* Matching rows currently exposed by the popup, in model order. The count is
 * capped at the popup row limit. pt_select_get_match_item maps a visible
 * position back to an item index (pt_list_index_none when out of range).
 * Since 1.2. */
pt_api uint32_t pt_select_get_match_count(const pt_widget *select);
pt_api uint32_t pt_select_get_match_item(const pt_widget *select, uint32_t position);

/* Popup row limit, 1..PT_PICKER_MAX_CANDIDATES; the default is 8. Values are
 * clamped and rows beyond the limit are not exposed as candidates. Since 1.2. */
pt_api void pt_select_set_max_popup_rows(pt_widget *select, uint32_t rows);
pt_api uint32_t pt_select_get_max_popup_rows(const pt_widget *select);

/* Opens or closes the popup, or reports it. Closing is a cancel: the committed
 * label returns to the field and the filter is dropped. pt_select_open is a
 * no-op without a window or with an empty model. Since 1.2. */
pt_api void pt_select_open(pt_widget *select);
pt_api void pt_select_close(pt_widget *select);
pt_api bool pt_select_is_open(const pt_widget *select);

/* Fires when the user commits an item, with its model index. Programmatic
 * selection never fires it and choosing the value already committed stays
 * silent. The callback may destroy the picker. Since 1.2. */
pt_api void pt_select_set_on_change(pt_widget *select, pt_index_callback callback,
                                    void *user_data);

/* ---------- date picker ---------- */

/* A civil date. year 1..9999, month 1..12, day 1..days_in_month(year, month).
 * A zero-initialized value (year == 0) is the empty value; there is no time of
 * day and no time zone anywhere in this control. Since 1.2. */
typedef struct pt_date {
    int32_t year;
    uint8_t month;
    uint8_t day;
} pt_date;

/* How the field writes a date. The control does not read the process locale;
 * the application picks the picture and may localize around the control.
 * pt_date_format_iso is YYYY-MM-DD, dmy is DD/MM/YYYY and mdy is MM/DD/YYYY.
 * Parsing accepts the chosen picture and ISO as a fallback. Since 1.2. */
typedef enum pt_date_format {
    pt_date_format_iso = 0,
    pt_date_format_dmy,
    pt_date_format_mdy
} pt_date_format;

/* Fires after the committed date changes; date is the new value and is the
 * empty date after a clear. The callback may destroy the picker. Since 1.2. */
typedef void (*pt_date_callback)(pt_widget *picker, pt_date date, void *user_data);

/* Today's local civil date. This is the one place the control reads the clock;
 * the stored value stays a plain y/m/d. Returns an empty date when the local
 * date is outside the 1..9999 range. Since 1.2. */
pt_api pt_date pt_date_today(void);

/* True for a real civil date. Since 1.2. */
pt_api bool pt_date_is_valid(pt_date date);

/* Creates a date picker: an entry showing the formatted date and a popup
 * calendar. Typing edits the text; Enter parses it (an invalid date is rejected
 * and the previous value and text stay), Escape restores the committed text.
 * F4 or Alt+Down opens the calendar, where Left/Right move one day, Up/Down one
 * week, Page Up/Page Down one month, Home/End the month edges, T selects today
 * and Enter commits the focused day. A press on a day commits it. The calendar
 * always renders six weeks (42 cells), with adjacent-month cells disabled.
 * Since 1.2. */
pt_api pt_widget *pt_date_picker_create(pt_widget *parent);

/* Sets the committed date. A zero-initialized date clears the value and returns
 * true; a structural error (bad month, day or year) returns false and changes
 * nothing. Silent. */
pt_api bool pt_date_picker_set_date(pt_widget *picker, pt_date date);
pt_api pt_date pt_date_picker_get_date(const pt_widget *picker);
pt_api void pt_date_picker_clear(pt_widget *picker);

/* Picture used by the field. Changing it rewrites the current text. Since 1.2. */
pt_api void pt_date_picker_set_format(pt_widget *picker, pt_date_format format);
pt_api pt_date_format pt_date_picker_get_format(const pt_widget *picker);

/* Parses and commits text. An empty string clears the date. Returns false and
 * records pt_get_last_error on an invalid date; the previous value and field
 * text are then untouched. Silent on success (callbacks report user input, not
 * programmatic changes). Since 1.2. */
pt_api bool pt_date_picker_set_text(pt_widget *picker, const char *text);
/* Borrowed committed display text, "" when empty. Since 1.2. */
pt_api const char *pt_date_picker_get_text(const pt_widget *picker);

pt_api void pt_date_picker_set_placeholder(pt_widget *picker, const char *text);
pt_api const char *pt_date_picker_get_placeholder(const pt_widget *picker);

pt_api void pt_date_picker_open(pt_widget *picker);
pt_api void pt_date_picker_close(pt_widget *picker);
pt_api bool pt_date_picker_is_open(const pt_widget *picker);

pt_api void pt_date_picker_set_on_change(pt_widget *picker, pt_date_callback callback,
                                         void *user_data);

/* ---------- number picker ---------- */

/* Fires after the value changes through user input; value is the clamped new
 * value. The callback may destroy the picker. Since 1.2. */
typedef void (*pt_number_callback)(pt_widget *picker, double value, void *user_data);

/* Creates a spin box: an entry, an increment and a decrement button. The
 * default range is [0, 100] with a step of 1 and no value (empty field). Up and
 * Down step by one, Page Up/Page Down by ten, Home jumps to the minimum and End
 * to the maximum. Typed text commits on Enter (a number is clamped into the
 * range); a non-numeric text is rejected with the previous value and text kept.
 * Escape restores the committed text. Stepping an empty picker starts from the
 * clamped zero and then applies the step. Values, minimum, maximum and step are
 * finite doubles; setters ignore NaN or infinity. The number picker has no
 * popup, so it reports no expandable state. Since 1.2. */
pt_api pt_widget *pt_number_picker_create(pt_widget *parent);

/* Sets the inclusive range. A non-finite bound or min > max is rejected with
 * pt_get_last_error and changes nothing. A current value outside the new range
 * is clamped. Silent. Since 1.2. */
pt_api void pt_number_picker_set_range(pt_widget *picker, double minimum, double maximum);
pt_api double pt_number_picker_get_min(const pt_widget *picker);
pt_api double pt_number_picker_get_max(const pt_widget *picker);

/* A positive, finite increment. Invalid values are rejected silently with the
 * error set. Since 1.2. */
pt_api void pt_number_picker_set_step(pt_widget *picker, double step);
pt_api double pt_number_picker_get_step(const pt_widget *picker);

/* Sets a value, clamped into the range. A non-finite value is ignored. Silent. */
pt_api void pt_number_picker_set_value(pt_widget *picker, double value);
/* The value, or 0.0 while the picker is empty; use has_value to tell them
 * apart. Since 1.2. */
pt_api double pt_number_picker_get_value(const pt_widget *picker);
pt_api bool pt_number_picker_has_value(const pt_widget *picker);
/* Empties the value and the field. Silent. Since 1.2. */
pt_api void pt_number_picker_clear(pt_widget *picker);

/* Steps by steps * step with the result clamped. The multiplier must be finite.
 * Stepping an empty picker first takes the clamped zero as the base. Since 1.2. */
pt_api void pt_number_picker_step_by(pt_widget *picker, double steps);

/* Parses and commits text. Empty text clears the value; a non-numeric text is
 * rejected, keeps the old value and restores the old text. Silent on success. */
pt_api bool pt_number_picker_set_text(pt_widget *picker, const char *text);
/* Borrowed committed display text, "" while empty. Locale-independent: the
 * decimal separator is always '.', both '.' and ',' are accepted on input. */
pt_api const char *pt_number_picker_get_text(const pt_widget *picker);

pt_api void pt_number_picker_set_placeholder(pt_widget *picker, const char *text);
pt_api const char *pt_number_picker_get_placeholder(const pt_widget *picker);

pt_api void pt_number_picker_set_on_change(pt_widget *picker, pt_number_callback callback,
                                           void *user_data);

/* ---------- colour picker ---------- */

/* Fires after the committed colour changes through user input. The callback may
 * destroy the picker. Since 1.2. */
typedef void (*pt_color_callback)(pt_widget *picker, pt_color color, void *user_data);

/* Creates a colour picker: an entry holding a hex value, a preview swatch and a
 * popup preset palette. The initial colour is opaque black. The field accepts
 * #RRGGBB and #RRGGBBAA (the # is optional); an invalid text is rejected with
 * the previous colour and text kept. F4 or Alt+Down opens the palette; Left/
 * Right move one preset, Up/Down one row (six columns), Home/End the edges and
 * Enter commits the highlighted preset. Enter with edited text commits the hex
 * value instead, Escape restores the committed text, and a press on a preset
 * commits it. The accessible value is the RGB part normalised to [0, 1].
 * Since 1.2. */
pt_api pt_widget *pt_color_picker_create(pt_widget *parent);

/* Sets the colour silently. Since 1.2. */
pt_api void pt_color_picker_set_color(pt_widget *picker, pt_color color);
pt_api pt_color pt_color_picker_get_color(const pt_widget *picker);

/* Parses and commits #RRGGBB / #RRGGBBAA. Returns false and records
 * pt_get_last_error for an invalid text, leaving the colour and field text
 * unchanged. Silent on success. Since 1.2. */
pt_api bool pt_color_picker_set_hex(pt_widget *picker, const char *hex);
/* Borrowed committed hex text: #RRGGBB for opaque colours, #RRGGBBAA otherwise.
 * Since 1.2. */
pt_api const char *pt_color_picker_get_hex(const pt_widget *picker);

/* The preset palette starts with 24 common colours. add appends (rejected past
 * PT_COLOR_MAX_PRESETS with pt_get_last_error); clear removes every preset.
 * Since 1.2. */
pt_api void pt_color_picker_add_preset(pt_widget *picker, pt_color color);
pt_api void pt_color_picker_clear_presets(pt_widget *picker);
pt_api uint32_t pt_color_picker_get_preset_count(const pt_widget *picker);
pt_api pt_color pt_color_picker_get_preset(const pt_widget *picker, uint32_t index);

pt_api void pt_color_picker_open(pt_widget *picker);
pt_api void pt_color_picker_close(pt_widget *picker);
pt_api bool pt_color_picker_is_open(const pt_widget *picker);

pt_api void pt_color_picker_set_on_change(pt_widget *picker, pt_color_callback callback,
                                          void *user_data);

#ifdef __cplusplus
}
#endif

#endif
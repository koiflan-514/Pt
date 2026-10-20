/*
 * ABI append-only guard.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The 1.0 compatibility policy makes every public enum append-only and keeps
 * the public struct layouts stable. This test pins the numeric values, bit
 * positions, field order and sentinel constants that an application compiled
 * against 1.0 will bake in. It fails the build when a renumbering, reordering
 * or bit move sneaks into a header without an explicit ABI decision.
 */

#include "harness.h"

#include <pt/pt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Widget kinds. New kinds are appended before pt_widget_combobox in a future
 * release; existing numbers never move. */
_Static_assert(pt_widget_box == 0, "widget kind box must stay 0");
_Static_assert(pt_widget_label == 1, "widget kind label must stay 1");
_Static_assert(pt_widget_button == 2, "widget kind button must stay 2");
_Static_assert(pt_widget_combobox == 34, "widget kind combobox must stay 34");

/* Theme tokens and metrics. */
_Static_assert(pt_theme_background == 0, "theme background must stay 0");
_Static_assert(pt_theme_surface_container == 24, "0.9 tokens start at 24");
_Static_assert(pt_theme_selection_text == 37, "selection text must stay 37");
_Static_assert(pt_theme_token_count == 38, "theme token count must stay 38");
_Static_assert(pt_theme_metric_radius_small == 0, "first metric must stay 0");
_Static_assert(pt_theme_metric_control_height == 9, "control height must stay 9");
_Static_assert(pt_theme_metric_count == 10, "metric count must stay 10");

/* Accessibility model. */
_Static_assert(pt_a11y_role_none == 0, "role none must stay 0");
_Static_assert(pt_a11y_role_combo_box == 34, "combobox role must stay 34");
_Static_assert(pt_a11y_role_list_box == 35, "list box role must stay 35");
_Static_assert(pt_a11y_role_count == 36, "role count must stay 36");
_Static_assert(pt_a11y_state_visible == (1u << 0), "visible state bit");
_Static_assert(pt_a11y_state_expanded == (1u << 7), "expanded state bit");
_Static_assert(pt_a11y_state_modal == (1u << 9), "modal state bit");
_Static_assert(pt_a11y_state_expandable == (1u << 10), "expandable state bit");
_Static_assert(pt_a11y_state_selectable == (1u << 11), "selectable state bit");
_Static_assert(pt_a11y_state_multi_selectable == (1u << 12),
               "multi-selectable state bit");
_Static_assert(pt_a11y_event_focus_changed == 0, "focus event must stay 0");
_Static_assert(pt_a11y_event_structure_changed == 6, "structure event must stay 6");

/* Input, drag and motion enums. */
_Static_assert(pt_input_purpose_normal == 0, "normal purpose must stay 0");
_Static_assert(pt_input_purpose_terminal == 6, "terminal purpose must stay 6");
_Static_assert(pt_button_primary == 0, "primary button must stay 0");
_Static_assert(pt_button_soft == 4, "soft button must stay 4");
_Static_assert(pt_icon_none == 0, "icon none must stay 0");
_Static_assert(pt_icon_warning == 21, "warning icon must stay 21");
_Static_assert(pt_icon_chevron_right == 1, "chevron icon must stay 1");
_Static_assert(pt_sort_none == 0, "sort none must stay 0");
_Static_assert(pt_sort_descending == 2, "descending sort must stay 2");
_Static_assert(pt_combobox_select_only == 0, "select-only mode must stay 0");
_Static_assert(pt_combobox_filterable == 1, "filterable mode must stay 1");
_Static_assert(pt_drag_action_none == 0, "drag action none must stay 0");
_Static_assert(pt_drag_action_copy == 1, "copy action bit");
_Static_assert(pt_drag_action_move == 2, "move action bit");
_Static_assert(pt_drag_action_ask == 4, "ask action bit");
_Static_assert(pt_drop_stream_data == 0, "stream data must stay 0");
_Static_assert(pt_drop_stream_error == 3, "stream error must stay 3");
_Static_assert(pt_popup_policy_auto == 0, "popup auto must stay 0");
_Static_assert(pt_popup_policy_window == 2, "popup window must stay 2");
_Static_assert(pt_toast_info == 0, "toast info must stay 0");
_Static_assert(pt_toast_error == 3, "toast error must stay 3");
_Static_assert(pt_gesture_kind_swipe == 0, "swipe must stay 0");
_Static_assert(pt_gesture_kind_hold == 2, "hold must stay 2");
_Static_assert(pt_gesture_phase_begin == 0, "phase begin must stay 0");
_Static_assert(pt_gesture_phase_cancel == 3, "phase cancel must stay 3");
_Static_assert(pt_stylus_tool_pen == 0, "pen must stay 0");
_Static_assert(pt_stylus_tool_unknown == 5, "unknown tool must stay 5");
_Static_assert(pt_stylus_phase_enter == 0, "stylus enter must stay 0");
_Static_assert(pt_stylus_phase_leave == 4, "stylus leave must stay 4");
_Static_assert(pt_easing_linear == 0, "linear must stay 0");
_Static_assert(pt_easing_out_back == 6, "out-back must stay 6");
_Static_assert(pt_color_scheme_dark == 0, "dark must stay 0");
_Static_assert(pt_color_scheme_light == 1, "light must stay 1");
_Static_assert(pt_align_auto == 0, "align auto must stay 0");
_Static_assert(pt_align_stretch == 4, "align stretch must stay 4");
_Static_assert(pt_direction_row == 0, "row must stay 0");
_Static_assert(pt_justify_space_evenly == 5, "space-evenly must stay 5");
_Static_assert(pt_text_align_end == 2, "text align end must stay 2");
_Static_assert(pt_font_weight_regular == 400, "regular weight must stay 400");
_Static_assert(pt_font_weight_bold == 700, "bold weight must stay 700");
_Static_assert(pt_font_slant_oblique == 2, "oblique must stay 2");
_Static_assert(pt_text_span_underline == (1u << 0), "underline span bit");
_Static_assert(pt_text_span_has_size == (1u << 5), "size span bit");
_Static_assert(pt_key_modifier_shift == (1u << 0), "shift modifier bit");
_Static_assert(pt_key_modifier_logo == (1u << 3), "logo modifier bit");

/* Sentinel constants and limits. */
_Static_assert(pt_list_index_none == (uint32_t)-1, "list none sentinel");
_Static_assert(pt_table_column_none == (uint32_t)-1, "column none sentinel");
_Static_assert(pt_row_key_none == (uint64_t)-1, "row key none sentinel");
_Static_assert(PT_TABLE_MAX_SORT_COLUMNS == 4, "four sort columns");
_Static_assert(PT_STYLUS_BUTTON_BARREL == 0x1u, "barrel button bit");
_Static_assert(PT_STYLUS_BUTTON_SECONDARY == 0x2u, "secondary button bit");
_Static_assert(PT_STYLUS_BUTTON_TERTIARY == 0x4u, "tertiary button bit");
_Static_assert(pt_color_transparent == 0x00000000u, "transparent sentinel");

/* Public struct field order. Offsets may differ across architectures, but the
 * order may not: applications depend on these layouts for callbacks. */
_Static_assert(offsetof(pt_text_span, start) < offsetof(pt_text_span, length), "span order");
_Static_assert(offsetof(pt_text_span, length) < offsetof(pt_text_span, weight), "span order");
_Static_assert(offsetof(pt_text_span, color) < offsetof(pt_text_span, flags), "span order");
_Static_assert(offsetof(pt_text_span, flags) < offsetof(pt_text_span, family), "span order");
_Static_assert(offsetof(pt_text_span, family) < offsetof(pt_text_span, size), "span order");
_Static_assert(sizeof(((pt_text_span *)0)->family) == 64, "family capacity");

_Static_assert(offsetof(pt_gesture_event, kind) == 0, "gesture kind first");
_Static_assert(offsetof(pt_gesture_event, kind) < offsetof(pt_gesture_event, phase),
               "gesture order");
_Static_assert(offsetof(pt_gesture_event, fingers) < offsetof(pt_gesture_event, x),
               "gesture order");
_Static_assert(offsetof(pt_gesture_event, rotation) < offsetof(pt_gesture_event, target),
               "gesture order");

_Static_assert(offsetof(pt_stylus_event, phase) == 0, "stylus phase first");
_Static_assert(offsetof(pt_stylus_event, tool) < offsetof(pt_stylus_event, x), "stylus order");
_Static_assert(offsetof(pt_stylus_event, rotation) < offsetof(pt_stylus_event, buttons),
               "stylus order");

_Static_assert(offsetof(pt_drag_data, mime_type) == 0, "drag data mime first");
_Static_assert(offsetof(pt_drag_data, data) < offsetof(pt_drag_data, size), "drag data order");

_Static_assert(offsetof(pt_drop_target, enter) == 0, "drop target enter first");
_Static_assert(offsetof(pt_drop_target, motion) < offsetof(pt_drop_target, leave),
               "drop target order");
_Static_assert(offsetof(pt_drop_target, drop) < offsetof(pt_drop_target, user_data),
               "drop target order");
_Static_assert(offsetof(pt_drop_target, user_data) < offsetof(pt_drop_target, enter_offer),
               "drop target order");

_Static_assert(offsetof(pt_transition, value) == 0, "transition value first");
_Static_assert(offsetof(pt_transition, start) < offsetof(pt_transition, target),
               "transition order");
_Static_assert(offsetof(pt_transition, elapsed) < offsetof(pt_transition, duration),
               "transition order");
_Static_assert(offsetof(pt_transition, duration) < offsetof(pt_transition, easing),
               "transition order");
_Static_assert(offsetof(pt_transition, easing) < offsetof(pt_transition, active),
               "transition order");

/* Runtime invariants that cannot be expressed as an offset comparison. */
int main(void) {
    char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d", pt_version_major, pt_version_minor,
             pt_version_patch);
    pt_check(strcmp(version, pt_version_string) == 0);
    pt_check(sizeof(pt_color) == 4);
    pt_check(sizeof(pt_text_span) % sizeof(uint32_t) == 0);
    pt_check(pt_auto < 0.0f);
    pt_check(pt_ease(pt_easing_linear, 0.0f) == 0.0f);
    pt_check(pt_ease(pt_easing_linear, 1.0f) == 1.0f);

    pt_transition transition;
    pt_transition_init(&transition, 0.0f);
    pt_transition_to(&transition, 1.0f, 100, pt_easing_linear);
    pt_check(pt_transition_active(&transition));
    pt_check(pt_transition_advance(&transition, 0.05f));
    pt_check(!pt_transition_advance(&transition, 0.05f));
    pt_check_near(transition.value, 1.0, 0.0001);

    /* The a11y role and state tables are the fields assistive technology
     * consumes; a count mismatch is an API break even when no value moved. */
    pt_check(pt_a11y_role_count == pt_a11y_role_list_box + 1);
    pt_check(pt_theme_token_count == pt_theme_selection_text + 1);
    pt_check(pt_theme_metric_count == pt_theme_metric_control_height + 1);

    return pt_test_finish("abi");
}

"""Pt - Python bindings for the Pt Wayland-native GUI framework.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

The binding speaks the flat C ABI through ctypes. Point PT_LIBRARY at
libpt.so if it is not in the default build directory. All calls must happen
on the thread that created the app.
"""

from __future__ import annotations

import ctypes as _ctypes
import os as _os
import pathlib as _pathlib
from typing import Any, Callable, Optional

__all__ = [
    "App",
    "Window",
    "Widget",
    "Menu",
    "Dialog",
    "Toast",
    "DropOffer",
    "DropStream",
    "GestureEvent",
    "StylusEvent",
    "TextSpan",
    "DragData",
    "TreeNode",
    "Transition",
    "pt_color_transparent",
    "align_auto",
    "align_start",
    "align_center",
    "align_end",
    "align_stretch",
    "direction_row",
    "direction_column",
    "justify_start",
    "justify_center",
    "justify_end",
    "justify_space_between",
    "justify_space_around",
    "justify_space_evenly",
    "text_align_start",
    "text_align_center",
    "text_align_end",
    "easing_linear",
    "easing_in_quad",
    "easing_out_quad",
    "easing_in_out_quad",
    "easing_out_cubic",
    "easing_in_out_cubic",
    "easing_out_back",
    "list_index_none",
    "table_column_none",
    "row_key_none",
    "table_max_sort_columns",
    "sort_none",
    "sort_ascending",
    "sort_descending",
    "popup_policy_auto",
    "popup_policy_native",
    "popup_policy_window",
    "combobox_select_only",
    "combobox_filterable",
    "icon_none",
    "icon_chevron_right",
    "icon_chevron_down",
    "icon_chevron_left",
    "icon_chevron_up",
    "icon_search",
    "icon_menu",
    "icon_close",
    "icon_check",
    "icon_folder",
    "icon_document",
    "icon_plus",
    "icon_settings",
    "icon_home",
    "icon_bell",
    "icon_user",
    "icon_refresh",
    "icon_trash",
    "icon_arrow_right",
    "icon_arrow_left",
    "icon_info",
    "icon_warning",
    "font_weight_regular",
    "font_weight_medium",
    "font_weight_semibold",
    "font_weight_bold",
    "font_slant_upright",
    "font_slant_italic",
    "font_slant_oblique",
    "text_span_underline",
    "text_span_has_weight",
    "text_span_has_slant",
    "text_span_has_color",
    "text_span_has_family",
    "text_span_has_size",
    "input_purpose_normal",
    "input_purpose_email",
    "input_purpose_url",
    "input_purpose_phone",
    "input_purpose_number",
    "input_purpose_password",
    "input_purpose_terminal",
    "color_scheme_dark",
    "color_scheme_light",
    "button_primary",
    "button_secondary",
    "button_ghost",
    "button_danger",
    "button_soft",
    "theme_background",
    "theme_surface",
    "theme_surface_raised",
    "theme_surface_hover",
    "theme_border",
    "theme_border_strong",
    "theme_text",
    "theme_text_muted",
    "theme_accent",
    "theme_accent_hover",
    "theme_accent_text",
    "theme_danger",
    "theme_success",
    "theme_warning",
    "theme_shadow",
    "theme_titlebar",
    "theme_scrollbar",
    "theme_focus",
    "theme_surface_sunken",
    "theme_control",
    "theme_control_hover",
    "theme_accent_soft",
    "theme_selection",
    "theme_track",
    "theme_surface_container",
    "theme_surface_container_high",
    "theme_outline",
    "theme_outline_variant",
    "theme_text_disabled",
    "theme_accent_pressed",
    "theme_control_pressed",
    "theme_danger_soft",
    "theme_success_soft",
    "theme_warning_soft",
    "theme_scrim",
    "theme_skeleton",
    "theme_skeleton_highlight",
    "theme_selection_text",
    "theme_metric_radius_small",
    "theme_metric_radius_medium",
    "theme_metric_radius_large",
    "theme_metric_radius_pill",
    "theme_metric_focus_width",
    "theme_metric_focus_offset",
    "theme_metric_elevation_low",
    "theme_metric_elevation_high",
    "theme_metric_chip_height",
    "theme_metric_control_height",
    "key_modifier_shift",
    "key_modifier_ctrl",
    "key_modifier_alt",
    "key_modifier_logo",
    "toast_info",
    "toast_success",
    "toast_warning",
    "toast_error",
    "gesture_kind_swipe",
    "gesture_kind_pinch",
    "gesture_kind_hold",
    "gesture_phase_begin",
    "gesture_phase_update",
    "gesture_phase_end",
    "gesture_phase_cancel",
    "stylus_tool_pen",
    "stylus_tool_eraser",
    "stylus_tool_brush",
    "stylus_tool_pencil",
    "stylus_tool_airbrush",
    "stylus_tool_unknown",
    "stylus_phase_enter",
    "stylus_phase_motion",
    "stylus_phase_down",
    "stylus_phase_up",
    "stylus_phase_leave",
    "a11y_role_none",
    "a11y_role_generic",
    "a11y_role_panel",
    "a11y_role_label",
    "a11y_role_push_button",
    "a11y_role_text",
    "a11y_role_toggle_button",
    "a11y_role_switch",
    "a11y_role_slider",
    "a11y_role_scroll_pane",
    "a11y_role_separator",
    "a11y_role_check_box",
    "a11y_role_radio_button",
    "a11y_role_progress_bar",
    "a11y_role_spinner",
    "a11y_role_group",
    "a11y_role_menu",
    "a11y_role_menu_item",
    "a11y_role_dialog",
    "a11y_role_tooltip",
    "a11y_role_alert",
    "a11y_role_list",
    "a11y_role_list_item",
    "a11y_role_text_frame",
    "a11y_role_table",
    "a11y_role_table_cell",
    "a11y_role_table_row",
    "a11y_role_column_header",
    "a11y_role_tree",
    "a11y_role_tree_item",
    "a11y_role_tab_list",
    "a11y_role_tab",
    "a11y_role_tab_panel",
    "a11y_role_icon",
    "a11y_role_combo_box",
    "a11y_role_list_box",
    "a11y_state_visible",
    "a11y_state_enabled",
    "a11y_state_focusable",
    "a11y_state_focused",
    "a11y_state_checked",
    "a11y_state_selected",
    "a11y_state_editable",
    "a11y_state_expanded",
    "a11y_state_busy",
    "a11y_state_modal",
    "a11y_state_expandable",
    "a11y_state_selectable",
    "a11y_state_multi_selectable",
    "a11y_event_focus_changed",
    "a11y_event_state_changed",
    "a11y_event_text_changed",
    "a11y_event_selection_changed",
    "a11y_event_value_changed",
    "a11y_event_name_changed",
    "a11y_event_structure_changed",
    "a11y_status_disabled",
    "a11y_status_unavailable",
    "a11y_status_active",
    "drag_action_none",
    "drag_action_copy",
    "drag_action_move",
    "drag_action_ask",
    "drop_stream_data",
    "drop_stream_done",
    "drop_stream_cancelled",
    "drop_stream_error",
    "auto",
    "rgb",
    "rgba",
    "color_alpha",
    "ease",
    "ease_value",
    "uri_list",
    "uri_list_count",
    "uri_list_get",
    "box",
    "row",
    "column",
    "label",
    "button",
    "entry",
    "text_area",
    "toggle",
    "checkbox",
    "radio",
    "slider",
    "progress",
    "spinner",
    "segmented",
    "scroll",
    "wrap",
    "grid",
    "list_view",
    "table",
    "tree",
    "combobox",
    "tabs",
    "sidebar",
    "icon",
    "icon_button",
    "divider",
    "card",
    "spacer",
    "chip",
    "skeleton",
]

auto = -1.0
pt_color_transparent = 0x00000000

align_auto = 0
align_start = 1
align_center = 2
align_end = 3
align_stretch = 4

direction_row = 0
direction_column = 1

justify_start = 0
justify_center = 1
justify_end = 2
justify_space_between = 3
justify_space_around = 4
justify_space_evenly = 5

text_align_start = 0
text_align_center = 1
text_align_end = 2

easing_linear = 0
easing_in_quad = 1
easing_out_quad = 2
easing_in_out_quad = 3
easing_out_cubic = 4
easing_in_out_cubic = 5
easing_out_back = 6

list_index_none = 0xFFFFFFFF
table_column_none = 0xFFFFFFFF
row_key_none = 0xFFFFFFFFFFFFFFFF
table_max_sort_columns = 4

sort_none = 0
sort_ascending = 1
sort_descending = 2

popup_policy_auto = 0
popup_policy_native = 1
popup_policy_window = 2

combobox_select_only = 0
combobox_filterable = 1

icon_none = 0
icon_chevron_right = 1
icon_chevron_down = 2
icon_chevron_left = 3
icon_chevron_up = 4
icon_search = 5
icon_menu = 6
icon_close = 7
icon_check = 8
icon_folder = 9
icon_document = 10
icon_plus = 11
icon_settings = 12
icon_home = 13
icon_bell = 14
icon_user = 15
icon_refresh = 16
icon_trash = 17
icon_arrow_right = 18
icon_arrow_left = 19
icon_info = 20
icon_warning = 21

font_weight_regular = 400
font_weight_medium = 500
font_weight_semibold = 600
font_weight_bold = 700

font_slant_upright = 0
font_slant_italic = 1
font_slant_oblique = 2

text_span_underline = 1 << 0
text_span_has_weight = 1 << 1
text_span_has_slant = 1 << 2
text_span_has_color = 1 << 3
text_span_has_family = 1 << 4
text_span_has_size = 1 << 5

input_purpose_normal = 0
input_purpose_email = 1
input_purpose_url = 2
input_purpose_phone = 3
input_purpose_number = 4
input_purpose_password = 5
input_purpose_terminal = 6

color_scheme_dark = 0
color_scheme_light = 1

button_primary = 0
button_secondary = 1
button_ghost = 2
button_danger = 3
button_soft = 4

theme_background = 0
theme_surface = 1
theme_surface_raised = 2
theme_surface_hover = 3
theme_border = 4
theme_border_strong = 5
theme_text = 6
theme_text_muted = 7
theme_accent = 8
theme_accent_hover = 9
theme_accent_text = 10
theme_danger = 11
theme_success = 12
theme_warning = 13
theme_shadow = 14
theme_titlebar = 15
theme_scrollbar = 16
theme_focus = 17
theme_surface_sunken = 18
theme_control = 19
theme_control_hover = 20
theme_accent_soft = 21
theme_selection = 22
theme_track = 23
theme_surface_container = 24
theme_surface_container_high = 25
theme_outline = 26
theme_outline_variant = 27
theme_text_disabled = 28
theme_accent_pressed = 29
theme_control_pressed = 30
theme_danger_soft = 31
theme_success_soft = 32
theme_warning_soft = 33
theme_scrim = 34
theme_skeleton = 35
theme_skeleton_highlight = 36
theme_selection_text = 37

theme_metric_radius_small = 0
theme_metric_radius_medium = 1
theme_metric_radius_large = 2
theme_metric_radius_pill = 3
theme_metric_focus_width = 4
theme_metric_focus_offset = 5
theme_metric_elevation_low = 6
theme_metric_elevation_high = 7
theme_metric_chip_height = 8
theme_metric_control_height = 9

key_modifier_shift = 1 << 0
key_modifier_ctrl = 1 << 1
key_modifier_alt = 1 << 2
key_modifier_logo = 1 << 3

toast_info = 0
toast_success = 1
toast_warning = 2
toast_error = 3

gesture_kind_swipe = 0
gesture_kind_pinch = 1
gesture_kind_hold = 2

gesture_phase_begin = 0
gesture_phase_update = 1
gesture_phase_end = 2
gesture_phase_cancel = 3

stylus_tool_pen = 0
stylus_tool_eraser = 1
stylus_tool_brush = 2
stylus_tool_pencil = 3
stylus_tool_airbrush = 4
stylus_tool_unknown = 5

stylus_phase_enter = 0
stylus_phase_motion = 1
stylus_phase_down = 2
stylus_phase_up = 3
stylus_phase_leave = 4

a11y_role_none = 0
a11y_role_generic = 1
a11y_role_panel = 2
a11y_role_label = 3
a11y_role_push_button = 4
a11y_role_text = 5
a11y_role_toggle_button = 6
a11y_role_switch = 7
a11y_role_slider = 8
a11y_role_scroll_pane = 9
a11y_role_separator = 10
a11y_role_check_box = 11
a11y_role_radio_button = 12
a11y_role_progress_bar = 13
a11y_role_spinner = 14
a11y_role_group = 15
a11y_role_menu = 16
a11y_role_menu_item = 17
a11y_role_dialog = 18
a11y_role_tooltip = 19
a11y_role_alert = 20
a11y_role_list = 21
a11y_role_list_item = 22
a11y_role_text_frame = 23
a11y_role_table = 24
a11y_role_table_cell = 25
a11y_role_table_row = 26
a11y_role_column_header = 27
a11y_role_tree = 28
a11y_role_tree_item = 29
a11y_role_tab_list = 30
a11y_role_tab = 31
a11y_role_tab_panel = 32
a11y_role_icon = 33
a11y_role_combo_box = 34
a11y_role_list_box = 35

a11y_state_visible = 1 << 0
a11y_state_enabled = 1 << 1
a11y_state_focusable = 1 << 2
a11y_state_focused = 1 << 3
a11y_state_checked = 1 << 4
a11y_state_selected = 1 << 5
a11y_state_editable = 1 << 6
a11y_state_expanded = 1 << 7
a11y_state_busy = 1 << 8
a11y_state_modal = 1 << 9
a11y_state_expandable = 1 << 10
a11y_state_selectable = 1 << 11
a11y_state_multi_selectable = 1 << 12

a11y_event_focus_changed = 0
a11y_event_state_changed = 1
a11y_event_text_changed = 2
a11y_event_selection_changed = 3
a11y_event_value_changed = 4
a11y_event_name_changed = 5
a11y_event_structure_changed = 6

a11y_status_disabled = 0
a11y_status_unavailable = 1
a11y_status_active = 2

# Bit flags advertised by a drag source; combined in an actions mask.
drag_action_none = 0
drag_action_copy = 1
drag_action_move = 2
drag_action_ask = 4

# States delivered to a streaming read of a dropped MIME type.
drop_stream_data = 0
drop_stream_done = 1
drop_stream_cancelled = 2
drop_stream_error = 3

_widget_box = 0
_widget_label = 1
_widget_button = 2
_widget_entry = 3
_widget_toggle = 4
_widget_slider = 5
_widget_scroll = 6
_widget_divider = 7
_widget_card = 8
_widget_spacer = 9
_widget_checkbox = 10
_widget_radio = 11
_widget_progress = 12
_widget_spinner = 13
_widget_segmented = 14
_widget_text_area = 15
_widget_wrap = 16
_widget_grid = 17
_widget_list = 18
_widget_list_row = 19
_widget_icon = 20
_widget_table = 21
_widget_table_header = 22
_widget_table_header_cell = 23
_widget_table_row = 24
_widget_table_cell = 25
_widget_tree = 26
_widget_tree_row = 27
_widget_tabs = 28
_widget_tab_page = 29
_widget_sidebar = 30
_widget_sidebar_item = 31


def rgb(value: int) -> int:
    """Packs 0xRRGGBB into the 0xRRGGBBAA colors used by Pt."""
    return ((value & 0xFFFFFF) << 8) | 0xFF


def rgba(red: int, green: int, blue: int, alpha: int = 255) -> int:
    return ((red & 0xFF) << 24) | ((green & 0xFF) << 16) | ((blue & 0xFF) << 8) | (alpha & 0xFF)


def color_alpha(color: int, alpha: int) -> int:
    return (color & 0xFFFFFF00) | (alpha & 0xFF)


def _locate_library() -> str:
    override = _os.environ.get("PT_LIBRARY")
    if override:
        return override
    root = _pathlib.Path(__file__).resolve().parents[2]
    candidates = (
        root / "build" / "libpt.so",
        _pathlib.Path.cwd() / "build" / "libpt.so",
        _pathlib.Path("libpt.so"),
    )
    for candidate in candidates:
        try:
            if candidate.exists():
                return str(candidate)
        except OSError:
            continue
    return "libpt.so"


_lib = _ctypes.CDLL(_locate_library())

_CLICK = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_void_p)
_TEXT = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_char_p, _ctypes.c_void_p)
_VALUE = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_float, _ctypes.c_void_p)
_INDEX = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_void_p)
_BIND = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_void_p
)
_CLOSE = _ctypes.CFUNCTYPE(_ctypes.c_bool, _ctypes.c_void_p, _ctypes.c_void_p)
_RESIZE = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_int, _ctypes.c_int, _ctypes.c_void_p
)
_KEY = _ctypes.CFUNCTYPE(
    _ctypes.c_bool, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_uint32, _ctypes.c_void_p
)
_TIMER = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_void_p)
_A11Y = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_void_p, _ctypes.c_int, _ctypes.c_void_p
)
_DIALOG = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_void_p)
_NULL_CLICK = _CLICK(0)


class GestureEvent(_ctypes.Structure):
    """One normalized gesture event delivered to a window callback."""

    _fields_ = [
        ("kind", _ctypes.c_int),
        ("phase", _ctypes.c_int),
        ("fingers", _ctypes.c_uint32),
        ("x", _ctypes.c_float),
        ("y", _ctypes.c_float),
        ("dx", _ctypes.c_float),
        ("dy", _ctypes.c_float),
        ("scale", _ctypes.c_float),
        ("rotation", _ctypes.c_float),
        ("target", _ctypes.c_void_p),
    ]

    @property
    def target_widget(self) -> Optional["Widget"]:
        return Widget._wrap(self.target) if self.target else None


_GESTURE = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.POINTER(GestureEvent), _ctypes.c_void_p)


class StylusEvent(_ctypes.Structure):
    """One tablet stylus event delivered to a window callback."""

    _fields_ = [
        ("phase", _ctypes.c_int),
        ("tool", _ctypes.c_int),
        ("x", _ctypes.c_float),
        ("y", _ctypes.c_float),
        ("pressure", _ctypes.c_float),
        ("distance", _ctypes.c_float),
        ("tilt_x", _ctypes.c_float),
        ("tilt_y", _ctypes.c_float),
        ("rotation", _ctypes.c_float),
    ]

    @property
    def position(self) -> tuple[float, float]:
        return (float(self.x), float(self.y))


class TextSpan(_ctypes.Structure):
    """A styled byte range over a label or text-area string."""

    _fields_ = [
        ("start", _ctypes.c_uint32),
        ("length", _ctypes.c_uint32),
        ("weight", _ctypes.c_int),
        ("slant", _ctypes.c_int),
        ("color", _ctypes.c_uint32),
        ("flags", _ctypes.c_uint32),
        ("family", _ctypes.c_char * 64),
        ("size", _ctypes.c_float),
    ]


class DragData(_ctypes.Structure):
    """One MIME type and payload offered by a structured drag.

    A size of zero means the payload is NUL-terminated text. Pass bytes for
    payloads that may contain embedded NUL bytes and set an explicit size.
    """

    _fields_ = [
        ("mime_type", _ctypes.c_char_p),
        ("data", _ctypes.c_char_p),
        ("size", _ctypes.c_size_t),
    ]


_STYLUS = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.POINTER(StylusEvent), _ctypes.c_void_p
)
_BIND_CELL = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_void_p,
    _ctypes.c_uint32,
    _ctypes.c_uint32,
    _ctypes.c_void_p,
)
_SORT = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_int, _ctypes.c_void_p
)
_ROW_KEY = _ctypes.CFUNCTYPE(
    _ctypes.c_uint64, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_void_p
)
_ROW_TEXT = _ctypes.CFUNCTYPE(
    _ctypes.c_char_p, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_void_p
)
_SORT_COLUMNS = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_uint32,
    _ctypes.POINTER(_ctypes.c_uint32),
    _ctypes.POINTER(_ctypes.c_int),
    _ctypes.c_void_p,
)
_TREE_NODE = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_void_p, _ctypes.c_void_p
)
_TABLE_EDITED = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_uint32,
    _ctypes.c_uint32,
    _ctypes.c_char_p,
    _ctypes.c_void_p,
)
_TABLE_REORDERED = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_uint32, _ctypes.c_void_p
)
_TABLE_ROW_DRAG = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_uint32, _ctypes.c_void_p
)
_TREE_RENAME = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_void_p,
    _ctypes.c_char_p,
    _ctypes.c_void_p,
)
_COMBO_TEXT = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_char_p, _ctypes.c_void_p
)
_DROP_ENTER = _ctypes.CFUNCTYPE(
    _ctypes.c_bool, _ctypes.c_void_p, _ctypes.c_char_p, _ctypes.c_void_p
)
_DROP_MOTION = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_float, _ctypes.c_float, _ctypes.c_void_p
)
_DROP_LEAVE = _ctypes.CFUNCTYPE(None, _ctypes.c_void_p, _ctypes.c_void_p)
_DROP_DATA = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_char_p,
    _ctypes.c_char_p,
    _ctypes.c_float,
    _ctypes.c_float,
    _ctypes.c_void_p,
)
_DROP_OFFER_ENTER = _ctypes.CFUNCTYPE(
    _ctypes.c_char_p, _ctypes.c_void_p, _ctypes.c_void_p, _ctypes.c_void_p
)
_DROP_OFFER_DROP = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_void_p,
    _ctypes.c_char_p,
    _ctypes.c_float,
    _ctypes.c_float,
    _ctypes.c_void_p,
)
_DROP_STREAM = _ctypes.CFUNCTYPE(
    None,
    _ctypes.c_void_p,
    _ctypes.c_void_p,
    _ctypes.c_int,
    _ctypes.c_void_p,
    _ctypes.c_size_t,
    _ctypes.c_void_p,
)
_DRAG_FINISHED = _ctypes.CFUNCTYPE(
    None, _ctypes.c_void_p, _ctypes.c_bool, _ctypes.c_int, _ctypes.c_void_p
)
# Unregistered callbacks must be passed as real NULL function pointers, which
# is what the C side checks for; ctypes rejects None for these slots.
_NULL_DROP_MOTION = _DROP_MOTION(0)
_NULL_DROP_LEAVE = _DROP_LEAVE(0)
_NULL_DROP_DATA = _DROP_DATA(0)
_NULL_DROP_OFFER_ENTER = _DROP_OFFER_ENTER(0)
_NULL_DROP_OFFER_DROP = _DROP_OFFER_DROP(0)
_NULL_DRAG_FINISHED = _DRAG_FINISHED(0)

class _DropTarget(_ctypes.Structure):
    _fields_ = [
        ("enter", _DROP_ENTER),
        ("motion", _DROP_MOTION),
        ("leave", _DROP_LEAVE),
        ("drop", _DROP_DATA),
        ("user_data", _ctypes.c_void_p),
        ("enter_offer", _DROP_OFFER_ENTER),
        ("drop_offer", _DROP_OFFER_DROP),
    ]

_c_void_p = _ctypes.c_void_p
_c_char_p = _ctypes.c_char_p
_c_int = _ctypes.c_int
_c_uint32 = _ctypes.c_uint32
_c_uint64 = _ctypes.c_uint64
_c_float = _ctypes.c_float
_c_bool = _ctypes.c_bool
_c_double = _ctypes.c_double
_c_size_t = _ctypes.c_size_t


class _TransitionState(_ctypes.Structure):
    _fields_ = [
        ("value", _c_float),
        ("start", _c_float),
        ("target", _c_float),
        ("elapsed", _c_float),
        ("duration", _c_float),
        ("easing", _c_int),
        ("active", _c_bool),
    ]


def _proto(name: str, restype: Any, *argtypes: Any) -> Any:
    function = getattr(_lib, name)
    function.restype = restype
    function.argtypes = list(argtypes)
    return function


def _string(text: str) -> bytes:
    return text.encode("utf-8")


def _text_out(value: Optional[bytes]) -> str:
    return value.decode("utf-8") if value else ""


def _text_or_none(value: Optional[bytes]) -> Optional[str]:
    return value.decode("utf-8") if value else None


def _bytes_at(pointer: Optional[int], size: int) -> bytes:
    """Copies a length-delimited C buffer that need not be NUL terminated."""
    if not pointer or size <= 0:
        return b""
    return _ctypes.string_at(int(pointer), int(size))


# Application
_pt_app_create = _proto("pt_app_create", _c_void_p, _c_char_p)
_pt_app_destroy = _proto("pt_app_destroy", None, _c_void_p)
_pt_app_run = _proto("pt_app_run", _c_int, _c_void_p)
_pt_app_run_for = _proto("pt_app_run_for", _c_int, _c_void_p, _c_uint32)
_pt_app_quit = _proto("pt_app_quit", None, _c_void_p)
_pt_app_is_running = _proto("pt_app_is_running", _c_bool, _c_void_p)
_pt_app_add_timeout = _proto("pt_app_add_timeout", _c_uint64, _c_void_p, _c_uint32, _TIMER, _c_void_p)
_pt_app_cancel_timeout = _proto("pt_app_cancel_timeout", None, _c_void_p, _c_uint64)
_pt_app_set_color_scheme = _proto("pt_app_set_color_scheme", None, _c_void_p, _c_int)
_pt_app_get_color_scheme = _proto("pt_app_get_color_scheme", _c_int, _c_void_p)
_pt_app_set_accent_color = _proto("pt_app_set_accent_color", None, _c_void_p, _c_uint32)
_pt_app_set_theme_color = _proto("pt_app_set_theme_color", None, _c_void_p, _c_int, _c_uint32)
_pt_app_get_theme_color = _proto("pt_app_get_theme_color", _c_uint32, _c_void_p, _c_int)
_pt_app_set_theme_metric = _proto(
    "pt_app_set_theme_metric", None, _c_void_p, _c_int, _c_float
)
_pt_app_get_theme_metric = _proto(
    "pt_app_get_theme_metric", _c_float, _c_void_p, _c_int
)
_pt_app_set_font_family = _proto("pt_app_set_font_family", None, _c_void_p, _c_char_p)
_pt_app_set_reduced_motion = _proto(
    "pt_app_set_reduced_motion", None, _c_void_p, _c_bool
)
_pt_app_get_reduced_motion = _proto("pt_app_get_reduced_motion", _c_bool, _c_void_p)
_pt_app_set_quit_on_last_window_close = _proto(
    "pt_app_set_quit_on_last_window_close", None, _c_void_p, _c_bool
)
_pt_app_set_a11y_callback = _proto(
    "pt_app_set_a11y_callback", None, _c_void_p, _A11Y, _c_void_p
)
_pt_app_enable_a11y = _proto("pt_app_enable_a11y", _c_bool, _c_void_p)
_pt_app_disable_a11y = _proto("pt_app_disable_a11y", None, _c_void_p)
_pt_app_get_a11y_status = _proto("pt_app_get_a11y_status", _c_int, _c_void_p)
_pt_get_last_error = _proto("pt_get_last_error", _c_char_p)
_pt_free = _proto("pt_free", None, _c_void_p)

# Windows
_pt_window_create = _proto("pt_window_create", _c_void_p, _c_void_p, _c_char_p, _c_int, _c_int)
_pt_window_show = _proto("pt_window_show", None, _c_void_p)
_pt_window_hide = _proto("pt_window_hide", None, _c_void_p)
_pt_window_close = _proto("pt_window_close", None, _c_void_p)
_pt_window_is_open = _proto("pt_window_is_open", _c_bool, _c_void_p)
_pt_window_root = _proto("pt_window_root", _c_void_p, _c_void_p)
_pt_window_app = _proto("pt_window_app", _c_void_p, _c_void_p)
_pt_window_get_width = _proto("pt_window_get_width", _c_int, _c_void_p)
_pt_window_get_height = _proto("pt_window_get_height", _c_int, _c_void_p)
_pt_window_get_scale = _proto("pt_window_get_scale", _c_float, _c_void_p)
_pt_window_set_title = _proto("pt_window_set_title", None, _c_void_p, _c_char_p)
_pt_window_set_size = _proto("pt_window_set_size", None, _c_void_p, _c_int, _c_int)
_pt_window_set_min_size = _proto("pt_window_set_min_size", None, _c_void_p, _c_int, _c_int)
_pt_window_set_resizable = _proto("pt_window_set_resizable", None, _c_void_p, _c_bool)
_pt_window_set_decorated = _proto("pt_window_set_decorated", None, _c_void_p, _c_bool)
_pt_window_request_redraw = _proto("pt_window_request_redraw", None, _c_void_p)
_pt_window_focus = _proto("pt_window_focus", None, _c_void_p, _c_void_p)
_pt_window_set_theme_color = _proto(
    "pt_window_set_theme_color", None, _c_void_p, _c_int, _c_uint32
)
_pt_window_get_theme_color = _proto(
    "pt_window_get_theme_color", _c_uint32, _c_void_p, _c_int
)
_pt_window_set_theme_metric = _proto(
    "pt_window_set_theme_metric", None, _c_void_p, _c_int, _c_float
)
_pt_window_get_theme_metric = _proto(
    "pt_window_get_theme_metric", _c_float, _c_void_p, _c_int
)
_pt_window_clear_theme_overrides = _proto(
    "pt_window_clear_theme_overrides", None, _c_void_p
)
_pt_window_set_close_callback = _proto(
    "pt_window_set_close_callback", None, _c_void_p, _CLOSE, _c_void_p
)
_pt_window_set_resize_callback = _proto(
    "pt_window_set_resize_callback", None, _c_void_p, _RESIZE, _c_void_p
)
_pt_window_set_key_callback = _proto(
    "pt_window_set_key_callback", None, _c_void_p, _KEY, _c_void_p
)
_pt_window_clipboard_set = _proto("pt_window_clipboard_set", _c_bool, _c_void_p, _c_char_p)
_pt_window_clipboard_get = _proto("pt_window_clipboard_get", _c_void_p, _c_void_p)
_pt_window_set_popup_policy = _proto(
    "pt_window_set_popup_policy", None, _c_void_p, _c_int
)
_pt_window_get_popup_policy = _proto("pt_window_get_popup_policy", _c_int, _c_void_p)
_pt_window_get_drag_active = _proto("pt_window_get_drag_active", _c_bool, _c_void_p)
_pt_window_cancel_drag = _proto("pt_window_cancel_drag", None, _c_void_p)
_pt_window_set_drag_timeout = _proto(
    "pt_window_set_drag_timeout", None, _c_void_p, _c_uint32
)
_pt_window_get_drag_timeout = _proto(
    "pt_window_get_drag_timeout", _c_uint32, _c_void_p
)

# Overlays
_pt_menu_create = _proto("pt_menu_create", _c_void_p, _c_void_p)
_pt_menu_destroy = _proto("pt_menu_destroy", None, _c_void_p)
_pt_menu_add_item = _proto(
    "pt_menu_add_item", None, _c_void_p, _c_char_p, _c_char_p, _CLICK, _c_void_p
)
_pt_menu_add_separator = _proto("pt_menu_add_separator", None, _c_void_p)
_pt_menu_show_at = _proto("pt_menu_show_at", None, _c_void_p, _c_float, _c_float)
_pt_menu_show_below = _proto("pt_menu_show_below", None, _c_void_p, _c_void_p)
_pt_menu_dismiss = _proto("pt_menu_dismiss", None, _c_void_p)
_pt_menu_is_open = _proto("pt_menu_is_open", _c_bool, _c_void_p)
_pt_dialog_create = _proto(
    "pt_dialog_create", _c_void_p, _c_void_p, _c_char_p, _c_char_p
)
_pt_dialog_destroy = _proto("pt_dialog_destroy", None, _c_void_p)
_pt_dialog_set_title = _proto("pt_dialog_set_title", None, _c_void_p, _c_char_p)
_pt_dialog_set_message = _proto("pt_dialog_set_message", None, _c_void_p, _c_char_p)
_pt_dialog_add_action = _proto(
    "pt_dialog_add_action", None, _c_void_p, _c_char_p, _c_bool, _CLICK, _c_void_p
)
_pt_dialog_show = _proto("pt_dialog_show", None, _c_void_p)
_pt_dialog_close = _proto("pt_dialog_close", None, _c_void_p)
_pt_dialog_is_open = _proto("pt_dialog_is_open", _c_bool, _c_void_p)
_pt_dialog_set_on_close = _proto(
    "pt_dialog_set_on_close", None, _c_void_p, _DIALOG, _c_void_p
)
_pt_window_show_toast = _proto(
    "pt_window_show_toast", _c_void_p, _c_void_p, _c_char_p, _c_int, _c_uint32
)
_pt_toast_dismiss = _proto("pt_toast_dismiss", None, _c_void_p)
_pt_toast_is_visible = _proto("pt_toast_is_visible", _c_bool, _c_void_p)

# Accessibility
_pt_widget_set_tooltip = _proto("pt_widget_set_tooltip", None, _c_void_p, _c_char_p)
_pt_widget_set_tooltip_delay = _proto(
    "pt_widget_set_tooltip_delay", None, _c_void_p, _c_uint32
)
_pt_widget_get_a11y_role = _proto("pt_widget_get_a11y_role", _c_int, _c_void_p)
_pt_widget_set_a11y_role = _proto("pt_widget_set_a11y_role", None, _c_void_p, _c_int)
_pt_widget_get_a11y_name = _proto("pt_widget_get_a11y_name", _c_char_p, _c_void_p)
_pt_widget_set_a11y_name = _proto("pt_widget_set_a11y_name", None, _c_void_p, _c_char_p)
_pt_widget_get_a11y_description = _proto(
    "pt_widget_get_a11y_description", _c_char_p, _c_void_p
)
_pt_widget_set_a11y_description = _proto(
    "pt_widget_set_a11y_description", None, _c_void_p, _c_char_p
)
_pt_widget_get_a11y_state = _proto("pt_widget_get_a11y_state", _c_uint32, _c_void_p)
_pt_widget_get_a11y_text = _proto(
    "pt_widget_get_a11y_text",
    _c_bool,
    _c_void_p,
    _ctypes.POINTER(_c_char_p),
    _ctypes.POINTER(_c_uint32),
    _ctypes.POINTER(_c_uint32),
    _ctypes.POINTER(_c_uint32),
)
_pt_widget_get_a11y_value = _proto(
    "pt_widget_get_a11y_value", _c_bool, _c_void_p, _ctypes.POINTER(_c_float)
)

# Widgets
_pt_box_create = _proto("pt_box_create", _c_void_p, _c_void_p, _c_int)
_pt_row_create = _proto("pt_row_create", _c_void_p, _c_void_p)
_pt_column_create = _proto("pt_column_create", _c_void_p, _c_void_p)
_pt_label_create = _proto("pt_label_create", _c_void_p, _c_void_p, _c_char_p)
_pt_button_create = _proto("pt_button_create", _c_void_p, _c_void_p, _c_char_p)
_pt_entry_create = _proto("pt_entry_create", _c_void_p, _c_void_p, _c_char_p)
_pt_toggle_create = _proto("pt_toggle_create", _c_void_p, _c_void_p)
_pt_checkbox_create = _proto("pt_checkbox_create", _c_void_p, _c_void_p, _c_char_p)
_pt_radio_create = _proto("pt_radio_create", _c_void_p, _c_void_p, _c_char_p)
_pt_slider_create = _proto("pt_slider_create", _c_void_p, _c_void_p)
_pt_progress_create = _proto("pt_progress_create", _c_void_p, _c_void_p)
_pt_spinner_create = _proto("pt_spinner_create", _c_void_p, _c_void_p)
_pt_segmented_create = _proto(
    "pt_segmented_create", _c_void_p, _c_void_p, _ctypes.POINTER(_c_char_p), _c_uint32
)
_pt_scroll_create = _proto("pt_scroll_create", _c_void_p, _c_void_p)
_pt_divider_create = _proto("pt_divider_create", _c_void_p, _c_void_p)
_pt_card_create = _proto("pt_card_create", _c_void_p, _c_void_p)
_pt_spacer_create = _proto("pt_spacer_create", _c_void_p, _c_void_p)
_pt_chip_create = _proto("pt_chip_create", _c_void_p, _c_void_p, _c_char_p)
_pt_skeleton_create = _proto("pt_skeleton_create", _c_void_p, _c_void_p)
_pt_widget_destroy = _proto("pt_widget_destroy", None, _c_void_p)
_pt_widget_clear_children = _proto("pt_widget_clear_children", None, _c_void_p)
_pt_widget_parent = _proto("pt_widget_parent", _c_void_p, _c_void_p)
_pt_widget_get_kind = _proto("pt_widget_get_kind", _c_int, _c_void_p)
_pt_widget_set_width = _proto("pt_widget_set_width", None, _c_void_p, _c_float)
_pt_widget_set_height = _proto("pt_widget_set_height", None, _c_void_p, _c_float)
_pt_widget_set_size = _proto("pt_widget_set_size", None, _c_void_p, _c_float, _c_float)
_pt_widget_set_min_width = _proto("pt_widget_set_min_width", None, _c_void_p, _c_float)
_pt_widget_set_min_height = _proto("pt_widget_set_min_height", None, _c_void_p, _c_float)
_pt_widget_set_max_width = _proto("pt_widget_set_max_width", None, _c_void_p, _c_float)
_pt_widget_set_max_height = _proto("pt_widget_set_max_height", None, _c_void_p, _c_float)
_pt_widget_set_margin = _proto(
    "pt_widget_set_margin", None, _c_void_p, _c_float, _c_float, _c_float, _c_float
)
_pt_widget_set_padding = _proto(
    "pt_widget_set_padding", None, _c_void_p, _c_float, _c_float, _c_float, _c_float
)
_pt_widget_set_gap = _proto("pt_widget_set_gap", None, _c_void_p, _c_float)
_pt_widget_set_grow = _proto("pt_widget_set_grow", None, _c_void_p, _c_float)
_pt_widget_set_align_self = _proto("pt_widget_set_align_self", None, _c_void_p, _c_int)
_pt_box_set_direction = _proto("pt_box_set_direction", None, _c_void_p, _c_int)
_pt_box_set_gap = _proto("pt_box_set_gap", None, _c_void_p, _c_float)
_pt_box_set_align = _proto("pt_box_set_align", None, _c_void_p, _c_int)
_pt_box_set_justify = _proto("pt_box_set_justify", None, _c_void_p, _c_int)
_pt_widget_set_background = _proto("pt_widget_set_background", None, _c_void_p, _c_uint32)
_pt_widget_set_foreground = _proto("pt_widget_set_foreground", None, _c_void_p, _c_uint32)
_pt_widget_set_border_color = _proto("pt_widget_set_border_color", None, _c_void_p, _c_uint32)
_pt_widget_set_border_width = _proto("pt_widget_set_border_width", None, _c_void_p, _c_float)
_pt_widget_set_radius = _proto("pt_widget_set_radius", None, _c_void_p, _c_float)
_pt_widget_set_font_size = _proto("pt_widget_set_font_size", None, _c_void_p, _c_float)
_pt_widget_set_font_weight = _proto("pt_widget_set_font_weight", None, _c_void_p, _c_int)
_pt_widget_set_text_align = _proto("pt_widget_set_text_align", None, _c_void_p, _c_int)
_pt_widget_set_visible = _proto("pt_widget_set_visible", None, _c_void_p, _c_bool)
_pt_widget_set_enabled = _proto("pt_widget_set_enabled", None, _c_void_p, _c_bool)
_pt_widget_set_busy = _proto("pt_widget_set_busy", None, _c_void_p, _c_bool)
_pt_widget_get_busy = _proto("pt_widget_get_busy", _c_bool, _c_void_p)
_pt_widget_set_invalid = _proto("pt_widget_set_invalid", None, _c_void_p, _c_bool)
_pt_widget_get_invalid = _proto("pt_widget_get_invalid", _c_bool, _c_void_p)
_pt_widget_set_label_wrap = _proto("pt_widget_set_label_wrap", None, _c_void_p, _c_bool)
_pt_widget_set_text = _proto("pt_widget_set_text", None, _c_void_p, _c_char_p)
_pt_widget_get_text = _proto("pt_widget_get_text", _c_char_p, _c_void_p)
_pt_widget_set_text_spans = _proto(
    "pt_widget_set_text_spans",
    _c_bool,
    _c_void_p,
    _ctypes.POINTER(TextSpan),
    _c_uint32,
)
_pt_widget_clear_text_spans = _proto("pt_widget_clear_text_spans", None, _c_void_p)
_pt_widget_get_text_span_count = _proto(
    "pt_widget_get_text_span_count", _c_uint32, _c_void_p
)
_pt_widget_get_text_span = _proto(
    "pt_widget_get_text_span",
    _c_bool,
    _c_void_p,
    _c_uint32,
    _ctypes.POINTER(TextSpan),
)
_pt_widget_set_input_purpose = _proto(
    "pt_widget_set_input_purpose", None, _c_void_p, _c_int
)
_pt_widget_get_input_purpose = _proto("pt_widget_get_input_purpose", _c_int, _c_void_p)
_pt_button_set_variant = _proto("pt_button_set_variant", None, _c_void_p, _c_int)
_pt_button_set_on_click = _proto("pt_button_set_on_click", None, _c_void_p, _CLICK, _c_void_p)
_pt_chip_set_icon = _proto("pt_chip_set_icon", None, _c_void_p, _c_int)
_pt_chip_set_removable = _proto("pt_chip_set_removable", None, _c_void_p, _c_bool)
_pt_chip_get_removable = _proto("pt_chip_get_removable", _c_bool, _c_void_p)
_pt_chip_set_selected = _proto("pt_chip_set_selected", None, _c_void_p, _c_bool)
_pt_chip_get_selected = _proto("pt_chip_get_selected", _c_bool, _c_void_p)
_pt_chip_set_on_click = _proto("pt_chip_set_on_click", None, _c_void_p, _CLICK, _c_void_p)
_pt_chip_set_on_remove = _proto("pt_chip_set_on_remove", None, _c_void_p, _CLICK, _c_void_p)
_pt_entry_set_placeholder = _proto("pt_entry_set_placeholder", None, _c_void_p, _c_char_p)
_pt_entry_set_password = _proto("pt_entry_set_password", None, _c_void_p, _c_bool)
_pt_entry_set_on_change = _proto("pt_entry_set_on_change", None, _c_void_p, _TEXT, _c_void_p)
_pt_entry_set_on_submit = _proto("pt_entry_set_on_submit", None, _c_void_p, _CLICK, _c_void_p)
_pt_entry_select_all = _proto("pt_entry_select_all", None, _c_void_p)
_pt_entry_set_selection = _proto("pt_entry_set_selection", None, _c_void_p, _c_uint32, _c_uint32)
_pt_entry_get_selection = _proto(
    "pt_entry_get_selection",
    _c_bool,
    _c_void_p,
    _ctypes.POINTER(_c_uint32),
    _ctypes.POINTER(_c_uint32),
)
_pt_entry_undo = _proto("pt_entry_undo", _c_bool, _c_void_p)
_pt_entry_redo = _proto("pt_entry_redo", _c_bool, _c_void_p)
_pt_widget_focus = _proto("pt_widget_focus", _c_bool, _c_void_p)
_pt_widget_has_focus = _proto("pt_widget_has_focus", _c_bool, _c_void_p)
_pt_toggle_get_checked = _proto("pt_toggle_get_checked", _c_bool, _c_void_p)
_pt_toggle_set_checked = _proto("pt_toggle_set_checked", None, _c_void_p, _c_bool)
_pt_toggle_set_on_change = _proto("pt_toggle_set_on_change", None, _c_void_p, _VALUE, _c_void_p)
_pt_widget_get_checked = _proto("pt_widget_get_checked", _c_bool, _c_void_p)
_pt_widget_set_checked = _proto("pt_widget_set_checked", None, _c_void_p, _c_bool)
_pt_widget_set_on_checked = _proto(
    "pt_widget_set_on_checked", None, _c_void_p, _VALUE, _c_void_p
)
_pt_slider_get_value = _proto("pt_slider_get_value", _c_float, _c_void_p)
_pt_slider_set_value = _proto("pt_slider_set_value", None, _c_void_p, _c_float)
_pt_slider_set_on_change = _proto("pt_slider_set_on_change", None, _c_void_p, _VALUE, _c_void_p)
_pt_progress_get_value = _proto("pt_progress_get_value", _c_float, _c_void_p)
_pt_progress_set_value = _proto("pt_progress_set_value", None, _c_void_p, _c_float)
_pt_progress_get_indeterminate = _proto(
    "pt_progress_get_indeterminate", _c_bool, _c_void_p
)
_pt_progress_set_indeterminate = _proto(
    "pt_progress_set_indeterminate", None, _c_void_p, _c_bool
)
_pt_segmented_get_selected = _proto("pt_segmented_get_selected", _c_uint32, _c_void_p)
_pt_segmented_set_selected = _proto("pt_segmented_set_selected", None, _c_void_p, _c_uint32)
_pt_segmented_set_on_change = _proto(
    "pt_segmented_set_on_change", None, _c_void_p, _INDEX, _c_void_p
)
_pt_scroll_set_offset = _proto("pt_scroll_set_offset", None, _c_void_p, _c_float)
_pt_scroll_get_offset = _proto("pt_scroll_get_offset", _c_float, _c_void_p)
_pt_scroll_get_max_offset = _proto("pt_scroll_get_max_offset", _c_float, _c_void_p)

# Text areas, responsive containers and virtual lists
_pt_text_area_create = _proto("pt_text_area_create", _c_void_p, _c_void_p, _c_char_p)
_pt_text_area_set_placeholder = _proto(
    "pt_text_area_set_placeholder", None, _c_void_p, _c_char_p
)
_pt_text_area_set_on_change = _proto(
    "pt_text_area_set_on_change", None, _c_void_p, _TEXT, _c_void_p
)
_pt_text_area_set_grow_to_content = _proto(
    "pt_text_area_set_grow_to_content", None, _c_void_p, _c_bool
)
_pt_text_area_get_grow_to_content = _proto(
    "pt_text_area_get_grow_to_content", _c_bool, _c_void_p
)
_pt_text_area_scroll_to_caret = _proto(
    "pt_text_area_scroll_to_caret", None, _c_void_p
)
_pt_text_area_select_all = _proto("pt_text_area_select_all", None, _c_void_p)
_pt_text_area_set_selection = _proto(
    "pt_text_area_set_selection", None, _c_void_p, _c_uint32, _c_uint32
)
_pt_text_area_get_selection = _proto(
    "pt_text_area_get_selection",
    _c_bool,
    _c_void_p,
    _ctypes.POINTER(_c_uint32),
    _ctypes.POINTER(_c_uint32),
)
_pt_text_area_undo = _proto("pt_text_area_undo", _c_bool, _c_void_p)
_pt_text_area_redo = _proto("pt_text_area_redo", _c_bool, _c_void_p)
_pt_wrap_create = _proto("pt_wrap_create", _c_void_p, _c_void_p)
_pt_wrap_set_gap = _proto("pt_wrap_set_gap", None, _c_void_p, _c_float, _c_float)
_pt_grid_create = _proto("pt_grid_create", _c_void_p, _c_void_p, _c_uint32)
_pt_grid_set_columns = _proto("pt_grid_set_columns", None, _c_void_p, _c_uint32)
_pt_grid_get_columns = _proto("pt_grid_get_columns", _c_uint32, _c_void_p)
_pt_grid_set_gap = _proto("pt_grid_set_gap", None, _c_void_p, _c_float, _c_float)
_pt_widget_set_grid_span = _proto(
    "pt_widget_set_grid_span", None, _c_void_p, _c_uint32
)
_pt_widget_get_grid_span = _proto("pt_widget_get_grid_span", _c_uint32, _c_void_p)
_pt_list_view_create = _proto("pt_list_view_create", _c_void_p, _c_void_p)
_pt_list_view_set_row_count = _proto(
    "pt_list_view_set_row_count", None, _c_void_p, _c_uint32
)
_pt_list_view_get_row_count = _proto("pt_list_view_get_row_count", _c_uint32, _c_void_p)
_pt_list_view_set_row_height = _proto(
    "pt_list_view_set_row_height", None, _c_void_p, _c_float
)
_pt_list_view_get_row_height = _proto(
    "pt_list_view_get_row_height", _c_float, _c_void_p
)
_pt_list_view_set_on_bind_row = _proto(
    "pt_list_view_set_on_bind_row", None, _c_void_p, _BIND, _c_void_p
)
_pt_list_view_set_on_select = _proto(
    "pt_list_view_set_on_select", None, _c_void_p, _INDEX, _c_void_p
)
_pt_list_view_set_on_activate = _proto(
    "pt_list_view_set_on_activate", None, _c_void_p, _INDEX, _c_void_p
)
_pt_list_view_get_selected = _proto("pt_list_view_get_selected", _c_uint32, _c_void_p)
_pt_list_view_set_selected = _proto(
    "pt_list_view_set_selected", None, _c_void_p, _c_uint32
)
_pt_list_view_refresh = _proto("pt_list_view_refresh", None, _c_void_p)
_pt_list_view_refresh_rows = _proto(
    "pt_list_view_refresh_rows", None, _c_void_p, _c_uint32, _c_uint32
)
_pt_list_view_scroll_to_row = _proto(
    "pt_list_view_scroll_to_row", None, _c_void_p, _c_uint32
)
_pt_list_view_set_row_key_callback = _proto(
    "pt_list_view_set_row_key_callback", None, _c_void_p, _ROW_KEY, _c_void_p
)
_pt_list_view_get_selected_key = _proto(
    "pt_list_view_get_selected_key", _c_uint64, _c_void_p
)
_pt_list_view_set_selected_key = _proto(
    "pt_list_view_set_selected_key", _c_bool, _c_void_p, _c_uint64
)
_pt_list_view_set_row_text_callback = _proto(
    "pt_list_view_set_row_text_callback", None, _c_void_p, _ROW_TEXT, _c_void_p
)
_pt_list_view_get_selection_count = _proto(
    "pt_list_view_get_selection_count", _c_uint32, _c_void_p
)
_pt_list_view_is_row_selected = _proto(
    "pt_list_view_is_row_selected", _c_bool, _c_void_p, _c_uint32
)
_pt_list_view_get_selection = _proto(
    "pt_list_view_get_selection",
    _c_uint32,
    _c_void_p,
    _ctypes.POINTER(_c_uint32),
    _c_uint32,
)
_pt_list_view_select_range = _proto(
    "pt_list_view_select_range", None, _c_void_p, _c_uint32, _c_uint32
)
_pt_list_view_select_toggle = _proto(
    "pt_list_view_select_toggle", None, _c_void_p, _c_uint32
)
_pt_list_view_clear_selection = _proto(
    "pt_list_view_clear_selection", None, _c_void_p
)

# Icons, tables, trees, tabs and sidebars
_pt_icon_create = _proto("pt_icon_create", _c_void_p, _c_void_p, _c_int)
_pt_icon_button_create = _proto(
    "pt_icon_button_create", _c_void_p, _c_void_p, _c_int, _c_char_p
)
_pt_widget_set_icon = _proto("pt_widget_set_icon", None, _c_void_p, _c_int)
_pt_widget_get_icon = _proto("pt_widget_get_icon", _c_int, _c_void_p)
_pt_icon_set_size = _proto("pt_icon_set_size", None, _c_void_p, _c_float)
_pt_icon_get_size = _proto("pt_icon_get_size", _c_float, _c_void_p)

_pt_table_create = _proto("pt_table_create", _c_void_p, _c_void_p)
_pt_table_set_column_count = _proto(
    "pt_table_set_column_count", None, _c_void_p, _c_uint32
)
_pt_table_get_column_count = _proto("pt_table_get_column_count", _c_uint32, _c_void_p)
_pt_table_set_column_title = _proto(
    "pt_table_set_column_title", None, _c_void_p, _c_uint32, _c_char_p
)
_pt_table_set_column_width = _proto(
    "pt_table_set_column_width", None, _c_void_p, _c_uint32, _c_float
)
_pt_table_get_column_width = _proto(
    "pt_table_get_column_width", _c_float, _c_void_p, _c_uint32
)
_pt_table_set_column_align = _proto(
    "pt_table_set_column_align", None, _c_void_p, _c_uint32, _c_int
)
_pt_table_set_column_resizable = _proto(
    "pt_table_set_column_resizable", None, _c_void_p, _c_uint32, _c_bool
)
_pt_table_set_column_hidden = _proto(
    "pt_table_set_column_hidden", None, _c_void_p, _c_uint32, _c_bool
)
_pt_table_get_column_hidden = _proto(
    "pt_table_get_column_hidden", _c_bool, _c_void_p, _c_uint32
)
_pt_table_set_row_count = _proto(
    "pt_table_set_row_count", None, _c_void_p, _c_uint32
)
_pt_table_get_row_count = _proto("pt_table_get_row_count", _c_uint32, _c_void_p)
_pt_table_set_row_height = _proto(
    "pt_table_set_row_height", None, _c_void_p, _c_float
)
_pt_table_get_row_height = _proto("pt_table_get_row_height", _c_float, _c_void_p)
_pt_table_set_header_height = _proto(
    "pt_table_set_header_height", None, _c_void_p, _c_float
)
_pt_table_set_on_bind_cell = _proto(
    "pt_table_set_on_bind_cell", None, _c_void_p, _BIND_CELL, _c_void_p
)
_pt_table_set_on_select = _proto(
    "pt_table_set_on_select", None, _c_void_p, _INDEX, _c_void_p
)
_pt_table_set_on_activate = _proto(
    "pt_table_set_on_activate", None, _c_void_p, _INDEX, _c_void_p
)
_pt_table_set_on_sort = _proto(
    "pt_table_set_on_sort", None, _c_void_p, _SORT, _c_void_p
)
_pt_table_set_on_sort_columns = _proto(
    "pt_table_set_on_sort_columns", None, _c_void_p, _SORT_COLUMNS, _c_void_p
)
_pt_table_get_sort = _proto(
    "pt_table_get_sort", _c_int, _c_void_p, _ctypes.POINTER(_c_uint32)
)
_pt_table_set_sort = _proto(
    "pt_table_set_sort", None, _c_void_p, _c_uint32, _c_int
)
_pt_table_get_sort_columns = _proto(
    "pt_table_get_sort_columns",
    _c_uint32,
    _c_void_p,
    _ctypes.POINTER(_c_uint32),
    _ctypes.POINTER(_c_int),
)
_pt_table_set_sort_columns = _proto(
    "pt_table_set_sort_columns",
    None,
    _c_void_p,
    _c_uint32,
    _ctypes.POINTER(_c_uint32),
    _ctypes.POINTER(_c_int),
)
_pt_table_get_selected = _proto("pt_table_get_selected", _c_uint32, _c_void_p)
_pt_table_set_selected = _proto(
    "pt_table_set_selected", None, _c_void_p, _c_uint32
)
_pt_table_set_row_key_callback = _proto(
    "pt_table_set_row_key_callback", None, _c_void_p, _ROW_KEY, _c_void_p
)
_pt_table_get_selected_key = _proto(
    "pt_table_get_selected_key", _c_uint64, _c_void_p
)
_pt_table_set_selected_key = _proto(
    "pt_table_set_selected_key", _c_bool, _c_void_p, _c_uint64
)
_pt_table_set_row_text_callback = _proto(
    "pt_table_set_row_text_callback", None, _c_void_p, _ROW_TEXT, _c_void_p
)
_pt_table_refresh = _proto("pt_table_refresh", None, _c_void_p)
_pt_table_refresh_rows = _proto(
    "pt_table_refresh_rows", None, _c_void_p, _c_uint32, _c_uint32
)
_pt_table_scroll_to_row = _proto(
    "pt_table_scroll_to_row", None, _c_void_p, _c_uint32
)
_pt_table_scroll_to_column = _proto(
    "pt_table_scroll_to_column", None, _c_void_p, _c_uint32
)
_pt_table_set_editable = _proto("pt_table_set_editable", None, _c_void_p, _c_bool)
_pt_table_get_editable = _proto("pt_table_get_editable", _c_bool, _c_void_p)
_pt_table_begin_edit = _proto(
    "pt_table_begin_edit", _c_bool, _c_void_p, _c_uint32, _c_uint32
)
_pt_table_commit_edit = _proto("pt_table_commit_edit", _c_bool, _c_void_p)
_pt_table_cancel_edit = _proto("pt_table_cancel_edit", None, _c_void_p)
_pt_table_is_editing = _proto("pt_table_is_editing", _c_bool, _c_void_p)
_pt_table_set_on_cell_edited = _proto(
    "pt_table_set_on_cell_edited", None, _c_void_p, _TABLE_EDITED, _c_void_p
)
_pt_table_get_selection_count = _proto(
    "pt_table_get_selection_count", _c_uint32, _c_void_p
)
_pt_table_is_row_selected = _proto(
    "pt_table_is_row_selected", _c_bool, _c_void_p, _c_uint32
)
_pt_table_get_selection = _proto(
    "pt_table_get_selection",
    _c_uint32,
    _c_void_p,
    _ctypes.POINTER(_c_uint32),
    _c_uint32,
)
_pt_table_select_range = _proto(
    "pt_table_select_range", None, _c_void_p, _c_uint32, _c_uint32
)
_pt_table_select_toggle = _proto(
    "pt_table_select_toggle", None, _c_void_p, _c_uint32
)
_pt_table_clear_selection = _proto("pt_table_clear_selection", None, _c_void_p)
_pt_table_set_columns_reorderable = _proto(
    "pt_table_set_columns_reorderable", None, _c_void_p, _c_bool
)
_pt_table_get_columns_reorderable = _proto(
    "pt_table_get_columns_reorderable", _c_bool, _c_void_p
)
_pt_table_move_column = _proto(
    "pt_table_move_column", _c_bool, _c_void_p, _c_uint32, _c_uint32
)
_pt_table_set_on_columns_reordered = _proto(
    "pt_table_set_on_columns_reordered", None, _c_void_p, _TABLE_REORDERED, _c_void_p
)
_pt_table_set_on_row_drag = _proto(
    "pt_table_set_on_row_drag", None, _c_void_p, _TABLE_ROW_DRAG, _c_void_p
)

_pt_tree_create = _proto("pt_tree_create", _c_void_p, _c_void_p)
_pt_tree_add_node = _proto(
    "pt_tree_add_node", _c_void_p, _c_void_p, _c_void_p, _c_char_p
)
_pt_tree_clear = _proto("pt_tree_clear", None, _c_void_p)
_pt_tree_get_root_count = _proto("pt_tree_get_root_count", _c_uint32, _c_void_p)
_pt_tree_get_root_node = _proto(
    "pt_tree_get_root_node", _c_void_p, _c_void_p, _c_uint32
)
_pt_tree_node_get_text = _proto("pt_tree_node_get_text", _c_char_p, _c_void_p)
_pt_tree_node_set_text = _proto("pt_tree_node_set_text", None, _c_void_p, _c_char_p)
_pt_tree_node_get_expanded = _proto("pt_tree_node_get_expanded", _c_bool, _c_void_p)
_pt_tree_node_set_expanded = _proto(
    "pt_tree_node_set_expanded", None, _c_void_p, _c_bool
)
_pt_tree_node_has_children = _proto(
    "pt_tree_node_has_children", _c_bool, _c_void_p
)
_pt_tree_node_get_depth = _proto("pt_tree_node_get_depth", _c_uint32, _c_void_p)
_pt_tree_node_parent = _proto("pt_tree_node_parent", _c_void_p, _c_void_p)
_pt_tree_node_get_child_count = _proto(
    "pt_tree_node_get_child_count", _c_uint32, _c_void_p
)
_pt_tree_node_get_child = _proto(
    "pt_tree_node_get_child", _c_void_p, _c_void_p, _c_uint32
)
_pt_tree_node_get_data = _proto("pt_tree_node_get_data", _c_void_p, _c_void_p)
_pt_tree_node_set_data = _proto(
    "pt_tree_node_set_data", None, _c_void_p, _c_void_p
)
_pt_tree_get_visible_count = _proto(
    "pt_tree_get_visible_count", _c_uint32, _c_void_p
)
_pt_tree_get_visible_node = _proto(
    "pt_tree_get_visible_node", _c_void_p, _c_void_p, _c_uint32
)
_pt_tree_get_selected = _proto("pt_tree_get_selected", _c_void_p, _c_void_p)
_pt_tree_set_selected = _proto("pt_tree_set_selected", None, _c_void_p, _c_void_p)
_pt_tree_set_row_height = _proto(
    "pt_tree_set_row_height", None, _c_void_p, _c_float
)
_pt_tree_set_indent = _proto("pt_tree_set_indent", None, _c_void_p, _c_float)
_pt_tree_set_on_select = _proto(
    "pt_tree_set_on_select", None, _c_void_p, _TREE_NODE, _c_void_p
)
_pt_tree_set_on_activate = _proto(
    "pt_tree_set_on_activate", None, _c_void_p, _TREE_NODE, _c_void_p
)
_pt_tree_refresh = _proto("pt_tree_refresh", None, _c_void_p)
_pt_tree_scroll_to_node = _proto(
    "pt_tree_scroll_to_node", None, _c_void_p, _c_void_p
)
_pt_tree_set_renamable = _proto("pt_tree_set_renamable", None, _c_void_p, _c_bool)
_pt_tree_get_renamable = _proto("pt_tree_get_renamable", _c_bool, _c_void_p)
_pt_tree_begin_rename = _proto(
    "pt_tree_begin_rename", _c_bool, _c_void_p, _c_void_p
)
_pt_tree_commit_rename = _proto("pt_tree_commit_rename", _c_bool, _c_void_p)
_pt_tree_cancel_rename = _proto("pt_tree_cancel_rename", None, _c_void_p)
_pt_tree_is_renaming = _proto("pt_tree_is_renaming", _c_bool, _c_void_p)
_pt_tree_set_on_rename = _proto(
    "pt_tree_set_on_rename", None, _c_void_p, _TREE_RENAME, _c_void_p
)
_pt_tree_get_selection_count = _proto(
    "pt_tree_get_selection_count", _c_uint32, _c_void_p
)
_pt_tree_is_selected = _proto(
    "pt_tree_is_selected", _c_bool, _c_void_p, _c_void_p
)
_pt_tree_get_selection = _proto(
    "pt_tree_get_selection",
    _c_uint32,
    _c_void_p,
    _ctypes.POINTER(_c_void_p),
    _c_uint32,
)
_pt_tree_select_range = _proto(
    "pt_tree_select_range", None, _c_void_p, _c_uint32, _c_uint32
)
_pt_tree_select_toggle = _proto("pt_tree_select_toggle", None, _c_void_p, _c_void_p)
_pt_tree_clear_selection = _proto("pt_tree_clear_selection", None, _c_void_p)

# Combobox
_pt_combobox_create = _proto("pt_combobox_create", _c_void_p, _c_void_p)
_pt_combobox_add_item = _proto("pt_combobox_add_item", None, _c_void_p, _c_char_p)
_pt_combobox_insert_item = _proto(
    "pt_combobox_insert_item", None, _c_void_p, _c_uint32, _c_char_p
)
_pt_combobox_remove_item = _proto(
    "pt_combobox_remove_item", None, _c_void_p, _c_uint32
)
_pt_combobox_clear = _proto("pt_combobox_clear", None, _c_void_p)
_pt_combobox_get_item_count = _proto(
    "pt_combobox_get_item_count", _c_uint32, _c_void_p
)
_pt_combobox_get_item_text = _proto(
    "pt_combobox_get_item_text", _c_char_p, _c_void_p, _c_uint32
)
_pt_combobox_set_item_text = _proto(
    "pt_combobox_set_item_text", None, _c_void_p, _c_uint32, _c_char_p
)
_pt_combobox_set_item_data = _proto(
    "pt_combobox_set_item_data", None, _c_void_p, _c_uint32, _c_void_p
)
_pt_combobox_get_item_data = _proto(
    "pt_combobox_get_item_data", _c_void_p, _c_void_p, _c_uint32
)
_pt_combobox_set_item_disabled = _proto(
    "pt_combobox_set_item_disabled", None, _c_void_p, _c_uint32, _c_bool
)
_pt_combobox_get_item_disabled = _proto(
    "pt_combobox_get_item_disabled", _c_bool, _c_void_p, _c_uint32
)
_pt_combobox_set_mode = _proto("pt_combobox_set_mode", None, _c_void_p, _c_int)
_pt_combobox_get_mode = _proto("pt_combobox_get_mode", _c_int, _c_void_p)
_pt_combobox_get_selected = _proto("pt_combobox_get_selected", _c_uint32, _c_void_p)
_pt_combobox_set_selected = _proto(
    "pt_combobox_set_selected", None, _c_void_p, _c_uint32
)
_pt_combobox_get_text = _proto("pt_combobox_get_text", _c_char_p, _c_void_p)
_pt_combobox_set_text = _proto("pt_combobox_set_text", None, _c_void_p, _c_char_p)
_pt_combobox_get_placeholder = _proto(
    "pt_combobox_get_placeholder", _c_char_p, _c_void_p
)
_pt_combobox_set_placeholder = _proto(
    "pt_combobox_set_placeholder", None, _c_void_p, _c_char_p
)
_pt_combobox_open = _proto("pt_combobox_open", None, _c_void_p)
_pt_combobox_close = _proto("pt_combobox_close", None, _c_void_p)
_pt_combobox_is_open = _proto("pt_combobox_is_open", _c_bool, _c_void_p)
_pt_combobox_set_popup_max_height = _proto(
    "pt_combobox_set_popup_max_height", None, _c_void_p, _c_float
)
_pt_combobox_get_popup_max_height = _proto(
    "pt_combobox_get_popup_max_height", _c_float, _c_void_p
)
_pt_combobox_set_on_change = _proto(
    "pt_combobox_set_on_change", None, _c_void_p, _INDEX, _c_void_p
)
_pt_combobox_set_on_text_change = _proto(
    "pt_combobox_set_on_text_change", None, _c_void_p, _COMBO_TEXT, _c_void_p
)

_pt_tabs_create = _proto("pt_tabs_create", _c_void_p, _c_void_p)
_pt_tabs_add_page = _proto("pt_tabs_add_page", _c_void_p, _c_void_p, _c_char_p)
_pt_tabs_remove_page = _proto("pt_tabs_remove_page", None, _c_void_p, _c_uint32)
_pt_tabs_get_page_count = _proto("pt_tabs_get_page_count", _c_uint32, _c_void_p)
_pt_tabs_get_page = _proto("pt_tabs_get_page", _c_void_p, _c_void_p, _c_uint32)
_pt_tabs_set_page_title = _proto(
    "pt_tabs_set_page_title", None, _c_void_p, _c_uint32, _c_char_p
)
_pt_tabs_get_page_title = _proto(
    "pt_tabs_get_page_title", _c_char_p, _c_void_p, _c_uint32
)
_pt_tabs_get_active = _proto("pt_tabs_get_active", _c_uint32, _c_void_p)
_pt_tabs_set_active = _proto("pt_tabs_set_active", None, _c_void_p, _c_uint32)
_pt_tabs_set_on_change = _proto(
    "pt_tabs_set_on_change", None, _c_void_p, _INDEX, _c_void_p
)
_pt_tabs_set_strip_height = _proto(
    "pt_tabs_set_strip_height", None, _c_void_p, _c_float
)

_pt_sidebar_create = _proto("pt_sidebar_create", _c_void_p, _c_void_p)
_pt_sidebar_add_item = _proto(
    "pt_sidebar_add_item", _c_void_p, _c_void_p, _c_int, _c_char_p
)
_pt_sidebar_get_item_count = _proto(
    "pt_sidebar_get_item_count", _c_uint32, _c_void_p
)
_pt_sidebar_get_item = _proto(
    "pt_sidebar_get_item", _c_void_p, _c_void_p, _c_uint32
)
_pt_sidebar_get_active = _proto("pt_sidebar_get_active", _c_uint32, _c_void_p)
_pt_sidebar_set_active = _proto("pt_sidebar_set_active", None, _c_void_p, _c_uint32)
_pt_sidebar_get_compact = _proto("pt_sidebar_get_compact", _c_bool, _c_void_p)
_pt_sidebar_set_compact = _proto(
    "pt_sidebar_set_compact", None, _c_void_p, _c_bool
)
_pt_sidebar_set_header = _proto(
    "pt_sidebar_set_header", None, _c_void_p, _c_char_p
)
_pt_sidebar_get_header = _proto("pt_sidebar_get_header", _c_char_p, _c_void_p)
_pt_sidebar_set_on_select = _proto(
    "pt_sidebar_set_on_select", None, _c_void_p, _INDEX, _c_void_p
)

# Motion
_pt_ease = _proto("pt_ease", _c_float, _c_int, _c_float)
_pt_ease_value = _proto("pt_ease_value", _c_float, _c_int, _c_float, _c_float, _c_float)
_pt_transition_init = _proto(
    "pt_transition_init", None, _ctypes.POINTER(_TransitionState), _c_float
)
_pt_transition_to = _proto(
    "pt_transition_to",
    None,
    _ctypes.POINTER(_TransitionState),
    _c_float,
    _c_uint32,
    _c_int,
)
_pt_transition_advance = _proto(
    "pt_transition_advance", _c_bool, _ctypes.POINTER(_TransitionState), _c_float
)
_pt_transition_set = _proto(
    "pt_transition_set", None, _ctypes.POINTER(_TransitionState), _c_float
)
_pt_transition_active = _proto(
    "pt_transition_active", _c_bool, _ctypes.POINTER(_TransitionState)
)
_pt_widget_get_bounds = _proto(
    "pt_widget_get_bounds",
    None,
    _c_void_p,
    _ctypes.POINTER(_c_float),
    _ctypes.POINTER(_c_float),
    _ctypes.POINTER(_c_float),
    _ctypes.POINTER(_c_float),
)

# Interaction and drag and drop
_pt_widget_set_drop_target = _proto(
    "pt_widget_set_drop_target", None, _c_void_p, _ctypes.POINTER(_DropTarget)
)
_pt_widget_get_drop_hover = _proto("pt_widget_get_drop_hover", _c_bool, _c_void_p)
_pt_entry_set_drag_enabled = _proto(
    "pt_entry_set_drag_enabled", None, _c_void_p, _c_bool
)
_pt_entry_get_drag_enabled = _proto("pt_entry_get_drag_enabled", _c_bool, _c_void_p)
_pt_window_set_gesture_callback = _proto(
    "pt_window_set_gesture_callback", None, _c_void_p, _GESTURE, _c_void_p
)
_pt_window_set_long_press_delay = _proto(
    "pt_window_set_long_press_delay", None, _c_void_p, _c_uint32
)
_pt_window_set_momentum_scrolling = _proto(
    "pt_window_set_momentum_scrolling", None, _c_void_p, _c_bool
)
_pt_window_get_momentum_scrolling = _proto(
    "pt_window_get_momentum_scrolling", _c_bool, _c_void_p
)
_pt_window_set_drag_image = _proto(
    "pt_window_set_drag_image",
    None,
    _c_void_p,
    _ctypes.POINTER(_c_uint32),
    _c_int,
    _c_int,
)
_pt_window_clear_drag_image = _proto(
    "pt_window_clear_drag_image", None, _c_void_p
)
_pt_window_begin_drag = _proto(
    "pt_window_begin_drag", _c_bool, _c_void_p, _ctypes.POINTER(DragData), _c_uint32
)
_pt_window_get_drag_result = _proto("pt_window_get_drag_result", _c_int, _c_void_p)
_pt_window_set_drag_finished_callback = _proto(
    "pt_window_set_drag_finished_callback", None, _c_void_p, _DRAG_FINISHED, _c_void_p
)

# Structured incoming drops: offer introspection and asynchronous reads.
_pt_drop_offer_mime_count = _proto("pt_drop_offer_mime_count", _c_uint32, _c_void_p)
_pt_drop_offer_mime = _proto("pt_drop_offer_mime", _c_char_p, _c_void_p, _c_uint32)
_pt_drop_offer_has_mime = _proto(
    "pt_drop_offer_has_mime", _c_bool, _c_void_p, _c_char_p
)
_pt_drop_offer_selected_mime = _proto(
    "pt_drop_offer_selected_mime", _c_char_p, _c_void_p
)
_pt_drop_offer_actions = _proto("pt_drop_offer_actions", _c_uint32, _c_void_p)
_pt_drop_offer_read = _proto(
    "pt_drop_offer_read",
    _c_void_p,
    _c_void_p,
    _c_char_p,
    _c_size_t,
    _DROP_STREAM,
    _c_void_p,
)
_pt_drop_stream_cancel = _proto("pt_drop_stream_cancel", None, _c_void_p)
_pt_drop_stream_mime = _proto("pt_drop_stream_mime", _c_char_p, _c_void_p)
_pt_drop_stream_size = _proto("pt_drop_stream_size", _c_size_t, _c_void_p)
_pt_drop_stream_active = _proto("pt_drop_stream_active", _c_bool, _c_void_p)
_pt_uri_list_count = _proto("pt_uri_list_count", _c_uint32, _c_char_p)
_pt_uri_list_get = _proto("pt_uri_list_get", _c_void_p, _c_char_p, _c_uint32)
_pt_window_set_stylus_callback = _proto(
    "pt_window_set_stylus_callback", None, _c_void_p, _STYLUS, _c_void_p
)


def ease(easing: int, t: float) -> float:
    return float(_pt_ease(int(easing), float(t)))


def ease_value(easing: int, start: float, end: float, t: float) -> float:
    return float(_pt_ease_value(int(easing), float(start), float(end), float(t)))


def uri_list_count(text: str) -> int:
    """Counts URIs in a text/uri-list payload, ignoring comments and blanks."""
    return int(_pt_uri_list_count(_string(text)))


def uri_list_get(text: str, index: int) -> Optional[str]:
    pointer = _pt_uri_list_get(_string(text), int(index))
    if not pointer:
        return None
    try:
        return _ctypes.string_at(pointer).decode("utf-8")
    finally:
        _pt_free(pointer)


def uri_list(text: str) -> list[str]:
    """Parses a text/uri-list payload into a list of URI strings."""
    return [uri for index in range(uri_list_count(text))
            if (uri := uri_list_get(text, index)) is not None]


class Transition:
    """A caller-owned eased value for custom animation."""

    def __init__(self, value: float = 0.0) -> None:
        self._state = _TransitionState()
        _pt_transition_init(_ctypes.byref(self._state), float(value))

    @property
    def value(self) -> float:
        return float(self._state.value)

    @property
    def active(self) -> bool:
        return bool(_pt_transition_active(_ctypes.byref(self._state)))

    def to(
        self,
        target: float,
        duration_ms: int,
        easing: int = easing_out_cubic,
    ) -> "Transition":
        _pt_transition_to(
            _ctypes.byref(self._state), float(target), int(duration_ms), int(easing)
        )
        return self

    def advance(self, dt: float) -> bool:
        return bool(_pt_transition_advance(_ctypes.byref(self._state), float(dt)))

    def set(self, value: float) -> "Transition":
        _pt_transition_set(_ctypes.byref(self._state), float(value))
        return self

    def __repr__(self) -> str:
        return f"<pt.Transition value={self.value:.3f} active={self.active}>"


_widgets: dict[int, "Widget"] = {}
_windows: dict[int, "Window"] = {}
_callback_roots: list[Any] = []
# Live stream reads keep their trampoline alive until Pt delivers the final
# done/cancelled/error callback, after which Pt releases the native stream.
_drop_streams: dict[int, "DropStream"] = {}


def _error_message() -> str:
    message = _pt_get_last_error()
    return _text_out(message) or "unknown Pt error"


class Widget:
    """A non-owning handle to a native widget."""

    def __init__(self, pointer: Optional[int]) -> None:
        if not pointer:
            raise RuntimeError("Pt returned a null widget")
        self._ptr = int(pointer)
        self._callbacks: list[Any] = []
        self._drop_target: Optional[_DropTarget] = None
        self._drop_mime: Any = None
        self._row_text = b""
        self._combo_data: dict[int, Any] = {}
        _widgets[self._ptr] = self

    @classmethod
    def _wrap(cls, pointer: Optional[int]) -> Optional["Widget"]:
        if not pointer:
            return None
        existing = _widgets.get(int(pointer))
        if existing is not None:
            return existing
        return cls(pointer)

    def _keep(self, callback: Any) -> None:
        self._callbacks.append(callback)
        _callback_roots.append(callback)

    @property
    def raw(self) -> int:
        return self._ptr

    @raw.setter
    def raw(self, value: int) -> None:
        self._ptr = int(value)

    @property
    def kind(self) -> int:
        return int(_pt_widget_get_kind(self._ptr))

    @property
    def parent(self) -> Optional["Widget"]:
        return Widget._wrap(_pt_widget_parent(self._ptr))

    @property
    def text(self) -> str:
        return _text_out(_pt_widget_get_text(self._ptr))

    @text.setter
    def text(self, value: str) -> None:
        _pt_widget_set_text(self._ptr, _string(value))

    def set_text_spans(self, spans: "list[TextSpan] | None") -> "Widget":
        if not spans:
            _pt_widget_set_text_spans(self._ptr, None, 0)
            return self
        array = (TextSpan * len(spans))(*spans)
        _pt_widget_set_text_spans(self._ptr, array, len(spans))
        return self

    def clear_text_spans(self) -> "Widget":
        _pt_widget_clear_text_spans(self._ptr)
        return self

    def text_span_count(self) -> int:
        return int(_pt_widget_get_text_span_count(self._ptr))

    def text_span(self, index: int) -> Optional[TextSpan]:
        span = TextSpan()
        if not _pt_widget_get_text_span(self._ptr, index, _ctypes.byref(span)):
            return None
        return span

    def set_input_purpose(self, purpose: int) -> "Widget":
        _pt_widget_set_input_purpose(self._ptr, int(purpose))
        return self

    def input_purpose(self) -> int:
        return int(_pt_widget_get_input_purpose(self._ptr))

    @property
    def bounds(self) -> tuple[float, float, float, float]:
        x = _c_float()
        y = _c_float()
        width = _c_float()
        height = _c_float()
        _pt_widget_get_bounds(self._ptr, _ctypes.byref(x), _ctypes.byref(y),
                              _ctypes.byref(width), _ctypes.byref(height))
        return (x.value, y.value, width.value, height.value)

    @property
    def checked(self) -> bool:
        return bool(_pt_widget_get_checked(self._ptr))

    @checked.setter
    def checked(self, value: bool) -> None:
        _pt_widget_set_checked(self._ptr, bool(value))

    @property
    def value(self) -> float:
        return float(_pt_slider_get_value(self._ptr))

    @value.setter
    def value(self, value: float) -> None:
        _pt_slider_set_value(self._ptr, float(value))

    @property
    def progress(self) -> float:
        return float(_pt_progress_get_value(self._ptr))

    @progress.setter
    def progress(self, value: float) -> None:
        _pt_progress_set_value(self._ptr, float(value))

    @property
    def indeterminate(self) -> bool:
        return bool(_pt_progress_get_indeterminate(self._ptr))

    @indeterminate.setter
    def indeterminate(self, value: bool) -> None:
        _pt_progress_set_indeterminate(self._ptr, bool(value))

    @property
    def selected(self) -> int:
        return int(_pt_segmented_get_selected(self._ptr))

    @selected.setter
    def selected(self, index: int) -> None:
        _pt_segmented_set_selected(self._ptr, int(index))

    @property
    def selection(self) -> Optional[tuple[int, int]]:
        start = _c_uint32()
        end = _c_uint32()
        if self.kind == _widget_text_area:
            found = _pt_text_area_get_selection(
                self._ptr, _ctypes.byref(start), _ctypes.byref(end)
            )
        else:
            found = _pt_entry_get_selection(
                self._ptr, _ctypes.byref(start), _ctypes.byref(end)
            )
        if not found:
            return None
        return (int(start.value), int(end.value))

    @property
    def grow_to_content(self) -> bool:
        return bool(_pt_text_area_get_grow_to_content(self._ptr))

    @grow_to_content.setter
    def grow_to_content(self, enabled: bool) -> None:
        _pt_text_area_set_grow_to_content(self._ptr, bool(enabled))

    @property
    def grid_columns(self) -> int:
        return int(_pt_grid_get_columns(self._ptr))

    @grid_columns.setter
    def grid_columns(self, columns: int) -> None:
        _pt_grid_set_columns(self._ptr, int(columns))

    @property
    def grid_span(self) -> int:
        return int(_pt_widget_get_grid_span(self._ptr))

    @grid_span.setter
    def grid_span(self, span: int) -> None:
        _pt_widget_set_grid_span(self._ptr, int(span))

    @property
    def row_count(self) -> int:
        return int(_pt_list_view_get_row_count(self._ptr))

    @row_count.setter
    def row_count(self, count: int) -> None:
        _pt_list_view_set_row_count(self._ptr, int(count))

    @property
    def row_height(self) -> float:
        return float(_pt_list_view_get_row_height(self._ptr))

    @row_height.setter
    def row_height(self, height: float) -> None:
        _pt_list_view_set_row_height(self._ptr, float(height))

    @property
    def selected_row(self) -> int:
        return int(_pt_list_view_get_selected(self._ptr))

    @selected_row.setter
    def selected_row(self, index: int) -> None:
        _pt_list_view_set_selected(self._ptr, int(index))

    @property
    def scroll_offset(self) -> float:
        return float(_pt_scroll_get_offset(self._ptr))

    @scroll_offset.setter
    def scroll_offset(self, offset: float) -> None:
        _pt_scroll_set_offset(self._ptr, float(offset))

    @property
    def max_scroll_offset(self) -> float:
        return float(_pt_scroll_get_max_offset(self._ptr))

    @property
    def focused(self) -> bool:
        return bool(_pt_widget_has_focus(self._ptr))

    @property
    def drop_hover(self) -> bool:
        return bool(_pt_widget_get_drop_hover(self._ptr))

    @property
    def drag_enabled(self) -> bool:
        return bool(_pt_entry_get_drag_enabled(self._ptr))

    @drag_enabled.setter
    def drag_enabled(self, enabled: bool) -> None:
        _pt_entry_set_drag_enabled(self._ptr, bool(enabled))

    @property
    def a11y_role(self) -> int:
        return int(_pt_widget_get_a11y_role(self._ptr))

    @a11y_role.setter
    def a11y_role(self, role: int) -> None:
        _pt_widget_set_a11y_role(self._ptr, int(role))

    @property
    def a11y_name(self) -> str:
        return _text_out(_pt_widget_get_a11y_name(self._ptr))

    @a11y_name.setter
    def a11y_name(self, name: str) -> None:
        _pt_widget_set_a11y_name(self._ptr, _string(name))

    @property
    def a11y_description(self) -> str:
        return _text_out(_pt_widget_get_a11y_description(self._ptr))

    @a11y_description.setter
    def a11y_description(self, description: str) -> None:
        _pt_widget_set_a11y_description(self._ptr, _string(description))

    @property
    def a11y_state(self) -> int:
        return int(_pt_widget_get_a11y_state(self._ptr))

    @property
    def a11y_text(self) -> Optional[tuple[str, int, int, int]]:
        text = _c_char_p()
        caret = _c_uint32()
        start = _c_uint32()
        end = _c_uint32()
        found = _pt_widget_get_a11y_text(
            self._ptr,
            _ctypes.byref(text),
            _ctypes.byref(caret),
            _ctypes.byref(start),
            _ctypes.byref(end),
        )
        if not found:
            return None
        return (_text_out(text.value), int(caret.value), int(start.value), int(end.value))

    @property
    def a11y_value(self) -> Optional[float]:
        value = _c_float()
        if not _pt_widget_get_a11y_value(self._ptr, _ctypes.byref(value)):
            return None
        return float(value.value)

    def focus(self) -> bool:
        return bool(_pt_widget_focus(self._ptr))

    def set_drag_enabled(self, enabled: bool) -> "Widget":
        self.drag_enabled = enabled
        return self

    def set_drop_target(
        self,
        enter: Optional[Callable[["Widget", str], Any]] = None,
        motion: Optional[Callable[["Widget", float, float], Any]] = None,
        leave: Optional[Callable[["Widget"], Any]] = None,
        drop: Optional[Callable[["Widget", str, str, float, float], Any]] = None,
        enter_offer: Optional[Callable[["Widget", "DropOffer"], Optional[str]]] = None,
        drop_offer: Optional[
            Callable[["Widget", "DropOffer", str, float, float], Any]
        ] = None,
    ) -> "Widget":
        """Registers drop callbacks. Calling it without callbacks clears
        the target. Enter defaults to accepting every offered MIME type.

        The structured pair enter_offer/drop_offer takes precedence over
        enter/drop: enter_offer returns the MIME type to accept (or None to
        decline) and drop_offer receives the accepted offer, from which data
        can be streamed with DropOffer.read."""
        if (
            enter is None
            and motion is None
            and leave is None
            and drop is None
            and enter_offer is None
            and drop_offer is None
        ):
            self._drop_target = None
            _pt_widget_set_drop_target(self._ptr, None)
            return self

        def dispatch_enter(_widget, mime_type, _data) -> bool:
            return True if enter is None else bool(enter(self, _text_out(mime_type)))

        trampoline_enter = _DROP_ENTER(dispatch_enter)
        self._keep(trampoline_enter)
        trampoline_motion = _NULL_DROP_MOTION
        if motion is not None:
            def dispatch_motion(_widget, x, y, _data) -> None:
                motion(self, float(x), float(y))

            trampoline_motion = _DROP_MOTION(dispatch_motion)
            self._keep(trampoline_motion)
        trampoline_leave = _NULL_DROP_LEAVE
        if leave is not None:
            def dispatch_leave(_widget, _data) -> None:
                leave(self)

            trampoline_leave = _DROP_LEAVE(dispatch_leave)
            self._keep(trampoline_leave)
        trampoline_drop = _NULL_DROP_DATA
        if drop is not None:
            def dispatch_drop(_widget, mime_type, data, x, y, _data) -> None:
                drop(self, _text_out(mime_type), _text_out(data), float(x), float(y))

            trampoline_drop = _DROP_DATA(dispatch_drop)
            self._keep(trampoline_drop)
        trampoline_enter_offer = _NULL_DROP_OFFER_ENTER
        if enter_offer is not None:
            def dispatch_enter_offer(_widget, offer, _data) -> Optional[int]:
                choice = enter_offer(self, DropOffer(offer, self))
                if not choice:
                    self._drop_mime = None
                    return None
                encoded = _string(choice) if isinstance(choice, str) else bytes(choice)
                # Returning the address of a buffer we keep alive avoids the
                # leak ctypes warns about when a callback returns bytes.
                self._drop_mime = _ctypes.create_string_buffer(encoded)
                return _ctypes.addressof(self._drop_mime)

            trampoline_enter_offer = _DROP_OFFER_ENTER(dispatch_enter_offer)
            self._keep(trampoline_enter_offer)
        trampoline_drop_offer = _NULL_DROP_OFFER_DROP
        if drop_offer is not None:
            def dispatch_drop_offer(_widget, offer, mime_type, x, y, _data) -> None:
                drop_offer(
                    self,
                    DropOffer(offer, self),
                    _text_out(mime_type),
                    float(x),
                    float(y),
                )

            trampoline_drop_offer = _DROP_OFFER_DROP(dispatch_drop_offer)
            self._keep(trampoline_drop_offer)
        self._drop_target = _DropTarget(
            trampoline_enter,
            trampoline_motion,
            trampoline_leave,
            trampoline_drop,
            None,
            trampoline_enter_offer,
            trampoline_drop_offer,
        )
        _pt_widget_set_drop_target(self._ptr, _ctypes.byref(self._drop_target))
        return self

    def set_tooltip(self, text: str) -> "Widget":
        _pt_widget_set_tooltip(self._ptr, _string(text))
        return self

    def set_tooltip_delay(self, milliseconds: int) -> "Widget":
        _pt_widget_set_tooltip_delay(self._ptr, int(milliseconds))
        return self

    def destroy(self) -> None:
        _pt_widget_destroy(self._ptr)
        _widgets.pop(self._ptr, None)

    def clear_children(self) -> "Widget":
        _pt_widget_clear_children(self._ptr)
        return self

    def set_size(self, width: float, height: float) -> "Widget":
        _pt_widget_set_size(self._ptr, width, height)
        return self

    def set_width(self, width: float) -> "Widget":
        _pt_widget_set_width(self._ptr, width)
        return self

    def set_height(self, height: float) -> "Widget":
        _pt_widget_set_height(self._ptr, height)
        return self

    def set_min_width(self, width: float) -> "Widget":
        _pt_widget_set_min_width(self._ptr, width)
        return self

    def set_min_height(self, height: float) -> "Widget":
        _pt_widget_set_min_height(self._ptr, height)
        return self

    def set_max_width(self, width: float) -> "Widget":
        _pt_widget_set_max_width(self._ptr, width)
        return self

    def set_max_height(self, height: float) -> "Widget":
        _pt_widget_set_max_height(self._ptr, height)
        return self

    def set_margin(self, top: float, right: float, bottom: float, left: float) -> "Widget":
        _pt_widget_set_margin(self._ptr, top, right, bottom, left)
        return self

    def set_padding(self, top: float, right: float, bottom: float, left: float) -> "Widget":
        _pt_widget_set_padding(self._ptr, top, right, bottom, left)
        return self

    def set_gap(self, gap: float) -> "Widget":
        _pt_widget_set_gap(self._ptr, gap)
        return self

    def set_grow(self, grow: float) -> "Widget":
        _pt_widget_set_grow(self._ptr, grow)
        return self

    def set_align_self(self, align: int) -> "Widget":
        _pt_widget_set_align_self(self._ptr, align)
        return self

    def set_direction(self, direction: int) -> "Widget":
        _pt_box_set_direction(self._ptr, direction)
        return self

    def set_align(self, align: int) -> "Widget":
        _pt_box_set_align(self._ptr, align)
        return self

    def set_justify(self, justify: int) -> "Widget":
        _pt_box_set_justify(self._ptr, justify)
        return self

    def set_background(self, color: int) -> "Widget":
        _pt_widget_set_background(self._ptr, color)
        return self

    def set_foreground(self, color: int) -> "Widget":
        _pt_widget_set_foreground(self._ptr, color)
        return self

    def set_border_color(self, color: int) -> "Widget":
        _pt_widget_set_border_color(self._ptr, color)
        return self

    def set_border_width(self, width: float) -> "Widget":
        _pt_widget_set_border_width(self._ptr, width)
        return self

    def set_radius(self, radius: float) -> "Widget":
        _pt_widget_set_radius(self._ptr, radius)
        return self

    def set_font_size(self, size: float) -> "Widget":
        _pt_widget_set_font_size(self._ptr, size)
        return self

    def set_font_weight(self, weight: int) -> "Widget":
        _pt_widget_set_font_weight(self._ptr, weight)
        return self

    def set_text_align(self, align: int) -> "Widget":
        _pt_widget_set_text_align(self._ptr, align)
        return self

    def set_visible(self, visible: bool) -> "Widget":
        _pt_widget_set_visible(self._ptr, bool(visible))
        return self

    def set_enabled(self, enabled: bool) -> "Widget":
        _pt_widget_set_enabled(self._ptr, bool(enabled))
        return self

    @property
    def busy(self) -> bool:
        return bool(_pt_widget_get_busy(self._ptr))

    @busy.setter
    def busy(self, busy: bool) -> None:
        _pt_widget_set_busy(self._ptr, bool(busy))

    def set_busy(self, busy: bool = True) -> "Widget":
        self.busy = busy
        return self

    @property
    def invalid(self) -> bool:
        return bool(_pt_widget_get_invalid(self._ptr))

    @invalid.setter
    def invalid(self, invalid: bool) -> None:
        _pt_widget_set_invalid(self._ptr, bool(invalid))

    def set_invalid(self, invalid: bool = True) -> "Widget":
        self.invalid = invalid
        return self

    @property
    def chip_removable(self) -> bool:
        return bool(_pt_chip_get_removable(self._ptr))

    @chip_removable.setter
    def chip_removable(self, removable: bool) -> None:
        _pt_chip_set_removable(self._ptr, bool(removable))

    @property
    def chip_selected(self) -> bool:
        return bool(_pt_chip_get_selected(self._ptr))

    @chip_selected.setter
    def chip_selected(self, selected: bool) -> None:
        _pt_chip_set_selected(self._ptr, bool(selected))

    def set_chip_icon(self, glyph: int) -> "Widget":
        _pt_chip_set_icon(self._ptr, int(glyph))
        return self

    def set_chip_removable(self, removable: bool = True) -> "Widget":
        self.chip_removable = removable
        return self

    def set_chip_selected(self, selected: bool = True) -> "Widget":
        self.chip_selected = selected
        return self

    def on_chip_click(self, callback: Callable[[], Any]) -> "Widget":
        def dispatch(_widget: int, _data: int) -> None:
            callback()

        trampoline = _CLICK(dispatch)
        self._keep(trampoline)
        _pt_chip_set_on_click(self._ptr, trampoline, None)
        return self

    def on_chip_remove(self, callback: Callable[[], Any]) -> "Widget":
        def dispatch(_widget: int, _data: int) -> None:
            callback()

        trampoline = _CLICK(dispatch)
        self._keep(trampoline)
        _pt_chip_set_on_remove(self._ptr, trampoline, None)
        return self

    def set_wrap(self, wrap: bool) -> "Widget":
        _pt_widget_set_label_wrap(self._ptr, bool(wrap))
        return self

    def set_checked(self, checked: bool) -> "Widget":
        self.checked = checked
        return self

    def set_value(self, value: float) -> "Widget":
        self.value = value
        return self

    def set_progress(self, value: float) -> "Widget":
        self.progress = value
        return self

    def set_indeterminate(self, indeterminate: bool) -> "Widget":
        self.indeterminate = indeterminate
        return self

    def select(self, index: int) -> "Widget":
        self.selected = index
        return self

    def select_all(self) -> "Widget":
        if self.kind == _widget_text_area:
            _pt_text_area_select_all(self._ptr)
        else:
            _pt_entry_select_all(self._ptr)
        return self

    def set_selection(self, start: int, end: int) -> "Widget":
        if self.kind == _widget_text_area:
            _pt_text_area_set_selection(self._ptr, int(start), int(end))
        else:
            _pt_entry_set_selection(self._ptr, int(start), int(end))
        return self

    def undo(self) -> bool:
        if self.kind == _widget_text_area:
            return bool(_pt_text_area_undo(self._ptr))
        return bool(_pt_entry_undo(self._ptr))

    def redo(self) -> bool:
        if self.kind == _widget_text_area:
            return bool(_pt_text_area_redo(self._ptr))
        return bool(_pt_entry_redo(self._ptr))

    def set_grow_to_content(self, enabled: bool = True) -> "Widget":
        self.grow_to_content = enabled
        return self

    def scroll_to_caret(self) -> "Widget":
        _pt_text_area_scroll_to_caret(self._ptr)
        return self

    def set_wrap_gap(self, row_gap: float, column_gap: float) -> "Widget":
        _pt_wrap_set_gap(self._ptr, float(row_gap), float(column_gap))
        return self

    def set_grid_columns(self, columns: int) -> "Widget":
        self.grid_columns = columns
        return self

    def set_grid_gap(self, row_gap: float, column_gap: float) -> "Widget":
        _pt_grid_set_gap(self._ptr, float(row_gap), float(column_gap))
        return self

    def set_grid_span(self, span: int) -> "Widget":
        self.grid_span = span
        return self

    def set_row_count(self, count: int) -> "Widget":
        self.row_count = count
        return self

    def set_row_height(self, height: float) -> "Widget":
        self.row_height = height
        return self

    def set_selected_row(self, index: int) -> "Widget":
        self.selected_row = index
        return self

    def scroll_to_row(self, index: int) -> "Widget":
        _pt_list_view_scroll_to_row(self._ptr, int(index))
        return self

    def refresh(self) -> "Widget":
        _pt_list_view_refresh(self._ptr)
        return self

    def refresh_rows(self, first: int, count: int) -> "Widget":
        _pt_list_view_refresh_rows(self._ptr, int(first), int(count))
        return self

    @property
    def selected_key(self) -> int:
        return int(_pt_list_view_get_selected_key(self._ptr))

    def set_selected_key(self, key: int) -> bool:
        return bool(_pt_list_view_set_selected_key(self._ptr, int(key)))

    def set_row_key_callback(
        self, callback: Callable[["Widget", int], int]
    ) -> "Widget":
        def dispatch(view: int, row: int, _data: int) -> int:
            return int(callback(Widget._wrap(view), int(row)))

        trampoline = _ROW_KEY(dispatch)
        self._keep(trampoline)
        _pt_list_view_set_row_key_callback(self._ptr, trampoline, None)
        return self

    def set_row_text_callback(
        self, callback: Callable[["Widget", int], str]
    ) -> "Widget":
        def dispatch(view: int, row: int, _data: int) -> bytes:
            self._row_text = _string(callback(Widget._wrap(view), int(row)))
            return self._row_text

        trampoline = _ROW_TEXT(dispatch)
        self._keep(trampoline)
        _pt_list_view_set_row_text_callback(self._ptr, trampoline, None)
        return self

    @property
    def list_selection_count(self) -> int:
        return int(_pt_list_view_get_selection_count(self._ptr))

    def is_list_row_selected(self, row: int) -> bool:
        return bool(_pt_list_view_is_row_selected(self._ptr, int(row)))

    def list_selection(self) -> list[int]:
        capacity = self.list_selection_count
        rows = (_c_uint32 * capacity)()
        count = _pt_list_view_get_selection(self._ptr, rows, capacity)
        return [int(rows[i]) for i in range(int(count))]

    def list_select_range(self, first: int, last: int) -> "Widget":
        _pt_list_view_select_range(self._ptr, int(first), int(last))
        return self

    def list_select_toggle(self, row: int) -> "Widget":
        _pt_list_view_select_toggle(self._ptr, int(row))
        return self

    def clear_list_selection(self) -> "Widget":
        _pt_list_view_clear_selection(self._ptr)
        return self

    @property
    def icon(self) -> int:
        return int(_pt_widget_get_icon(self._ptr))

    @icon.setter
    def icon(self, glyph: int) -> None:
        _pt_widget_set_icon(self._ptr, int(glyph))

    @property
    def icon_size(self) -> float:
        return float(_pt_icon_get_size(self._ptr))

    @icon_size.setter
    def icon_size(self, size: float) -> None:
        _pt_icon_set_size(self._ptr, float(size))

    def set_icon(self, glyph: int) -> "Widget":
        self.icon = glyph
        return self

    def set_icon_size(self, size: float) -> "Widget":
        self.icon_size = size
        return self

    @property
    def column_count(self) -> int:
        return int(_pt_table_get_column_count(self._ptr))

    @column_count.setter
    def column_count(self, count: int) -> None:
        _pt_table_set_column_count(self._ptr, int(count))

    @property
    def table_row_count(self) -> int:
        return int(_pt_table_get_row_count(self._ptr))

    @table_row_count.setter
    def table_row_count(self, count: int) -> None:
        _pt_table_set_row_count(self._ptr, int(count))

    @property
    def table_row_height(self) -> float:
        return float(_pt_table_get_row_height(self._ptr))

    @table_row_height.setter
    def table_row_height(self, height: float) -> None:
        _pt_table_set_row_height(self._ptr, float(height))

    @property
    def table_selected(self) -> int:
        return int(_pt_table_get_selected(self._ptr))

    @table_selected.setter
    def table_selected(self, index: int) -> None:
        _pt_table_set_selected(self._ptr, int(index))

    @property
    def sort(self) -> tuple[int, int]:
        column = _c_uint32(table_column_none)
        direction = _pt_table_get_sort(self._ptr, _ctypes.byref(column))
        return (int(direction), int(column.value))

    @property
    def active_tab(self) -> int:
        return int(_pt_tabs_get_active(self._ptr))

    @active_tab.setter
    def active_tab(self, index: int) -> None:
        _pt_tabs_set_active(self._ptr, int(index))

    @property
    def tab_count(self) -> int:
        return int(_pt_tabs_get_page_count(self._ptr))

    @property
    def sidebar_item_count(self) -> int:
        return int(_pt_sidebar_get_item_count(self._ptr))

    @property
    def active_sidebar_item(self) -> int:
        return int(_pt_sidebar_get_active(self._ptr))

    @active_sidebar_item.setter
    def active_sidebar_item(self, index: int) -> None:
        _pt_sidebar_set_active(self._ptr, int(index))

    @property
    def sidebar_compact(self) -> bool:
        return bool(_pt_sidebar_get_compact(self._ptr))

    @sidebar_compact.setter
    def sidebar_compact(self, compact: bool) -> None:
        _pt_sidebar_set_compact(self._ptr, bool(compact))

    @property
    def sidebar_header(self) -> str:
        return _text_out(_pt_sidebar_get_header(self._ptr))

    @sidebar_header.setter
    def sidebar_header(self, text: str) -> None:
        _pt_sidebar_set_header(self._ptr, _string(text))

    def set_column_count(self, count: int) -> "Widget":
        self.column_count = count
        return self

    def set_column_title(self, column: int, title: str) -> "Widget":
        _pt_table_set_column_title(self._ptr, int(column), _string(title))
        return self

    def set_column_width(self, column: int, width: float) -> "Widget":
        _pt_table_set_column_width(self._ptr, int(column), float(width))
        return self

    def column_width(self, column: int) -> float:
        return float(_pt_table_get_column_width(self._ptr, int(column)))

    def set_column_align(self, column: int, align: int) -> "Widget":
        _pt_table_set_column_align(self._ptr, int(column), int(align))
        return self

    def set_column_resizable(self, column: int, resizable: bool) -> "Widget":
        _pt_table_set_column_resizable(self._ptr, int(column), bool(resizable))
        return self

    def set_column_hidden(self, column: int, hidden: bool = True) -> "Widget":
        _pt_table_set_column_hidden(self._ptr, int(column), bool(hidden))
        return self

    def column_hidden(self, column: int) -> bool:
        return bool(_pt_table_get_column_hidden(self._ptr, int(column)))

    def set_header_height(self, height: float) -> "Widget":
        _pt_table_set_header_height(self._ptr, float(height))
        return self

    def set_table_row_count(self, count: int) -> "Widget":
        self.table_row_count = count
        return self

    def set_table_row_height(self, height: float) -> "Widget":
        self.table_row_height = height
        return self

    def set_table_selected(self, index: int) -> "Widget":
        self.table_selected = index
        return self

    def set_sort(self, column: int, direction: int = sort_ascending) -> "Widget":
        _pt_table_set_sort(self._ptr, int(column), int(direction))
        return self

    def table_refresh(self) -> "Widget":
        _pt_table_refresh(self._ptr)
        return self

    def table_refresh_rows(self, first: int, count: int) -> "Widget":
        _pt_table_refresh_rows(self._ptr, int(first), int(count))
        return self

    @property
    def table_selected_key(self) -> int:
        return int(_pt_table_get_selected_key(self._ptr))

    def set_table_selected_key(self, key: int) -> bool:
        return bool(_pt_table_set_selected_key(self._ptr, int(key)))

    def set_table_row_key_callback(
        self, callback: Callable[["Widget", int], int]
    ) -> "Widget":
        def dispatch(view: int, row: int, _data: int) -> int:
            return int(callback(Widget._wrap(view), int(row)))

        trampoline = _ROW_KEY(dispatch)
        self._keep(trampoline)
        _pt_table_set_row_key_callback(self._ptr, trampoline, None)
        return self

    def set_table_row_text_callback(
        self, callback: Callable[["Widget", int], str]
    ) -> "Widget":
        def dispatch(view: int, row: int, _data: int) -> bytes:
            self._row_text = _string(callback(Widget._wrap(view), int(row)))
            return self._row_text

        trampoline = _ROW_TEXT(dispatch)
        self._keep(trampoline)
        _pt_table_set_row_text_callback(self._ptr, trampoline, None)
        return self

    @property
    def sort_columns(self) -> list[tuple[int, int]]:
        columns = (_c_uint32 * table_max_sort_columns)()
        directions = (_c_int * table_max_sort_columns)()
        count = _pt_table_get_sort_columns(self._ptr, columns, directions)
        return [(int(columns[i]), int(directions[i])) for i in range(int(count))]

    def set_sort_columns(self, keys: list[tuple[int, int]]) -> "Widget":
        count = min(len(keys), table_max_sort_columns)
        columns = (_c_uint32 * count)(*[int(key[0]) for key in keys[:count]])
        directions = (_c_int * count)(*[int(key[1]) for key in keys[:count]])
        _pt_table_set_sort_columns(self._ptr, count, columns, directions)
        return self

    def on_sort_columns(
        self, callback: Callable[[int, list[int], list[int]], Any]
    ) -> "Widget":
        def dispatch(_table: int, count: int, columns: Any, directions: Any,
                     _data: int) -> None:
            callback(
                int(count),
                [int(columns[i]) for i in range(int(count))],
                [int(directions[i]) for i in range(int(count))],
            )

        trampoline = _SORT_COLUMNS(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_sort_columns(self._ptr, trampoline, None)
        return self

    def table_scroll_to_row(self, index: int) -> "Widget":
        _pt_table_scroll_to_row(self._ptr, int(index))
        return self

    def scroll_to_column(self, column: int) -> "Widget":
        _pt_table_scroll_to_column(self._ptr, int(column))
        return self

    @property
    def table_editable(self) -> bool:
        return bool(_pt_table_get_editable(self._ptr))

    @table_editable.setter
    def table_editable(self, editable: bool) -> None:
        _pt_table_set_editable(self._ptr, bool(editable))

    def set_table_editable(self, editable: bool = True) -> "Widget":
        self.table_editable = editable
        return self

    def begin_cell_edit(self, row: int, column: int) -> bool:
        return bool(_pt_table_begin_edit(self._ptr, int(row), int(column)))

    def commit_cell_edit(self) -> bool:
        return bool(_pt_table_commit_edit(self._ptr))

    def cancel_cell_edit(self) -> "Widget":
        _pt_table_cancel_edit(self._ptr)
        return self

    @property
    def table_editing(self) -> bool:
        return bool(_pt_table_is_editing(self._ptr))

    def on_cell_edited(
        self, callback: Callable[["Widget", int, int, str], Any]
    ) -> "Widget":
        def dispatch(_table: int, row: int, column: int, text: Any, _data: int) -> None:
            callback(self, int(row), int(column), _text_out(text))

        trampoline = _TABLE_EDITED(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_cell_edited(self._ptr, trampoline, None)
        return self

    @property
    def table_selection_count(self) -> int:
        return int(_pt_table_get_selection_count(self._ptr))

    def is_row_selected(self, row: int) -> bool:
        return bool(_pt_table_is_row_selected(self._ptr, int(row)))

    def table_selection(self) -> list[int]:
        capacity = self.table_selection_count
        rows = (_c_uint32 * capacity)()
        count = _pt_table_get_selection(self._ptr, rows, capacity)
        return [int(rows[i]) for i in range(int(count))]

    def select_range(self, first: int, last: int) -> "Widget":
        _pt_table_select_range(self._ptr, int(first), int(last))
        return self

    def select_toggle(self, row: int) -> "Widget":
        _pt_table_select_toggle(self._ptr, int(row))
        return self

    def clear_table_selection(self) -> "Widget":
        _pt_table_clear_selection(self._ptr)
        return self

    @property
    def columns_reorderable(self) -> bool:
        return bool(_pt_table_get_columns_reorderable(self._ptr))

    @columns_reorderable.setter
    def columns_reorderable(self, reorderable: bool) -> None:
        _pt_table_set_columns_reorderable(self._ptr, bool(reorderable))

    def set_columns_reorderable(self, reorderable: bool = True) -> "Widget":
        self.columns_reorderable = reorderable
        return self

    def move_column(self, source: int, target: int) -> bool:
        return bool(_pt_table_move_column(self._ptr, int(source), int(target)))

    def on_columns_reordered(
        self, callback: Callable[["Widget", int, int], Any]
    ) -> "Widget":
        def dispatch(table: int, source: int, target: int, _data: int) -> None:
            callback(Widget._wrap(table), int(source), int(target))

        trampoline = _TABLE_REORDERED(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_columns_reordered(self._ptr, trampoline, None)
        return self

    def on_row_drag(self, callback: Callable[["Widget", int], Any]) -> "Widget":
        def dispatch(table: int, row: int, _data: int) -> None:
            callback(Widget._wrap(table), int(row))

        trampoline = _TABLE_ROW_DRAG(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_row_drag(self._ptr, trampoline, None)
        return self

    def on_bind_cell(
        self, callback: Callable[["Widget", int, int], Any]
    ) -> "Widget":
        def dispatch(_table: int, cell: int, row: int, column: int, _data: int) -> None:
            callback(Widget._wrap(cell), int(row), int(column))

        trampoline = _BIND_CELL(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_bind_cell(self._ptr, trampoline, None)
        return self

    def on_table_select(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_table: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_select(self._ptr, trampoline, None)
        return self

    def on_table_activate(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_table: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_activate(self._ptr, trampoline, None)
        return self

    def on_table_sort(self, callback: Callable[[int, int], Any]) -> "Widget":
        def dispatch(_table: int, column: int, direction: int, _data: int) -> None:
            callback(int(column), int(direction))

        trampoline = _SORT(dispatch)
        self._keep(trampoline)
        _pt_table_set_on_sort(self._ptr, trampoline, None)
        return self

    def add_tree_node(self, parent: Optional["TreeNode"], text: str) -> TreeNode:
        pointer = _pt_tree_add_node(
            self._ptr, parent._ptr if parent else None, _string(text)
        )
        if not pointer:
            raise RuntimeError("failed to add tree node")
        return TreeNode(pointer)

    @property
    def tree_root_count(self) -> int:
        return int(_pt_tree_get_root_count(self._ptr))

    def tree_root(self, index: int) -> Optional[TreeNode]:
        pointer = _pt_tree_get_root_node(self._ptr, int(index))
        return TreeNode(pointer) if pointer else None

    @property
    def tree_visible_count(self) -> int:
        return int(_pt_tree_get_visible_count(self._ptr))

    def tree_visible_node(self, row: int) -> Optional[TreeNode]:
        pointer = _pt_tree_get_visible_node(self._ptr, int(row))
        return TreeNode(pointer) if pointer else None

    @property
    def tree_selected(self) -> Optional[TreeNode]:
        pointer = _pt_tree_get_selected(self._ptr)
        return TreeNode(pointer) if pointer else None

    @tree_selected.setter
    def tree_selected(self, node: Optional[TreeNode]) -> None:
        _pt_tree_set_selected(self._ptr, node._ptr if node else None)

    def set_tree_selected(self, node: Optional[TreeNode]) -> "Widget":
        self.tree_selected = node
        return self

    @property
    def tree_selection_count(self) -> int:
        return int(_pt_tree_get_selection_count(self._ptr))

    def is_tree_node_selected(self, node: TreeNode) -> bool:
        return bool(_pt_tree_is_selected(self._ptr, node._ptr if node else None))

    def tree_selection(self) -> list[TreeNode]:
        capacity = self.tree_selection_count
        pointers = (_c_void_p * capacity)()
        count = _pt_tree_get_selection(self._ptr, pointers, capacity)
        return [TreeNode(pointers[i]) for i in range(int(count))]

    def tree_select_range(self, first_row: int, last_row: int) -> "Widget":
        _pt_tree_select_range(self._ptr, int(first_row), int(last_row))
        return self

    def tree_select_toggle(self, node: TreeNode) -> "Widget":
        _pt_tree_select_toggle(self._ptr, node._ptr if node else None)
        return self

    def clear_tree_selection(self) -> "Widget":
        _pt_tree_clear_selection(self._ptr)
        return self

    def set_tree_row_height(self, height: float) -> "Widget":
        _pt_tree_set_row_height(self._ptr, float(height))
        return self

    def set_tree_indent(self, indent: float) -> "Widget":
        _pt_tree_set_indent(self._ptr, float(indent))
        return self

    def on_tree_select(self, callback: Callable[[TreeNode], Any]) -> "Widget":
        def dispatch(_tree: int, node: int, _data: int) -> None:
            callback(TreeNode(node))

        trampoline = _TREE_NODE(dispatch)
        self._keep(trampoline)
        _pt_tree_set_on_select(self._ptr, trampoline, None)
        return self

    def on_tree_activate(self, callback: Callable[[TreeNode], Any]) -> "Widget":
        def dispatch(_tree: int, node: int, _data: int) -> None:
            callback(TreeNode(node))

        trampoline = _TREE_NODE(dispatch)
        self._keep(trampoline)
        _pt_tree_set_on_activate(self._ptr, trampoline, None)
        return self

    def tree_refresh(self) -> "Widget":
        _pt_tree_refresh(self._ptr)
        return self

    def scroll_to_tree_node(self, node: TreeNode) -> "Widget":
        _pt_tree_scroll_to_node(self._ptr, node._ptr)
        return self

    @property
    def tree_renamable(self) -> bool:
        return bool(_pt_tree_get_renamable(self._ptr))

    @tree_renamable.setter
    def tree_renamable(self, renamable: bool) -> None:
        _pt_tree_set_renamable(self._ptr, bool(renamable))

    def set_tree_renamable(self, renamable: bool = True) -> "Widget":
        self.tree_renamable = renamable
        return self

    def begin_tree_rename(self, node: TreeNode) -> bool:
        return bool(_pt_tree_begin_rename(self._ptr, node._ptr))

    def commit_tree_rename(self) -> bool:
        return bool(_pt_tree_commit_rename(self._ptr))

    def cancel_tree_rename(self) -> "Widget":
        _pt_tree_cancel_rename(self._ptr)
        return self

    @property
    def tree_renaming(self) -> bool:
        return bool(_pt_tree_is_renaming(self._ptr))

    def on_tree_rename(
        self, callback: Callable[["Widget", TreeNode, str], Any]
    ) -> "Widget":
        def dispatch(tree: int, node: int, text: Any, _data: int) -> None:
            callback(Widget._wrap(tree), TreeNode(node), _text_out(text))

        trampoline = _TREE_RENAME(dispatch)
        self._keep(trampoline)
        _pt_tree_set_on_rename(self._ptr, trampoline, None)
        return self

    def clear_tree(self) -> "Widget":
        _pt_tree_clear(self._ptr)
        return self

    def add_item(self, text: str) -> "Widget":
        _pt_combobox_add_item(self._ptr, _string(text))
        return self

    def insert_item(self, index: int, text: str) -> "Widget":
        index = int(index)
        self._combo_data = {
            (key + 1 if key >= index else key): value
            for key, value in self._combo_data.items()
        }
        _pt_combobox_insert_item(self._ptr, index, _string(text))
        return self

    def remove_item(self, index: int) -> "Widget":
        index = int(index)
        self._combo_data = {
            (key - 1 if key > index else key): value
            for key, value in self._combo_data.items()
            if key != index
        }
        _pt_combobox_remove_item(self._ptr, index)
        return self

    def clear_items(self) -> "Widget":
        self._combo_data.clear()
        _pt_combobox_clear(self._ptr)
        return self

    @property
    def item_count(self) -> int:
        return int(_pt_combobox_get_item_count(self._ptr))

    def item_text(self, index: int) -> str:
        return _text_out(_pt_combobox_get_item_text(self._ptr, int(index)))

    def set_item_text(self, index: int, text: str) -> "Widget":
        _pt_combobox_set_item_text(self._ptr, int(index), _string(text))
        return self

    def set_item_data(self, index: int, data: Any) -> "Widget":
        index = int(index)
        if data is None:
            self._combo_data.pop(index, None)
            _pt_combobox_set_item_data(self._ptr, index, None)
        else:
            self._combo_data[index] = data
            _pt_combobox_set_item_data(self._ptr, index, id(data))
        return self

    def item_data(self, index: int) -> Any:
        index = int(index)
        if index in self._combo_data:
            return self._combo_data[index]
        return _pt_combobox_get_item_data(self._ptr, index)

    def set_item_disabled(self, index: int, disabled: bool) -> "Widget":
        _pt_combobox_set_item_disabled(self._ptr, int(index), bool(disabled))
        return self

    def is_item_disabled(self, index: int) -> bool:
        return bool(_pt_combobox_get_item_disabled(self._ptr, int(index)))

    @property
    def combo_mode(self) -> int:
        return int(_pt_combobox_get_mode(self._ptr))

    @combo_mode.setter
    def combo_mode(self, mode: int) -> None:
        _pt_combobox_set_mode(self._ptr, int(mode))

    def set_combo_mode(self, mode: int) -> "Widget":
        self.combo_mode = mode
        return self

    @property
    def combo_selected(self) -> int:
        return int(_pt_combobox_get_selected(self._ptr))

    @combo_selected.setter
    def combo_selected(self, index: int) -> None:
        _pt_combobox_set_selected(self._ptr, int(index))

    def set_combo_selected(self, index: int) -> "Widget":
        self.combo_selected = index
        return self

    @property
    def combo_text(self) -> str:
        return _text_out(_pt_combobox_get_text(self._ptr))

    @combo_text.setter
    def combo_text(self, text: str) -> None:
        _pt_combobox_set_text(self._ptr, _string(text))

    def set_combo_text(self, text: str) -> "Widget":
        self.combo_text = text
        return self

    @property
    def combo_placeholder(self) -> str:
        return _text_out(_pt_combobox_get_placeholder(self._ptr))

    @combo_placeholder.setter
    def combo_placeholder(self, text: str) -> None:
        _pt_combobox_set_placeholder(self._ptr, _string(text))

    def set_combo_placeholder(self, text: str) -> "Widget":
        self.combo_placeholder = text
        return self

    def open_popup(self) -> "Widget":
        _pt_combobox_open(self._ptr)
        return self

    def close_popup(self) -> "Widget":
        _pt_combobox_close(self._ptr)
        return self

    @property
    def popup_open(self) -> bool:
        return bool(_pt_combobox_is_open(self._ptr))

    @property
    def popup_max_height(self) -> float:
        return float(_pt_combobox_get_popup_max_height(self._ptr))

    @popup_max_height.setter
    def popup_max_height(self, height: float) -> None:
        _pt_combobox_set_popup_max_height(self._ptr, float(height))

    def set_popup_max_height(self, height: float) -> "Widget":
        self.popup_max_height = height
        return self

    def on_combo_change(self, callback: Callable[["Widget", int], Any]) -> "Widget":
        def dispatch(_combobox: int, index: int, _data: int) -> None:
            callback(self, int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_combobox_set_on_change(self._ptr, trampoline, None)
        return self

    def on_combo_text_change(self, callback: Callable[["Widget", str], Any]) -> "Widget":
        def dispatch(_combobox: int, text: Any, _data: int) -> None:
            callback(self, _text_out(text))

        trampoline = _COMBO_TEXT(dispatch)
        self._keep(trampoline)
        _pt_combobox_set_on_text_change(self._ptr, trampoline, None)
        return self

    def add_tab(self, title: str) -> "Widget":
        pointer = _pt_tabs_add_page(self._ptr, _string(title))
        if not pointer:
            raise RuntimeError("failed to add tab page")
        return Widget(pointer)

    def remove_tab(self, index: int) -> None:
        page = _pt_tabs_get_page(self._ptr, int(index))
        _pt_tabs_remove_page(self._ptr, int(index))
        if page:
            _widgets.pop(int(page), None)

    def tab_page(self, index: int) -> Optional["Widget"]:
        pointer = _pt_tabs_get_page(self._ptr, int(index))
        return Widget._wrap(pointer) if pointer else None

    def set_tab_title(self, index: int, title: str) -> "Widget":
        _pt_tabs_set_page_title(self._ptr, int(index), _string(title))
        return self

    def tab_title(self, index: int) -> str:
        return _text_out(_pt_tabs_get_page_title(self._ptr, int(index)))

    def set_active_tab(self, index: int) -> "Widget":
        self.active_tab = index
        return self

    def on_tab_change(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_tabs: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_tabs_set_on_change(self._ptr, trampoline, None)
        return self

    def set_tab_strip_height(self, height: float) -> "Widget":
        _pt_tabs_set_strip_height(self._ptr, float(height))
        return self

    def add_sidebar_item(self, glyph: int, label: str) -> "Widget":
        pointer = _pt_sidebar_add_item(self._ptr, int(glyph), _string(label))
        if not pointer:
            raise RuntimeError("failed to add sidebar item")
        return Widget(pointer)

    def sidebar_item(self, index: int) -> Optional["Widget"]:
        pointer = _pt_sidebar_get_item(self._ptr, int(index))
        return Widget._wrap(pointer) if pointer else None

    def set_active_sidebar_item(self, index: int) -> "Widget":
        self.active_sidebar_item = index
        return self

    def set_sidebar_compact(self, compact: bool) -> "Widget":
        self.sidebar_compact = compact
        return self

    def set_sidebar_header(self, text: str) -> "Widget":
        self.sidebar_header = text
        return self

    def on_sidebar_select(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_sidebar: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_sidebar_set_on_select(self._ptr, trampoline, None)
        return self

    def set_scroll_offset(self, offset: float) -> "Widget":
        self.scroll_offset = offset
        return self

    def set_button_variant(self, variant: int) -> "Widget":
        _pt_button_set_variant(self._ptr, variant)
        return self

    def set_placeholder(self, placeholder: str) -> "Widget":
        if self.kind == _widget_text_area:
            _pt_text_area_set_placeholder(self._ptr, _string(placeholder))
        else:
            _pt_entry_set_placeholder(self._ptr, _string(placeholder))
        return self

    def set_password(self, password: bool) -> "Widget":
        _pt_entry_set_password(self._ptr, bool(password))
        return self

    def on_click(self, callback: Callable[[], Any]) -> "Widget":
        def dispatch(_widget: int, _data: int) -> None:
            callback()

        trampoline = _CLICK(dispatch)
        self._keep(trampoline)
        _pt_button_set_on_click(self._ptr, trampoline, None)
        return self

    def on_submit(self, callback: Callable[[], Any]) -> "Widget":
        def dispatch(_widget: int, _data: int) -> None:
            callback()

        trampoline = _CLICK(dispatch)
        self._keep(trampoline)
        _pt_entry_set_on_submit(self._ptr, trampoline, None)
        return self

    def on_change(self, callback: Callable[[str], Any]) -> "Widget":
        def dispatch(_widget: int, text: Optional[bytes], _data: int) -> None:
            callback(_text_out(text))

        trampoline = _TEXT(dispatch)
        self._keep(trampoline)
        if self.kind == _widget_text_area:
            _pt_text_area_set_on_change(self._ptr, trampoline, None)
        else:
            _pt_entry_set_on_change(self._ptr, trampoline, None)
        return self

    def on_bind_row(
        self, callback: Callable[["Widget", int], Any]
    ) -> "Widget":
        def dispatch(_list: int, row: int, index: int, _data: int) -> None:
            callback(Widget._wrap(row), int(index))

        trampoline = _BIND(dispatch)
        self._keep(trampoline)
        _pt_list_view_set_on_bind_row(self._ptr, trampoline, None)
        return self

    def on_row_select(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_list: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_list_view_set_on_select(self._ptr, trampoline, None)
        return self

    def on_row_activate(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_list: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_list_view_set_on_activate(self._ptr, trampoline, None)
        return self

    def on_toggle(self, callback: Callable[[bool], Any]) -> "Widget":
        return self.on_checked(callback)

    def on_checked(self, callback: Callable[[bool], Any]) -> "Widget":
        def dispatch(_widget: int, value: float, _data: int) -> None:
            callback(value > 0.5)

        trampoline = _VALUE(dispatch)
        self._keep(trampoline)
        _pt_widget_set_on_checked(self._ptr, trampoline, None)
        return self

    def on_value_change(self, callback: Callable[[float], Any]) -> "Widget":
        def dispatch(_widget: int, value: float, _data: int) -> None:
            callback(float(value))

        trampoline = _VALUE(dispatch)
        self._keep(trampoline)
        _pt_slider_set_on_change(self._ptr, trampoline, None)
        return self

    def on_select(self, callback: Callable[[int], Any]) -> "Widget":
        def dispatch(_widget: int, index: int, _data: int) -> None:
            callback(int(index))

        trampoline = _INDEX(dispatch)
        self._keep(trampoline)
        _pt_segmented_set_on_change(self._ptr, trampoline, None)
        return self

    def __repr__(self) -> str:
        return f"<pt.Widget kind={self.kind} at 0x{self._ptr:x}>"


class DropOffer:
    """A non-owning handle to an incoming structured drop offer.

    An offer is only valid while enter_offer or drop_offer runs; streams
    opened with read() outlive the callback. Every accessor is NULL-safe, so a
    None-backed handle reports an empty offer instead of raising.
    """

    def __init__(
        self, pointer: Optional[int], widget: Optional["Widget"] = None
    ) -> None:
        self._ptr = int(pointer) if pointer else 0
        self._widget = widget

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def mime_count(self) -> int:
        return int(_pt_drop_offer_mime_count(self._ptr))

    def mime(self, index: int) -> Optional[str]:
        """The advertised MIME type at index, or None when out of range."""
        return _text_or_none(_pt_drop_offer_mime(self._ptr, int(index)))

    def has_mime(self, mime_type: str) -> bool:
        return bool(_pt_drop_offer_has_mime(self._ptr, _string(mime_type)))

    @property
    def selected_mime(self) -> Optional[str]:
        """The MIME type enter_offer accepted, or None while hovering."""
        return _text_or_none(_pt_drop_offer_selected_mime(self._ptr))

    @property
    def actions(self) -> int:
        """Bit mask of the drag_action values advertised by the source."""
        return int(_pt_drop_offer_actions(self._ptr))

    def read(
        self,
        mime_type: str,
        max_bytes: int = 0,
        callback: Optional[Callable[["Widget", "DropStream", int, bytes], Any]] = None,
    ) -> Optional["DropStream"]:
        """Starts an asynchronous read of one offered MIME type.

        callback receives the target widget, the stream, a drop_stream state
        and the next raw bytes; chunks are never decoded, so concatenate them
        for text formats. max_bytes of zero means unlimited. Returns None when
        the offer is finished, the MIME type is unknown, no read backend exists
        or no callback was given.
        """
        if callback is None:
            return None

        def dispatch(
            _widget: Any,
            stream: int,
            state: int,
            data: Optional[int],
            size: int,
            _data: Any,
        ) -> None:
            handle = DropStream._wrap(stream)
            status = int(state)
            try:
                callback(handle._widget, handle, status, _bytes_at(data, int(size)))
            finally:
                if status != drop_stream_data:
                    handle._detach()

        trampoline = _DROP_STREAM(dispatch)
        pointer = _pt_drop_offer_read(
            self._ptr, _string(mime_type), int(max_bytes), trampoline, None
        )
        if not pointer:
            return None
        handle = DropStream(pointer, self._widget)
        handle._callbacks.append(trampoline)
        _drop_streams[handle._ptr] = handle
        return handle

    def __repr__(self) -> str:
        return (
            f"<pt.DropOffer mimes={self.mime_count} "
            f"actions={self.actions} at 0x{self._ptr:x}>"
        )


class DropStream:
    """A non-owning handle to one asynchronous read started from an offer.

    Pt releases the native stream after its final done/cancelled/error
    callback. The handle then forgets the pointer so later reads of mime, size
    and active stay NULL-safe rather than touching freed memory.
    """

    def __init__(
        self, pointer: Optional[int], widget: Optional["Widget"] = None
    ) -> None:
        self._ptr = int(pointer) if pointer else 0
        self._widget = widget
        self._callbacks: list[Any] = []

    @classmethod
    def _wrap(cls, pointer: Optional[int]) -> "DropStream":
        key = int(pointer) if pointer else 0
        existing = _drop_streams.get(key)
        if existing is not None:
            return existing
        return cls(pointer)

    def _detach(self) -> None:
        if self._ptr:
            _drop_streams.pop(self._ptr, None)
            self._ptr = 0

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def mime(self) -> Optional[str]:
        return _text_or_none(_pt_drop_stream_mime(self._ptr))

    @property
    def size(self) -> int:
        """Bytes delivered so far."""
        return int(_pt_drop_stream_size(self._ptr))

    @property
    def active(self) -> bool:
        return bool(_pt_drop_stream_active(self._ptr))

    def cancel(self) -> "DropStream":
        """Cancels the read, delivering drop_stream_cancelled before returning."""
        _pt_drop_stream_cancel(self._ptr)
        return self

    def __repr__(self) -> str:
        return f"<pt.DropStream active={self.active} at 0x{self._ptr:x}>"


class TreeNode:
    """A non-owning handle to a node inside a pt.tree widget.

    Node handles stay valid until the node, an ancestor or the tree is
    cleared. Compare and hash by pointer so nodes remain usable as dict keys.
    """

    def __init__(self, pointer: Optional[int]) -> None:
        if not pointer:
            raise RuntimeError("Pt returned a null tree node")
        self._ptr = int(pointer)

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def text(self) -> str:
        return _text_out(_pt_tree_node_get_text(self._ptr))

    @text.setter
    def text(self, value: str) -> None:
        _pt_tree_node_set_text(self._ptr, _string(value))

    @property
    def expanded(self) -> bool:
        return bool(_pt_tree_node_get_expanded(self._ptr))

    @expanded.setter
    def expanded(self, value: bool) -> None:
        _pt_tree_node_set_expanded(self._ptr, bool(value))

    @property
    def has_children(self) -> bool:
        return bool(_pt_tree_node_has_children(self._ptr))

    @property
    def depth(self) -> int:
        return int(_pt_tree_node_get_depth(self._ptr))

    @property
    def parent(self) -> Optional["TreeNode"]:
        pointer = _pt_tree_node_parent(self._ptr)
        return TreeNode(pointer) if pointer else None

    @property
    def child_count(self) -> int:
        return int(_pt_tree_node_get_child_count(self._ptr))

    @property
    def children(self) -> list["TreeNode"]:
        return [
            TreeNode(_pt_tree_node_get_child(self._ptr, index))
            for index in range(self.child_count)
        ]

    @property
    def data(self) -> Optional[int]:
        return _pt_tree_node_get_data(self._ptr)

    @data.setter
    def data(self, value: Optional[int]) -> None:
        _pt_tree_node_set_data(self._ptr, value)

    def child(self, index: int) -> Optional["TreeNode"]:
        pointer = _pt_tree_node_get_child(self._ptr, int(index))
        return TreeNode(pointer) if pointer else None

    def set_expanded(self, expanded: bool = True) -> "TreeNode":
        self.expanded = expanded
        return self

    def __eq__(self, other: object) -> bool:
        return isinstance(other, TreeNode) and other._ptr == self._ptr

    def __hash__(self) -> int:
        return self._ptr

    def __repr__(self) -> str:
        return f"<pt.TreeNode {self.text!r} at 0x{self._ptr:x}>"


def _drag_entry(item: Any) -> DragData:
    """Coerces one begin_drag entry into the native pt_drag_data layout."""
    if isinstance(item, DragData):
        return item
    if isinstance(item, dict):
        mime_type = item.get("mime_type")
        payload = item.get("data")
        size = int(item.get("size", 0))
    else:
        parts = tuple(item)
        if len(parts) == 2:
            mime_type, payload = parts
            size = 0
        elif len(parts) == 3:
            mime_type, payload, size = parts
            size = int(size)
        else:
            raise TypeError("drag entry must be (mime_type, data[, size])")
    if not isinstance(mime_type, str):
        raise TypeError("drag MIME type must be a string")
    if isinstance(payload, str):
        payload = _string(payload)
    elif payload is not None and not isinstance(payload, bytes):
        payload = bytes(payload)
    return DragData(_string(mime_type), payload, size)


class Window:
    """A native Pt window. Keeps callback trampolines alive."""

    def __init__(self, app: "App", title: str = "Pt", width: int = 640, height: int = 420) -> None:
        pointer = _pt_window_create(app._ptr, _string(title), int(width), int(height))
        if not pointer:
            raise RuntimeError(_error_message())
        self._ptr = int(pointer)
        self._callbacks: list[Any] = []
        self._root: Optional[Widget] = None
        self._closed = False
        self._last_close_allowed = True
        self._app = app
        _windows[self._ptr] = self

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def root(self) -> Widget:
        if self._closed:
            raise RuntimeError("the Pt window has been closed")
        if self._root is None:
            self._root = Widget(_pt_window_root(self._ptr))
        return self._root

    @property
    def is_open(self) -> bool:
        return not self._closed and bool(_pt_window_is_open(self._ptr))

    @property
    def width(self) -> int:
        return int(_pt_window_get_width(self._ptr))

    @property
    def height(self) -> int:
        return int(_pt_window_get_height(self._ptr))

    @property
    def scale(self) -> float:
        return float(_pt_window_get_scale(self._ptr))

    @property
    def clipboard(self) -> str:
        pointer = _pt_window_clipboard_get(self._ptr)
        if not pointer:
            return ""
        try:
            return _ctypes.string_at(pointer).decode("utf-8")
        finally:
            _pt_free(pointer)

    @clipboard.setter
    def clipboard(self, text: str) -> None:
        _pt_window_clipboard_set(self._ptr, _string(text))

    def set_clipboard(self, text: str) -> "Window":
        self.clipboard = text
        return self

    def show(self) -> "Window":
        _pt_window_show(self._ptr)
        return self

    def hide(self) -> "Window":
        _pt_window_hide(self._ptr)
        return self

    def close(self) -> None:
        if self._closed:
            return
        self._last_close_allowed = True
        _pt_window_close(self._ptr)
        if self._last_close_allowed:
            self._closed = True

    def set_title(self, title: str) -> "Window":
        _pt_window_set_title(self._ptr, _string(title))
        return self

    def set_size(self, width: int, height: int) -> "Window":
        _pt_window_set_size(self._ptr, int(width), int(height))
        return self

    def set_min_size(self, width: int, height: int) -> "Window":
        _pt_window_set_min_size(self._ptr, int(width), int(height))
        return self

    def set_resizable(self, resizable: bool) -> "Window":
        _pt_window_set_resizable(self._ptr, bool(resizable))
        return self

    def set_decorated(self, decorated: bool) -> "Window":
        _pt_window_set_decorated(self._ptr, bool(decorated))
        return self

    @property
    def popup_policy(self) -> int:
        return int(_pt_window_get_popup_policy(self._ptr))

    @popup_policy.setter
    def popup_policy(self, policy: int) -> None:
        _pt_window_set_popup_policy(self._ptr, int(policy))

    @property
    def drag_active(self) -> bool:
        return bool(_pt_window_get_drag_active(self._ptr))

    @property
    def drag_result(self) -> int:
        """Action performed by the last finished drag, or drag_action_none."""
        return int(_pt_window_get_drag_result(self._ptr))

    @property
    def drag_timeout(self) -> int:
        return int(_pt_window_get_drag_timeout(self._ptr))

    @drag_timeout.setter
    def drag_timeout(self, milliseconds: int) -> None:
        _pt_window_set_drag_timeout(self._ptr, int(milliseconds))

    def cancel_drag(self) -> "Window":
        _pt_window_cancel_drag(self._ptr)
        return self

    def request_redraw(self) -> None:
        _pt_window_request_redraw(self._ptr)

    def focus(self, widget: Widget) -> None:
        _pt_window_focus(self._ptr, widget._ptr)

    def set_theme_color(self, token: int, color: int) -> "Window":
        """Overrides one semantic color role for this window only."""
        _pt_window_set_theme_color(self._ptr, int(token), int(color))
        return self

    def get_theme_color(self, token: int) -> int:
        return int(_pt_window_get_theme_color(self._ptr, int(token)))

    def set_theme_metric(self, metric: int, value: float) -> "Window":
        """Overrides one geometric token (radius, focus ring, elevation...)."""
        _pt_window_set_theme_metric(self._ptr, int(metric), float(value))
        return self

    def get_theme_metric(self, metric: int) -> float:
        return float(_pt_window_get_theme_metric(self._ptr, int(metric)))

    def clear_theme_overrides(self) -> "Window":
        _pt_window_clear_theme_overrides(self._ptr)
        return self

    def on_close(self, callback: Callable[["Window"], Any]) -> "Window":
        def dispatch(_window: int, _data: int) -> bool:
            allowed = callback(self) is not False
            self._last_close_allowed = allowed
            return allowed

        trampoline = _CLOSE(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_window_set_close_callback(self._ptr, trampoline, None)
        return self

    def on_resize(self, callback: Callable[["Window", int, int], Any]) -> "Window":
        def dispatch(_window: int, width: int, height: int, _data: int) -> None:
            callback(self, int(width), int(height))

        trampoline = _RESIZE(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_window_set_resize_callback(self._ptr, trampoline, None)
        return self

    def on_key(self, callback: Callable[["Window", int, int], Any]) -> "Window":
        def dispatch(_window: int, key: int, modifiers: int, _data: int) -> bool:
            return bool(callback(self, int(key), int(modifiers)))

        trampoline = _KEY(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_window_set_key_callback(self._ptr, trampoline, None)
        return self

    def on_gesture(self, callback: Callable[["Window", GestureEvent], Any]) -> "Window":
        def dispatch(_window: int, event: Any, _data: int) -> None:
            source = event.contents
            copy = GestureEvent(
                source.kind,
                source.phase,
                source.fingers,
                source.x,
                source.y,
                source.dx,
                source.dy,
                source.scale,
                source.rotation,
                source.target,
            )
            callback(self, copy)

        trampoline = _GESTURE(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_window_set_gesture_callback(self._ptr, trampoline, None)
        return self

    def set_long_press_delay(self, milliseconds: int) -> "Window":
        _pt_window_set_long_press_delay(self._ptr, int(milliseconds))
        return self

    def set_momentum_scrolling(self, enabled: bool) -> "Window":
        _pt_window_set_momentum_scrolling(self._ptr, bool(enabled))
        return self

    @property
    def momentum_scrolling(self) -> bool:
        return bool(_pt_window_get_momentum_scrolling(self._ptr))

    @momentum_scrolling.setter
    def momentum_scrolling(self, enabled: bool) -> None:
        _pt_window_set_momentum_scrolling(self._ptr, bool(enabled))

    def begin_drag(self, types: list[Any]) -> bool:
        """Starts a structured drag offering several MIME types.

        Each entry is a (mime_type, data) pair, a (mime_type, data, size)
        triple, a mapping with mime_type/data and an optional size, or a
        DragData. A size of zero means NUL-terminated text. Needs the implicit
        grab of an active pointer press, so it returns False unless the
        pointer was recently pressed.
        """
        entries = [_drag_entry(item) for item in types]
        if not entries:
            return False
        array = (DragData * len(entries))(*entries)
        return bool(_pt_window_begin_drag(self._ptr, array, len(entries)))

    def set_drag_finished_callback(
        self, callback: Optional[Callable[["Window", bool, int], Any]]
    ) -> "Window":
        """Receives (window, dropped, action) once per completed drag.

        dropped is False when the drag was cancelled, otherwise action is the
        drag_action the destination performed. Passing None clears it. The
        trampoline is kept on this window and rooted so Pt can call it from the
        event loop.
        """
        if callback is None:
            _pt_window_set_drag_finished_callback(
                self._ptr, _NULL_DRAG_FINISHED, None
            )
            return self

        def dispatch(_window: int, dropped: bool, action: int, _data: int) -> None:
            callback(self, bool(dropped), int(action))

        trampoline = _DRAG_FINISHED(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_window_set_drag_finished_callback(self._ptr, trampoline, None)
        return self

    def set_drag_image(
        self, pixels: list[int], width: int, height: int
    ) -> "Window":
        width = int(width)
        height = int(height)
        if width <= 0 or height <= 0:
            _pt_window_clear_drag_image(self._ptr)
            return self
        if len(pixels) < width * height:
            raise ValueError("drag image has fewer pixels than width * height")
        array = (_c_uint32 * (width * height))(*[int(pixel) for pixel in pixels])
        _pt_window_set_drag_image(self._ptr, array, width, height)
        return self

    def clear_drag_image(self) -> "Window":
        _pt_window_clear_drag_image(self._ptr)
        return self

    def on_stylus(
        self, callback: Callable[["Window", StylusEvent], Any]
    ) -> "Window":
        def dispatch(_window: int, event: Any, _data: int) -> None:
            source = event.contents
            copy = StylusEvent(
                source.phase,
                source.tool,
                source.x,
                source.y,
                source.pressure,
                source.distance,
                source.tilt_x,
                source.tilt_y,
                source.rotation,
            )
            callback(self, copy)

        trampoline = _STYLUS(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_window_set_stylus_callback(self._ptr, trampoline, None)
        return self

    def show_toast(
        self, text: str, kind: int = toast_info, duration_ms: int = 3000
    ) -> "Toast":
        pointer = _pt_window_show_toast(
            self._ptr, _string(text), int(kind), int(duration_ms)
        )
        if not pointer:
            raise RuntimeError("failed to create toast")
        return Toast(pointer, self)

    def __repr__(self) -> str:
        return f"<pt.Window at 0x{self._ptr:x}>"


class Menu:
    """A popup menu composited inside a window.

    Callbacks run after the menu has closed, so they may open another overlay.
    Call destroy() when the menu is no longer needed.
    """

    def __init__(self, window: Window) -> None:
        pointer = _pt_menu_create(window._ptr)
        if not pointer:
            raise RuntimeError(_error_message())
        self._ptr = int(pointer)
        self._window = window
        self._callbacks: list[Any] = []

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def is_open(self) -> bool:
        return bool(self._ptr) and bool(_pt_menu_is_open(self._ptr))

    def add_item(
        self,
        label: str,
        shortcut: str = "",
        callback: Optional[Callable[[], Any]] = None,
    ) -> "Menu":
        if not self._ptr:
            return self
        trampoline = _NULL_CLICK
        if callback is not None:
            def dispatch(_widget: int, _data: int) -> None:
                callback()

            trampoline = _CLICK(dispatch)
            self._callbacks.append(trampoline)
            _callback_roots.append(trampoline)
        _pt_menu_add_item(
            self._ptr,
            _string(label),
            _string(shortcut) if shortcut else None,
            trampoline,
            None,
        )
        return self

    def add_separator(self) -> "Menu":
        if self._ptr:
            _pt_menu_add_separator(self._ptr)
        return self

    def show_at(self, x: float, y: float) -> "Menu":
        if self._ptr:
            _pt_menu_show_at(self._ptr, float(x), float(y))
        return self

    def show_below(self, anchor: Widget) -> "Menu":
        if self._ptr:
            _pt_menu_show_below(self._ptr, anchor._ptr)
        return self

    def dismiss(self) -> "Menu":
        if self._ptr:
            _pt_menu_dismiss(self._ptr)
        return self

    def destroy(self) -> None:
        if self._ptr:
            _pt_menu_destroy(self._ptr)
            self._ptr = 0
            self._callbacks.clear()

    def __repr__(self) -> str:
        return f"<pt.Menu open={self.is_open} at 0x{self._ptr:x}>"


class Dialog:
    """A modal dialog composited inside a window."""

    def __init__(self, window: Window, title: str = "", message: str = "") -> None:
        pointer = _pt_dialog_create(window._ptr, _string(title), _string(message))
        if not pointer:
            raise RuntimeError(_error_message())
        self._ptr = int(pointer)
        self._window = window
        self._callbacks: list[Any] = []

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def is_open(self) -> bool:
        return bool(self._ptr) and bool(_pt_dialog_is_open(self._ptr))

    def set_title(self, title: str) -> "Dialog":
        if self._ptr:
            _pt_dialog_set_title(self._ptr, _string(title))
        return self

    def set_message(self, message: str) -> "Dialog":
        if self._ptr:
            _pt_dialog_set_message(self._ptr, _string(message))
        return self

    def add_action(
        self,
        label: str,
        primary: bool = False,
        callback: Optional[Callable[[], Any]] = None,
    ) -> "Dialog":
        if not self._ptr:
            return self
        trampoline = _NULL_CLICK
        if callback is not None:
            def dispatch(_widget: int, _data: int) -> None:
                callback()

            trampoline = _CLICK(dispatch)
            self._callbacks.append(trampoline)
            _callback_roots.append(trampoline)
        _pt_dialog_add_action(
            self._ptr, _string(label), bool(primary), trampoline, None
        )
        return self

    def on_close(self, callback: Callable[["Dialog"], Any]) -> "Dialog":
        if not self._ptr:
            return self

        def dispatch(_dialog: int, _data: int) -> None:
            callback(self)

        trampoline = _DIALOG(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_dialog_set_on_close(self._ptr, trampoline, None)
        return self

    def show(self) -> "Dialog":
        if self._ptr:
            _pt_dialog_show(self._ptr)
        return self

    def close(self) -> "Dialog":
        if self._ptr:
            _pt_dialog_close(self._ptr)
        return self

    def destroy(self) -> None:
        if self._ptr:
            _pt_dialog_destroy(self._ptr)
            self._ptr = 0
            self._callbacks.clear()

    def __repr__(self) -> str:
        return f"<pt.Dialog open={self.is_open} at 0x{self._ptr:x}>"


class Toast:
    """A non-owning handle to a toast owned by its window."""

    def __init__(self, pointer: int, window: Window) -> None:
        self._ptr = int(pointer)
        self._window = window

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def visible(self) -> bool:
        return bool(self._ptr) and bool(_pt_toast_is_visible(self._ptr))

    def dismiss(self) -> "Toast":
        if self._ptr:
            _pt_toast_dismiss(self._ptr)
        return self

    def __repr__(self) -> str:
        return f"<pt.Toast visible={self.visible} at 0x{self._ptr:x}>"


class App:
    """Owns the native application, event loop and timers."""

    def __init__(self, app_id: str = "pt.application") -> None:
        pointer = _pt_app_create(_string(app_id))
        if not pointer:
            raise RuntimeError(_error_message())
        self._ptr = int(pointer)
        self._timers: dict[int, Any] = {}
        self._callbacks: list[Any] = []

    @property
    def raw(self) -> int:
        return self._ptr

    @property
    def running(self) -> bool:
        return bool(_pt_app_is_running(self._ptr))

    def run(self) -> int:
        return int(_pt_app_run(self._ptr))

    def run_for(self, timeout_ms: int) -> int:
        return int(_pt_app_run_for(self._ptr, int(timeout_ms)))

    def quit(self) -> None:
        _pt_app_quit(self._ptr)

    def destroy(self) -> None:
        if self._ptr:
            for window in _windows.values():
                if window._app is self:
                    window._closed = True
            _pt_app_destroy(self._ptr)
            self._ptr = 0
            _widgets.clear()
            _windows.clear()
            _drop_streams.clear()

    def add_timeout(self, milliseconds: int, callback: Callable[[], Any]) -> int:
        timer_id_holder: list[int] = [0]

        def dispatch(_app: int, _data: int) -> None:
            self._timers.pop(timer_id_holder[0], None)
            callback()

        trampoline = _TIMER(dispatch)
        timer_id = int(
            _pt_app_add_timeout(self._ptr, int(milliseconds), trampoline, None)
        )
        timer_id_holder[0] = timer_id
        self._timers[timer_id] = trampoline
        return timer_id

    def cancel_timeout(self, timer_id: int) -> None:
        _pt_app_cancel_timeout(self._ptr, int(timer_id))
        self._timers.pop(int(timer_id), None)

    @property
    def color_scheme(self) -> int:
        return int(_pt_app_get_color_scheme(self._ptr))

    @color_scheme.setter
    def color_scheme(self, scheme: int) -> None:
        _pt_app_set_color_scheme(self._ptr, int(scheme))

    @property
    def accent_color(self) -> int:
        return int(_pt_app_get_theme_color(self._ptr, theme_accent))

    @accent_color.setter
    def accent_color(self, color: int) -> None:
        _pt_app_set_accent_color(self._ptr, int(color))

    def set_theme_color(self, token: int, color: int) -> None:
        _pt_app_set_theme_color(self._ptr, int(token), int(color))

    def get_theme_color(self, token: int) -> int:
        return int(_pt_app_get_theme_color(self._ptr, int(token)))

    def set_theme_metric(self, metric: int, value: float) -> None:
        _pt_app_set_theme_metric(self._ptr, int(metric), float(value))

    def get_theme_metric(self, metric: int) -> float:
        return float(_pt_app_get_theme_metric(self._ptr, int(metric)))

    def set_font_family(self, family: str) -> None:
        _pt_app_set_font_family(self._ptr, _string(family))

    @property
    def reduced_motion(self) -> bool:
        return bool(_pt_app_get_reduced_motion(self._ptr))

    @reduced_motion.setter
    def reduced_motion(self, reduced: bool) -> None:
        _pt_app_set_reduced_motion(self._ptr, bool(reduced))

    def set_quit_on_last_window_close(self, enabled: bool) -> None:
        _pt_app_set_quit_on_last_window_close(self._ptr, bool(enabled))

    def enable_a11y(self) -> bool:
        return bool(_pt_app_enable_a11y(self._ptr))

    def disable_a11y(self) -> None:
        _pt_app_disable_a11y(self._ptr)

    @property
    def a11y_status(self) -> int:
        return int(_pt_app_get_a11y_status(self._ptr))

    def on_a11y_event(
        self,
        callback: Callable[[Optional[Window], Optional[Widget], int], Any],
    ) -> "App":
        def dispatch(window_pointer: int, widget_pointer: int, event: int,
                     _data: int) -> None:
            window = _windows.get(int(window_pointer)) if window_pointer else None
            widget = Widget._wrap(widget_pointer)
            callback(window, widget, int(event))

        trampoline = _A11Y(dispatch)
        self._callbacks.append(trampoline)
        _callback_roots.append(trampoline)
        _pt_app_set_a11y_callback(self._ptr, trampoline, None)
        return self

    def __repr__(self) -> str:
        return f"<pt.App at 0x{self._ptr:x}>"


def _create(pointer: Optional[int]) -> Widget:
    if not pointer:
        raise RuntimeError("failed to create widget")
    return Widget(pointer)


def box(parent: Optional[Widget], direction: int = direction_column) -> Widget:
    return _create(_pt_box_create(parent._ptr if parent else None, direction))


def row(parent: Optional[Widget]) -> Widget:
    return _create(_pt_row_create(parent._ptr if parent else None))


def column(parent: Optional[Widget]) -> Widget:
    return _create(_pt_column_create(parent._ptr if parent else None))


def label(parent: Optional[Widget], text: str) -> Widget:
    return _create(_pt_label_create(parent._ptr if parent else None, _string(text)))


def button(parent: Optional[Widget], text: str) -> Widget:
    return _create(_pt_button_create(parent._ptr if parent else None, _string(text)))


def entry(parent: Optional[Widget], placeholder: str = "") -> Widget:
    return _create(_pt_entry_create(parent._ptr if parent else None, _string(placeholder)))


def text_area(parent: Optional[Widget], placeholder: str = "") -> Widget:
    return _create(
        _pt_text_area_create(parent._ptr if parent else None, _string(placeholder))
    )


def toggle(parent: Optional[Widget]) -> Widget:
    return _create(_pt_toggle_create(parent._ptr if parent else None))


def checkbox(parent: Optional[Widget], text: str) -> Widget:
    return _create(_pt_checkbox_create(parent._ptr if parent else None, _string(text)))


def radio(parent: Optional[Widget], text: str) -> Widget:
    return _create(_pt_radio_create(parent._ptr if parent else None, _string(text)))


def slider(parent: Optional[Widget]) -> Widget:
    return _create(_pt_slider_create(parent._ptr if parent else None))


def progress(parent: Optional[Widget]) -> Widget:
    return _create(_pt_progress_create(parent._ptr if parent else None))


def spinner(parent: Optional[Widget]) -> Widget:
    return _create(_pt_spinner_create(parent._ptr if parent else None))


def segmented(parent: Optional[Widget], labels: list[str]) -> Widget:
    encoded = [_string(label) for label in labels]
    array = (_c_char_p * len(encoded))(*encoded)
    return _create(
        _pt_segmented_create(parent._ptr if parent else None, array, len(encoded))
    )


def scroll(parent: Optional[Widget]) -> Widget:
    return _create(_pt_scroll_create(parent._ptr if parent else None))


def wrap(parent: Optional[Widget]) -> Widget:
    return _create(_pt_wrap_create(parent._ptr if parent else None))


def grid(parent: Optional[Widget], columns: int = 2) -> Widget:
    return _create(
        _pt_grid_create(parent._ptr if parent else None, int(columns))
    )


def list_view(parent: Optional[Widget]) -> Widget:
    return _create(_pt_list_view_create(parent._ptr if parent else None))


def icon(parent: Optional[Widget], glyph: int) -> Widget:
    return _create(_pt_icon_create(parent._ptr if parent else None, int(glyph)))


def icon_button(parent: Optional[Widget], glyph: int, accessible_name: str) -> Widget:
    return _create(
        _pt_icon_button_create(
            parent._ptr if parent else None, int(glyph), _string(accessible_name)
        )
    )


def table(parent: Optional[Widget]) -> Widget:
    return _create(_pt_table_create(parent._ptr if parent else None))


def tree(parent: Optional[Widget]) -> Widget:
    return _create(_pt_tree_create(parent._ptr if parent else None))


def combobox(parent: Optional[Widget]) -> Widget:
    return _create(_pt_combobox_create(parent._ptr if parent else None))


def tabs(parent: Optional[Widget]) -> Widget:
    return _create(_pt_tabs_create(parent._ptr if parent else None))


def sidebar(parent: Optional[Widget]) -> Widget:
    return _create(_pt_sidebar_create(parent._ptr if parent else None))


def divider(parent: Optional[Widget]) -> Widget:
    return _create(_pt_divider_create(parent._ptr if parent else None))


def card(parent: Optional[Widget]) -> Widget:
    return _create(_pt_card_create(parent._ptr if parent else None))


def chip(parent: Optional[Widget], text: str) -> Widget:
    return _create(_pt_chip_create(parent._ptr if parent else None, _string(text)))


def skeleton(parent: Optional[Widget]) -> Widget:
    return _create(_pt_skeleton_create(parent._ptr if parent else None))


def spacer(parent: Optional[Widget]) -> Widget:
    return _create(_pt_spacer_create(parent._ptr if parent else None))

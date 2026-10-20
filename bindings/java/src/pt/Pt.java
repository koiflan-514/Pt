/*
 * Pt - Java bindings through the Foreign Function & Memory API (JDK 22+).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Method names follow the project-wide snake_case convention. Handle objects
 * (MemorySegment) are non-owning; pt.Pt.app_destroy frees the native tree.
 * Callback stubs live in the Arena supplied by the caller, so keep it open for
 * as long as the window can emit events.
 */

package pt;

import java.lang.foreign.AddressLayout;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

@SuppressWarnings("restricted")
public final class Pt {
    private Pt() {}

    /* ---------- callbacks ---------- */

    @FunctionalInterface
    public interface ClickHandler {
        void handle(MemorySegment widget, MemorySegment data);
    }

    @FunctionalInterface
    public interface TextHandler {
        void handle(MemorySegment widget, MemorySegment text, MemorySegment data);
    }

    @FunctionalInterface
    public interface ValueHandler {
        void handle(MemorySegment widget, float value, MemorySegment data);
    }

    @FunctionalInterface
    public interface CloseHandler {
        boolean handle(MemorySegment window, MemorySegment data);
    }

    @FunctionalInterface
    public interface KeyHandler {
        boolean handle(MemorySegment window, int key, int modifiers, MemorySegment data);
    }

    @FunctionalInterface
    public interface TimerHandler {
        void handle(MemorySegment app, MemorySegment data);
    }

    @FunctionalInterface
    public interface IndexHandler {
        void handle(MemorySegment widget, int index, MemorySegment data);
    }

    public interface ListBindHandler {
        void handle(MemorySegment list, MemorySegment row, int index, MemorySegment data);
    }

    @FunctionalInterface
    public interface TableBindHandler {
        void handle(MemorySegment table, MemorySegment cell, int row, int column,
                MemorySegment data);
    }

    @FunctionalInterface
    public interface SortHandler {
        void handle(MemorySegment table, int column, int direction, MemorySegment data);
    }

    @FunctionalInterface
    public interface SortColumnsHandler {
        void handle(MemorySegment table, int count, MemorySegment columns,
                MemorySegment directions, MemorySegment data);
    }

    @FunctionalInterface
    public interface RowKeyHandler {
        long handle(MemorySegment view, int row, MemorySegment data);
    }

    @FunctionalInterface
    public interface RowTextHandler {
        String handle(MemorySegment view, int row, MemorySegment data);
    }

    @FunctionalInterface
    public interface TreeNodeHandler {
        void handle(MemorySegment tree, MemorySegment node, MemorySegment data);
    }

    @FunctionalInterface
    public interface StylusHandler {
        void handle(MemorySegment window, StylusEvent event, MemorySegment data);
    }

    @FunctionalInterface
    public interface A11yHandler {
        void handle(MemorySegment window, MemorySegment widget, int event, MemorySegment data);
    }

    @FunctionalInterface
    public interface ResizeHandler {
        void handle(MemorySegment window, int width, int height, MemorySegment data);
    }

    @FunctionalInterface
    public interface DialogHandler {
        void handle(MemorySegment dialog, MemorySegment data);
    }

    @FunctionalInterface
    public interface GestureHandler {
        void handle(MemorySegment window, GestureEvent event, MemorySegment data);
    }

    @FunctionalInterface
    public interface DropEnterHandler {
        boolean handle(MemorySegment widget, String mime_type, MemorySegment data);
    }

    @FunctionalInterface
    public interface DropMotionHandler {
        void handle(MemorySegment widget, float x, float y, MemorySegment data);
    }

    @FunctionalInterface
    public interface DropLeaveHandler {
        void handle(MemorySegment widget, MemorySegment data);
    }

    @FunctionalInterface
    public interface DropHandler {
        void handle(MemorySegment widget, String mime_type, String text, float x, float y,
                MemorySegment data);
    }

    @FunctionalInterface
    public interface DropOfferEnterHandler {
        String handle(MemorySegment widget, MemorySegment offer, MemorySegment data);
    }

    @FunctionalInterface
    public interface DropOfferHandler {
        void handle(MemorySegment widget, MemorySegment offer, String mime_type, float x, float y,
                MemorySegment data);
    }

    @FunctionalInterface
    public interface DropStreamHandler {
        void handle(MemorySegment widget, MemorySegment stream, int state, byte[] chunk,
                String mime_type, MemorySegment data);
    }

    @FunctionalInterface
    public interface DragFinishedHandler {
        void handle(MemorySegment window, boolean dropped, int action, MemorySegment data);
    }

    @FunctionalInterface
    public interface TableEditedHandler {
        void handle(MemorySegment table, int row, int column, String text, MemorySegment data);
    }

    @FunctionalInterface
    public interface TableReorderHandler {
        void handle(MemorySegment table, int from, int to, MemorySegment data);
    }

    @FunctionalInterface
    public interface TableRowDragHandler {
        void handle(MemorySegment table, int row, MemorySegment data);
    }

    @FunctionalInterface
    public interface TreeRenameHandler {
        void handle(MemorySegment tree, MemorySegment node, String text, MemorySegment data);
    }

    public record A11yText(String text, int caret, int selection_start, int selection_end) {}

    public record GestureEvent(int kind, int phase, int fingers, float x, float y, float dx,
            float dy, float scale, float rotation, MemorySegment target) {
        private static GestureEvent read(MemorySegment event) {
            MemorySegment view = event.reinterpret(48);
            return new GestureEvent(view.get(ValueLayout.JAVA_INT, 0),
                    view.get(ValueLayout.JAVA_INT, 4), view.get(ValueLayout.JAVA_INT, 8),
                    view.get(ValueLayout.JAVA_FLOAT, 12), view.get(ValueLayout.JAVA_FLOAT, 16),
                    view.get(ValueLayout.JAVA_FLOAT, 20), view.get(ValueLayout.JAVA_FLOAT, 24),
                    view.get(ValueLayout.JAVA_FLOAT, 28), view.get(ValueLayout.JAVA_FLOAT, 32),
                    view.get(ValueLayout.ADDRESS, 40));
        }
    }

    public record StylusEvent(int phase, int tool, float x, float y, float pressure,
            float distance, float tilt_x, float tilt_y, float rotation) {
        private static StylusEvent read(MemorySegment event) {
            MemorySegment view = event.reinterpret(36);
            return new StylusEvent(view.get(ValueLayout.JAVA_INT, 0),
                    view.get(ValueLayout.JAVA_INT, 4), view.get(ValueLayout.JAVA_FLOAT, 8),
                    view.get(ValueLayout.JAVA_FLOAT, 12), view.get(ValueLayout.JAVA_FLOAT, 16),
                    view.get(ValueLayout.JAVA_FLOAT, 20), view.get(ValueLayout.JAVA_FLOAT, 24),
                    view.get(ValueLayout.JAVA_FLOAT, 28), view.get(ValueLayout.JAVA_FLOAT, 32));
        }
    }

    public record TextSpan(int start, int length, int weight, int slant, int color, int flags,
            String family, float size) {
        public TextSpan(int start, int length, int weight, int slant, int color, int flags) {
            this(start, length, weight, slant, color, flags, "", 0.0f);
        }
    }

    /** One MIME entry of a structured drag; size zero means NUL-terminated data. */
    public record DragData(String mime_type, String data, long size) {}

    /* ---------- constants ---------- */

    public static final int color_scheme_dark = 0;
    public static final int color_scheme_light = 1;

    public static final int align_auto = 0;
    public static final int align_start = 1;
    public static final int align_center = 2;
    public static final int align_end = 3;
    public static final int align_stretch = 4;

    public static final int direction_row = 0;
    public static final int direction_column = 1;

    public static final int justify_start = 0;
    public static final int justify_center = 1;
    public static final int justify_end = 2;
    public static final int justify_space_between = 3;
    public static final int justify_space_around = 4;
    public static final int justify_space_evenly = 5;

    public static final int text_align_start = 0;
    public static final int text_align_center = 1;
    public static final int text_align_end = 2;

    public static final int sort_none = 0;
    public static final int sort_ascending = 1;
    public static final int sort_descending = 2;

    public static final int icon_none = 0;
    public static final int icon_chevron_right = 1;
    public static final int icon_chevron_down = 2;
    public static final int icon_chevron_left = 3;
    public static final int icon_chevron_up = 4;
    public static final int icon_search = 5;
    public static final int icon_menu = 6;
    public static final int icon_close = 7;
    public static final int icon_check = 8;
    public static final int icon_folder = 9;
    public static final int icon_document = 10;
    public static final int icon_plus = 11;
    public static final int icon_settings = 12;
    public static final int icon_home = 13;
    public static final int icon_bell = 14;
    public static final int icon_user = 15;
    public static final int icon_refresh = 16;
    public static final int icon_trash = 17;
    public static final int icon_arrow_right = 18;
    public static final int icon_arrow_left = 19;
    public static final int icon_info = 20;
    public static final int icon_warning = 21;

    public static final int font_weight_regular = 400;
    public static final int font_weight_medium = 500;
    public static final int font_weight_semibold = 600;
    public static final int font_weight_bold = 700;

    public static final int font_slant_upright = 0;
    public static final int font_slant_italic = 1;
    public static final int font_slant_oblique = 2;

    public static final int text_span_underline = 1 << 0;
    public static final int text_span_has_weight = 1 << 1;
    public static final int text_span_has_slant = 1 << 2;
    public static final int text_span_has_color = 1 << 3;
    public static final int text_span_has_family = 1 << 4;
    public static final int text_span_has_size = 1 << 5;

    public static final int input_purpose_normal = 0;
    public static final int input_purpose_email = 1;
    public static final int input_purpose_url = 2;
    public static final int input_purpose_phone = 3;
    public static final int input_purpose_number = 4;
    public static final int input_purpose_password = 5;
    public static final int input_purpose_terminal = 6;

    public static final int button_primary = 0;
    public static final int button_secondary = 1;
    public static final int button_ghost = 2;
    public static final int button_danger = 3;
    public static final int button_soft = 4;

    public static final int theme_background = 0;
    public static final int theme_surface = 1;
    public static final int theme_surface_raised = 2;
    public static final int theme_surface_hover = 3;
    public static final int theme_border = 4;
    public static final int theme_border_strong = 5;
    public static final int theme_text = 6;
    public static final int theme_text_muted = 7;
    public static final int theme_accent = 8;
    public static final int theme_accent_hover = 9;
    public static final int theme_accent_text = 10;
    public static final int theme_danger = 11;
    public static final int theme_success = 12;
    public static final int theme_warning = 13;
    public static final int theme_shadow = 14;
    public static final int theme_titlebar = 15;
    public static final int theme_scrollbar = 16;
    public static final int theme_focus = 17;
    public static final int theme_surface_sunken = 18;
    public static final int theme_control = 19;
    public static final int theme_control_hover = 20;
    public static final int theme_accent_soft = 21;
    public static final int theme_selection = 22;
    public static final int theme_track = 23;
    public static final int theme_surface_container = 24;
    public static final int theme_surface_container_high = 25;
    public static final int theme_outline = 26;
    public static final int theme_outline_variant = 27;
    public static final int theme_text_disabled = 28;
    public static final int theme_accent_pressed = 29;
    public static final int theme_control_pressed = 30;
    public static final int theme_danger_soft = 31;
    public static final int theme_success_soft = 32;
    public static final int theme_warning_soft = 33;
    public static final int theme_scrim = 34;
    public static final int theme_skeleton = 35;
    public static final int theme_skeleton_highlight = 36;
    public static final int theme_selection_text = 37;

    public static final int theme_metric_radius_small = 0;
    public static final int theme_metric_radius_medium = 1;
    public static final int theme_metric_radius_large = 2;
    public static final int theme_metric_radius_pill = 3;
    public static final int theme_metric_focus_width = 4;
    public static final int theme_metric_focus_offset = 5;
    public static final int theme_metric_elevation_low = 6;
    public static final int theme_metric_elevation_high = 7;
    public static final int theme_metric_chip_height = 8;
    public static final int theme_metric_control_height = 9;

    public static final int key_modifier_shift = 1;
    public static final int key_modifier_ctrl = 1 << 1;
    public static final int key_modifier_alt = 1 << 2;
    public static final int key_modifier_logo = 1 << 3;

    public static final int toast_info = 0;
    public static final int toast_success = 1;
    public static final int toast_warning = 2;
    public static final int toast_error = 3;

    public static final int gesture_kind_swipe = 0;
    public static final int gesture_kind_pinch = 1;
    public static final int gesture_kind_hold = 2;

    public static final int gesture_phase_begin = 0;
    public static final int gesture_phase_update = 1;
    public static final int gesture_phase_end = 2;
    public static final int gesture_phase_cancel = 3;

    public static final int stylus_tool_pen = 0;
    public static final int stylus_tool_eraser = 1;
    public static final int stylus_tool_brush = 2;
    public static final int stylus_tool_pencil = 3;
    public static final int stylus_tool_airbrush = 4;
    public static final int stylus_tool_unknown = 5;

    public static final int stylus_phase_enter = 0;
    public static final int stylus_phase_motion = 1;
    public static final int stylus_phase_down = 2;
    public static final int stylus_phase_up = 3;
    public static final int stylus_phase_leave = 4;

    public static final int drag_action_none = 0;
    public static final int drag_action_copy = 1;
    public static final int drag_action_move = 2;
    public static final int drag_action_ask = 4;

    public static final int drop_stream_data = 0;
    public static final int drop_stream_done = 1;
    public static final int drop_stream_cancelled = 2;
    public static final int drop_stream_error = 3;

    public static final int a11y_role_none = 0;
    public static final int a11y_role_generic = 1;
    public static final int a11y_role_panel = 2;
    public static final int a11y_role_label = 3;
    public static final int a11y_role_push_button = 4;
    public static final int a11y_role_text = 5;
    public static final int a11y_role_toggle_button = 6;
    public static final int a11y_role_switch = 7;
    public static final int a11y_role_slider = 8;
    public static final int a11y_role_scroll_pane = 9;
    public static final int a11y_role_separator = 10;
    public static final int a11y_role_check_box = 11;
    public static final int a11y_role_radio_button = 12;
    public static final int a11y_role_progress_bar = 13;
    public static final int a11y_role_spinner = 14;
    public static final int a11y_role_group = 15;
    public static final int a11y_role_menu = 16;
    public static final int a11y_role_menu_item = 17;
    public static final int a11y_role_dialog = 18;
    public static final int a11y_role_tooltip = 19;
    public static final int a11y_role_alert = 20;
    public static final int a11y_role_list = 21;
    public static final int a11y_role_list_item = 22;
    public static final int a11y_role_text_frame = 23;
    public static final int a11y_role_table = 24;
    public static final int a11y_role_table_cell = 25;
    public static final int a11y_role_table_row = 26;
    public static final int a11y_role_column_header = 27;
    public static final int a11y_role_tree = 28;
    public static final int a11y_role_tree_item = 29;
    public static final int a11y_role_tab_list = 30;
    public static final int a11y_role_tab = 31;
    public static final int a11y_role_tab_panel = 32;
    public static final int a11y_role_icon = 33;
    public static final int a11y_role_combo_box = 34;
    public static final int a11y_role_list_box = 35;

    public static final int easing_linear = 0;
    public static final int easing_in_quad = 1;
    public static final int easing_out_quad = 2;
    public static final int easing_in_out_quad = 3;
    public static final int easing_out_cubic = 4;
    public static final int easing_in_out_cubic = 5;
    public static final int easing_out_back = 6;

    public static final int list_index_none = -1;
    public static final int table_column_none = -1;
    public static final long row_key_none = -1L;
    public static final int table_max_sort_columns = 4;

    public static final int popup_policy_auto = 0;
    public static final int popup_policy_native = 1;
    public static final int popup_policy_window = 2;

    public static final int combobox_select_only = 0;
    public static final int combobox_filterable = 1;

    public static final int a11y_status_disabled = 0;
    public static final int a11y_status_unavailable = 1;
    public static final int a11y_status_active = 2;

    public static final int a11y_state_visible = 1 << 0;
    public static final int a11y_state_enabled = 1 << 1;
    public static final int a11y_state_focusable = 1 << 2;
    public static final int a11y_state_focused = 1 << 3;
    public static final int a11y_state_checked = 1 << 4;
    public static final int a11y_state_selected = 1 << 5;
    public static final int a11y_state_editable = 1 << 6;
    public static final int a11y_state_expanded = 1 << 7;
    public static final int a11y_state_busy = 1 << 8;
    public static final int a11y_state_modal = 1 << 9;
    public static final int a11y_state_expandable = 1 << 10;
    public static final int a11y_state_selectable = 1 << 11;
    public static final int a11y_state_multi_selectable = 1 << 12;

    public static final int a11y_event_focus_changed = 0;
    public static final int a11y_event_state_changed = 1;
    public static final int a11y_event_text_changed = 2;
    public static final int a11y_event_selection_changed = 3;
    public static final int a11y_event_value_changed = 4;
    public static final int a11y_event_name_changed = 5;
    public static final int a11y_event_structure_changed = 6;

    public static final float auto = -1.0f;

    public static int rgb(int value) {
        return ((value & 0xFFFFFF) << 8) | 0xFF;
    }

    public static int rgba(int red, int green, int blue, int alpha) {
        return ((red & 0xFF) << 24) | ((green & 0xFF) << 16) | ((blue & 0xFF) << 8)
                | (alpha & 0xFF);
    }

    /* ---------- native plumbing ---------- */

    private static final Linker linker = Linker.nativeLinker();
    private static final Arena library_arena = Arena.ofShared();
    private static final SymbolLookup lookup;
    private static final AddressLayout ptr = ValueLayout.ADDRESS;    private static final ValueLayout.OfInt i32 = ValueLayout.JAVA_INT;
    private static final ValueLayout.OfLong i64 = ValueLayout.JAVA_LONG;
    private static final ValueLayout.OfFloat f32 = ValueLayout.JAVA_FLOAT;
    private static final ValueLayout.OfBoolean bool = ValueLayout.JAVA_BOOLEAN;

    static {
        String path = System.getProperty("pt.library");
        if (path == null || path.isBlank()) {
            path = System.getenv("PT_LIBRARY");
        }
        if (path == null || path.isBlank()) {
            path = "libpt.so";
        }
        try {
            lookup = SymbolLookup.libraryLookup(path, library_arena);
        } catch (IllegalArgumentException error) {
            throw new ExceptionInInitializerError(
                    "cannot load the Pt library (" + path + "); pass -Dpt.library=/path/to/libpt.so");
        }
    }

    private static MethodHandle bind(String name, FunctionDescriptor descriptor) {
        MemorySegment symbol = lookup.find(name)
                .orElseThrow(() -> new IllegalStateException("Pt symbol not found: " + name));
        return linker.downcallHandle(symbol, descriptor);
    }

    private static Object invoke(MethodHandle handle, Object... arguments) {
        try {
            return handle.invokeWithArguments(arguments);
        } catch (RuntimeException error) {
            throw error;
        } catch (Throwable error) {
            throw new RuntimeException("Pt native call failed", error);
        }
    }

    private static MemorySegment cstring(Arena arena, String text) {
        return arena.allocateFrom(text, StandardCharsets.UTF_8);
    }

    private static FunctionDescriptor desc(ValueLayout layout, ValueLayout... arguments) {
        return FunctionDescriptor.of(layout, arguments);
    }

    /* Application */
    private static final MethodHandle app_create = bind("pt_app_create", desc(ptr, ptr));
    private static final MethodHandle app_destroy = bind("pt_app_destroy",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle app_run = bind("pt_app_run", desc(i32, ptr));
    private static final MethodHandle app_run_for = bind("pt_app_run_for", desc(i32, ptr, i32));
    private static final MethodHandle app_quit = bind("pt_app_quit", FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle app_is_running = bind("pt_app_is_running", desc(bool, ptr));
    private static final MethodHandle app_get_color_scheme = bind("pt_app_get_color_scheme",
            desc(i32, ptr));
    private static final MethodHandle app_enable_a11y = bind("pt_app_enable_a11y",
            desc(bool, ptr));
    private static final MethodHandle app_disable_a11y = bind("pt_app_disable_a11y",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle app_get_a11y_status = bind("pt_app_get_a11y_status",
            desc(i32, ptr));
    private static final MethodHandle app_set_color_scheme = bind("pt_app_set_color_scheme",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle app_set_accent_color = bind("pt_app_set_accent_color",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle app_set_font_family = bind("pt_app_set_font_family",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle app_set_reduced_motion = bind("pt_app_set_reduced_motion",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle app_get_reduced_motion = bind("pt_app_get_reduced_motion",
            desc(bool, ptr));
    private static final MethodHandle app_get_theme_color = bind("pt_app_get_theme_color",
            desc(i32, ptr, i32));
    private static final MethodHandle app_set_theme_color = bind("pt_app_set_theme_color",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle app_set_theme_metric = bind("pt_app_set_theme_metric",
            FunctionDescriptor.ofVoid(ptr, i32, f32));
    private static final MethodHandle app_get_theme_metric = bind("pt_app_get_theme_metric",
            desc(f32, ptr, i32));
    private static final MethodHandle app_set_quit_on_last_window_close = bind(
            "pt_app_set_quit_on_last_window_close", FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle app_add_timeout = bind("pt_app_add_timeout",
            desc(i64, ptr, i32, ptr, ptr));
    private static final MethodHandle app_cancel_timeout = bind("pt_app_cancel_timeout",
            FunctionDescriptor.ofVoid(ptr, i64));
    private static final MethodHandle get_last_error = bind("pt_get_last_error", desc(ptr));
    private static final MethodHandle free = bind("pt_free", FunctionDescriptor.ofVoid(ptr));

    /* Windows */
    private static final MethodHandle window_create = bind("pt_window_create",
            desc(ptr, ptr, ptr, i32, i32));
    private static final MethodHandle window_show = bind("pt_window_show",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_hide = bind("pt_window_hide",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_close = bind("pt_window_close",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_is_open = bind("pt_window_is_open", desc(bool, ptr));
    private static final MethodHandle window_root = bind("pt_window_root", desc(ptr, ptr));
    private static final MethodHandle window_app = bind("pt_window_app", desc(ptr, ptr));
    private static final MethodHandle window_get_width = bind("pt_window_get_width", desc(i32, ptr));
    private static final MethodHandle window_get_height = bind("pt_window_get_height",
            desc(i32, ptr));
    private static final MethodHandle window_get_scale = bind("pt_window_get_scale",
            desc(f32, ptr));
    private static final MethodHandle window_set_title = bind("pt_window_set_title",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle window_set_size = bind("pt_window_set_size",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle window_set_min_size = bind("pt_window_set_min_size",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle window_set_resizable = bind("pt_window_set_resizable",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle window_set_resize_callback = bind(
            "pt_window_set_resize_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle window_set_popup_policy = bind(
            "pt_window_set_popup_policy", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle window_get_popup_policy = bind(
            "pt_window_get_popup_policy", desc(i32, ptr));
    private static final MethodHandle window_set_decorated = bind("pt_window_set_decorated",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle window_clipboard_set = bind("pt_window_clipboard_set",
            desc(bool, ptr, ptr));
    private static final MethodHandle window_clipboard_get = bind("pt_window_clipboard_get",
            desc(ptr, ptr));
    private static final MethodHandle window_request_redraw = bind("pt_window_request_redraw",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_focus = bind("pt_window_focus",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle window_set_theme_color = bind("pt_window_set_theme_color",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle window_get_theme_color = bind("pt_window_get_theme_color",
            desc(i32, ptr, i32));
    private static final MethodHandle window_set_theme_metric = bind("pt_window_set_theme_metric",
            FunctionDescriptor.ofVoid(ptr, i32, f32));
    private static final MethodHandle window_get_theme_metric = bind("pt_window_get_theme_metric",
            desc(f32, ptr, i32));
    private static final MethodHandle window_clear_theme_overrides = bind(
            "pt_window_clear_theme_overrides", FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_set_close_callback = bind(
            "pt_window_set_close_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle window_set_key_callback = bind(
            "pt_window_set_key_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle window_set_gesture_callback = bind(
            "pt_window_set_gesture_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle window_set_long_press_delay = bind(
            "pt_window_set_long_press_delay", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle window_set_momentum_scrolling = bind(
            "pt_window_set_momentum_scrolling", FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle window_get_momentum_scrolling = bind(
            "pt_window_get_momentum_scrolling", desc(bool, ptr));
    private static final MemoryLayout drag_data_layout = MemoryLayout.structLayout(
            ValueLayout.ADDRESS.withName("mime_type"),
            ValueLayout.ADDRESS.withName("data"),
            ValueLayout.JAVA_LONG.withName("size"));
    private static final MethodHandle window_begin_drag = bind(
            "pt_window_begin_drag", desc(bool, ptr, ptr, i32));
    private static final MethodHandle window_set_drag_image = bind(
            "pt_window_set_drag_image", FunctionDescriptor.ofVoid(ptr, ptr, i32, i32));
    private static final MethodHandle window_clear_drag_image = bind(
            "pt_window_clear_drag_image", FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_get_drag_active = bind("pt_window_get_drag_active",
            desc(bool, ptr));
    private static final MethodHandle window_cancel_drag = bind("pt_window_cancel_drag",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle window_set_drag_timeout = bind(
            "pt_window_set_drag_timeout", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle window_get_drag_timeout = bind(
            "pt_window_get_drag_timeout", desc(i32, ptr));
    private static final MethodHandle window_get_drag_result = bind(
            "pt_window_get_drag_result", desc(i32, ptr));
    private static final MethodHandle window_set_drag_finished_callback = bind(
            "pt_window_set_drag_finished_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle uri_list_count = bind("pt_uri_list_count", desc(i32, ptr));
    private static final MethodHandle uri_list_get = bind("pt_uri_list_get", desc(ptr, ptr, i32));
    private static final MethodHandle window_set_stylus_callback = bind(
            "pt_window_set_stylus_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle app_set_a11y_callback = bind(
            "pt_app_set_a11y_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));

    /* Overlays */
    private static final MethodHandle menu_create = bind("pt_menu_create", desc(ptr, ptr));
    private static final MethodHandle menu_destroy = bind("pt_menu_destroy",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle menu_add_item = bind("pt_menu_add_item",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr, ptr, ptr));
    private static final MethodHandle menu_add_separator = bind("pt_menu_add_separator",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle menu_show_at = bind("pt_menu_show_at",
            FunctionDescriptor.ofVoid(ptr, f32, f32));
    private static final MethodHandle menu_show_below = bind("pt_menu_show_below",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle menu_dismiss = bind("pt_menu_dismiss",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle menu_is_open = bind("pt_menu_is_open", desc(bool, ptr));
    private static final MethodHandle dialog_create = bind("pt_dialog_create",
            desc(ptr, ptr, ptr, ptr));
    private static final MethodHandle dialog_destroy = bind("pt_dialog_destroy",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle dialog_set_title = bind("pt_dialog_set_title",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle dialog_set_message = bind("pt_dialog_set_message",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle dialog_add_action = bind("pt_dialog_add_action",
            FunctionDescriptor.ofVoid(ptr, ptr, bool, ptr, ptr));
    private static final MethodHandle dialog_show = bind("pt_dialog_show",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle dialog_close = bind("pt_dialog_close",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle dialog_is_open = bind("pt_dialog_is_open", desc(bool, ptr));
    private static final MethodHandle dialog_set_on_close = bind("pt_dialog_set_on_close",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle window_show_toast = bind("pt_window_show_toast",
            desc(ptr, ptr, ptr, i32, i32));
    private static final MethodHandle toast_dismiss = bind("pt_toast_dismiss",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle toast_is_visible = bind("pt_toast_is_visible", desc(bool, ptr));

    /* Accessibility */
    private static final MethodHandle widget_set_tooltip = bind("pt_widget_set_tooltip",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle widget_set_tooltip_delay = bind("pt_widget_set_tooltip_delay",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_get_a11y_role = bind("pt_widget_get_a11y_role",
            desc(i32, ptr));
    private static final MethodHandle widget_set_a11y_role = bind("pt_widget_set_a11y_role",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_get_a11y_name = bind("pt_widget_get_a11y_name",
            desc(ptr, ptr));
    private static final MethodHandle widget_set_a11y_name = bind("pt_widget_set_a11y_name",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle widget_get_a11y_description = bind(
            "pt_widget_get_a11y_description", desc(ptr, ptr));
    private static final MethodHandle widget_set_a11y_description = bind(
            "pt_widget_set_a11y_description", FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle widget_get_a11y_state = bind("pt_widget_get_a11y_state",
            desc(i32, ptr));
    private static final MethodHandle widget_get_a11y_text = bind("pt_widget_get_a11y_text",
            desc(bool, ptr, ptr, ptr, ptr, ptr));
    private static final MethodHandle widget_get_a11y_value = bind("pt_widget_get_a11y_value",
            desc(bool, ptr, ptr));

    /* Widgets */
    private static final MethodHandle box_create = bind("pt_box_create", desc(ptr, ptr, i32));
    private static final MethodHandle row_create = bind("pt_row_create", desc(ptr, ptr));
    private static final MethodHandle column_create = bind("pt_column_create", desc(ptr, ptr));
    private static final MethodHandle label_create = bind("pt_label_create", desc(ptr, ptr, ptr));
    private static final MethodHandle button_create = bind("pt_button_create", desc(ptr, ptr, ptr));
    private static final MethodHandle entry_create = bind("pt_entry_create", desc(ptr, ptr, ptr));
    private static final MethodHandle text_area_create = bind("pt_text_area_create",
            desc(ptr, ptr, ptr));
    private static final MethodHandle wrap_create = bind("pt_wrap_create", desc(ptr, ptr));
    private static final MethodHandle grid_create = bind("pt_grid_create", desc(ptr, ptr, i32));
    private static final MethodHandle list_view_create = bind("pt_list_view_create",
            desc(ptr, ptr));
    private static final MethodHandle toggle_create = bind("pt_toggle_create", desc(ptr, ptr));
    private static final MethodHandle checkbox_create = bind("pt_checkbox_create",
            desc(ptr, ptr, ptr));
    private static final MethodHandle radio_create = bind("pt_radio_create", desc(ptr, ptr, ptr));
    private static final MethodHandle slider_create = bind("pt_slider_create", desc(ptr, ptr));
    private static final MethodHandle progress_create = bind("pt_progress_create",
            desc(ptr, ptr));
    private static final MethodHandle spinner_create = bind("pt_spinner_create", desc(ptr, ptr));
    private static final MethodHandle segmented_create = bind("pt_segmented_create",
            desc(ptr, ptr, ptr, i32));
    private static final MethodHandle scroll_create = bind("pt_scroll_create", desc(ptr, ptr));
    private static final MethodHandle divider_create = bind("pt_divider_create", desc(ptr, ptr));
    private static final MethodHandle card_create = bind("pt_card_create", desc(ptr, ptr));
    private static final MethodHandle spacer_create = bind("pt_spacer_create", desc(ptr, ptr));
    private static final MethodHandle chip_create = bind("pt_chip_create", desc(ptr, ptr, ptr));
    private static final MethodHandle skeleton_create = bind("pt_skeleton_create", desc(ptr, ptr));
    private static final MethodHandle widget_set_text = bind("pt_widget_set_text",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle widget_get_text = bind("pt_widget_get_text", desc(ptr, ptr));
    private static final MethodHandle widget_set_text_spans = bind("pt_widget_set_text_spans",
            desc(bool, ptr, ptr, i32));
    private static final MethodHandle widget_clear_text_spans = bind("pt_widget_clear_text_spans",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle widget_get_text_span_count = bind(
            "pt_widget_get_text_span_count", desc(i32, ptr));
    private static final MethodHandle widget_get_text_span = bind("pt_widget_get_text_span",
            desc(bool, ptr, i32, ptr));
    private static final MethodHandle widget_set_input_purpose = bind(
            "pt_widget_set_input_purpose", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_get_input_purpose = bind(
            "pt_widget_get_input_purpose", desc(i32, ptr));

    private static final MemoryLayout text_span_layout = MemoryLayout.structLayout(
            ValueLayout.JAVA_INT.withName("start"),
            ValueLayout.JAVA_INT.withName("length"),
            ValueLayout.JAVA_INT.withName("weight"),
            ValueLayout.JAVA_INT.withName("slant"),
            ValueLayout.JAVA_INT.withName("color"),
            ValueLayout.JAVA_INT.withName("flags"),
            MemoryLayout.sequenceLayout(64, ValueLayout.JAVA_BYTE).withName("family"),
            ValueLayout.JAVA_FLOAT.withName("size"));
    private static final MethodHandle widget_set_size = bind("pt_widget_set_size",
            FunctionDescriptor.ofVoid(ptr, f32, f32));
    private static final MethodHandle widget_set_width = bind("pt_widget_set_width",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_height = bind("pt_widget_set_height",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_min_width = bind("pt_widget_set_min_width",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_min_height = bind("pt_widget_set_min_height",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_max_width = bind("pt_widget_set_max_width",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_max_height = bind("pt_widget_set_max_height",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_destroy = bind("pt_widget_destroy",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle widget_clear_children = bind("pt_widget_clear_children",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle widget_parent = bind("pt_widget_parent", desc(ptr, ptr));
    private static final MethodHandle widget_get_kind = bind("pt_widget_get_kind", desc(i32, ptr));
    private static final MethodHandle widget_get_bounds = bind("pt_widget_get_bounds",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr, ptr, ptr));
    private static final MethodHandle widget_has_focus = bind("pt_widget_has_focus",
            desc(bool, ptr));
    private static final MethodHandle widget_set_margin = bind("pt_widget_set_margin",
            FunctionDescriptor.ofVoid(ptr, f32, f32, f32, f32));
    private static final MethodHandle widget_set_padding = bind("pt_widget_set_padding",
            FunctionDescriptor.ofVoid(ptr, f32, f32, f32, f32));
    private static final MethodHandle widget_set_gap = bind("pt_widget_set_gap",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_grow = bind("pt_widget_set_grow",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_align_self = bind("pt_widget_set_align_self",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle box_set_direction = bind("pt_box_set_direction",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle box_set_align = bind("pt_box_set_align",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle box_set_justify = bind("pt_box_set_justify",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle box_set_gap = bind("pt_box_set_gap",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_background = bind("pt_widget_set_background",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_set_foreground = bind("pt_widget_set_foreground",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_set_border_color = bind("pt_widget_set_border_color",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_set_border_width = bind("pt_widget_set_border_width",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_radius = bind("pt_widget_set_radius",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_font_size = bind("pt_widget_set_font_size",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle widget_set_font_weight = bind("pt_widget_set_font_weight",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_set_text_align = bind("pt_widget_set_text_align",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_set_visible = bind("pt_widget_set_visible",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle widget_set_enabled = bind("pt_widget_set_enabled",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle widget_set_busy = bind("pt_widget_set_busy",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle widget_get_busy = bind("pt_widget_get_busy", desc(bool, ptr));
    private static final MethodHandle widget_set_invalid = bind("pt_widget_set_invalid",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle widget_get_invalid = bind("pt_widget_get_invalid",
            desc(bool, ptr));
    private static final MethodHandle widget_set_label_wrap = bind("pt_widget_set_label_wrap",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle button_set_variant = bind("pt_button_set_variant",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle button_set_on_click = bind("pt_button_set_on_click",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle chip_set_icon = bind("pt_chip_set_icon",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle chip_set_removable = bind("pt_chip_set_removable",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle chip_get_removable = bind("pt_chip_get_removable",
            desc(bool, ptr));
    private static final MethodHandle chip_set_selected = bind("pt_chip_set_selected",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle chip_get_selected = bind("pt_chip_get_selected",
            desc(bool, ptr));
    private static final MethodHandle chip_set_on_click = bind("pt_chip_set_on_click",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle chip_set_on_remove = bind("pt_chip_set_on_remove",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle entry_set_placeholder = bind("pt_entry_set_placeholder",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle entry_set_password = bind("pt_entry_set_password",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle entry_set_on_change = bind("pt_entry_set_on_change",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle entry_set_on_submit = bind("pt_entry_set_on_submit",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle entry_select_all = bind("pt_entry_select_all",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle entry_set_selection = bind("pt_entry_set_selection",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle entry_get_selection = bind("pt_entry_get_selection",
            desc(bool, ptr, ptr, ptr));
    private static final MethodHandle entry_undo = bind("pt_entry_undo", desc(bool, ptr));
    private static final MethodHandle entry_redo = bind("pt_entry_redo", desc(bool, ptr));
    private static final MethodHandle entry_set_drag_enabled = bind("pt_entry_set_drag_enabled",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle entry_get_drag_enabled = bind("pt_entry_get_drag_enabled",
            desc(bool, ptr));
    private static final MethodHandle widget_get_drop_hover = bind("pt_widget_get_drop_hover",
            desc(bool, ptr));
    private static final MethodHandle widget_set_drop_target = bind("pt_widget_set_drop_target",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MemoryLayout drop_target_layout = MemoryLayout.structLayout(
            ValueLayout.ADDRESS.withName("enter"),
            ValueLayout.ADDRESS.withName("motion"),
            ValueLayout.ADDRESS.withName("leave"),
            ValueLayout.ADDRESS.withName("drop"),
            ValueLayout.ADDRESS.withName("user_data"),
            ValueLayout.ADDRESS.withName("enter_offer"),
            ValueLayout.ADDRESS.withName("drop_offer"));
    private static final MethodHandle drop_offer_mime_count = bind("pt_drop_offer_mime_count",
            desc(i32, ptr));
    private static final MethodHandle drop_offer_mime = bind("pt_drop_offer_mime",
            desc(ptr, ptr, i32));
    private static final MethodHandle drop_offer_has_mime = bind("pt_drop_offer_has_mime",
            desc(bool, ptr, ptr));
    private static final MethodHandle drop_offer_selected_mime = bind(
            "pt_drop_offer_selected_mime", desc(ptr, ptr));
    private static final MethodHandle drop_offer_actions = bind("pt_drop_offer_actions",
            desc(i32, ptr));
    private static final MethodHandle drop_offer_read = bind("pt_drop_offer_read",
            desc(ptr, ptr, ptr, i64, ptr, ptr));
    private static final MethodHandle drop_stream_cancel = bind("pt_drop_stream_cancel",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle drop_stream_mime = bind("pt_drop_stream_mime",
            desc(ptr, ptr));
    private static final MethodHandle drop_stream_size = bind("pt_drop_stream_size",
            desc(i64, ptr));
    private static final MethodHandle drop_stream_active = bind("pt_drop_stream_active",
            desc(bool, ptr));
    private static final MethodHandle widget_focus = bind("pt_widget_focus", desc(bool, ptr));
    private static final MethodHandle toggle_get_checked = bind("pt_toggle_get_checked",
            desc(bool, ptr));
    private static final MethodHandle toggle_set_checked = bind("pt_toggle_set_checked",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle toggle_set_on_change = bind("pt_toggle_set_on_change",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle slider_get_value = bind("pt_slider_get_value",
            desc(f32, ptr));
    private static final MethodHandle slider_set_value = bind("pt_slider_set_value",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle slider_set_on_change = bind("pt_slider_set_on_change",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle widget_get_checked = bind("pt_widget_get_checked",
            desc(bool, ptr));
    private static final MethodHandle widget_set_checked = bind("pt_widget_set_checked",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle widget_set_on_checked = bind("pt_widget_set_on_checked",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle progress_get_value = bind("pt_progress_get_value",
            desc(f32, ptr));
    private static final MethodHandle progress_set_value = bind("pt_progress_set_value",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle progress_get_indeterminate = bind(
            "pt_progress_get_indeterminate", desc(bool, ptr));
    private static final MethodHandle progress_set_indeterminate = bind(
            "pt_progress_set_indeterminate", FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle segmented_get_selected = bind("pt_segmented_get_selected",
            desc(i32, ptr));
    private static final MethodHandle segmented_set_selected = bind("pt_segmented_set_selected",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle segmented_set_on_change = bind("pt_segmented_set_on_change",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle scroll_set_offset = bind("pt_scroll_set_offset",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle scroll_get_offset = bind("pt_scroll_get_offset",
            desc(f32, ptr));
    private static final MethodHandle scroll_get_max_offset = bind("pt_scroll_get_max_offset",
            desc(f32, ptr));

    /* Text areas, responsive containers and virtual lists */
    private static final MethodHandle text_area_set_placeholder = bind(
            "pt_text_area_set_placeholder", FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle text_area_set_on_change = bind(
            "pt_text_area_set_on_change", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle text_area_set_grow_to_content = bind(
            "pt_text_area_set_grow_to_content", FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle text_area_get_grow_to_content = bind(
            "pt_text_area_get_grow_to_content", desc(bool, ptr));
    private static final MethodHandle text_area_scroll_to_caret = bind(
            "pt_text_area_scroll_to_caret", FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle text_area_select_all = bind("pt_text_area_select_all",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle text_area_set_selection = bind(
            "pt_text_area_set_selection", FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle text_area_get_selection = bind(
            "pt_text_area_get_selection", desc(bool, ptr, ptr, ptr));
    private static final MethodHandle text_area_undo = bind("pt_text_area_undo", desc(bool, ptr));
    private static final MethodHandle text_area_redo = bind("pt_text_area_redo", desc(bool, ptr));
    private static final MethodHandle wrap_set_gap = bind("pt_wrap_set_gap",
            FunctionDescriptor.ofVoid(ptr, f32, f32));
    private static final MethodHandle grid_set_columns = bind("pt_grid_set_columns",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle grid_get_columns = bind("pt_grid_get_columns",
            desc(i32, ptr));
    private static final MethodHandle grid_set_gap = bind("pt_grid_set_gap",
            FunctionDescriptor.ofVoid(ptr, f32, f32));
    private static final MethodHandle widget_set_grid_span = bind("pt_widget_set_grid_span",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_get_grid_span = bind("pt_widget_get_grid_span",
            desc(i32, ptr));
    private static final MethodHandle list_view_set_row_count = bind(
            "pt_list_view_set_row_count", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle list_view_get_row_count = bind(
            "pt_list_view_get_row_count", desc(i32, ptr));
    private static final MethodHandle list_view_set_row_height = bind(
            "pt_list_view_set_row_height", FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle list_view_get_row_height = bind(
            "pt_list_view_get_row_height", desc(f32, ptr));
    private static final MethodHandle list_view_set_on_bind_row = bind(
            "pt_list_view_set_on_bind_row", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle list_view_set_on_select = bind(
            "pt_list_view_set_on_select", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle list_view_set_on_activate = bind(
            "pt_list_view_set_on_activate", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle list_view_get_selected = bind(
            "pt_list_view_get_selected", desc(i32, ptr));
    private static final MethodHandle list_view_set_selected = bind(
            "pt_list_view_set_selected", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle list_view_refresh = bind("pt_list_view_refresh",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle list_view_refresh_rows = bind(
            "pt_list_view_refresh_rows", FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle list_view_scroll_to_row = bind(
            "pt_list_view_scroll_to_row", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle list_view_set_row_key_callback = bind(
            "pt_list_view_set_row_key_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle list_view_get_selected_key = bind(
            "pt_list_view_get_selected_key", desc(i64, ptr));
    private static final MethodHandle list_view_set_selected_key = bind(
            "pt_list_view_set_selected_key", desc(bool, ptr, i64));
    private static final MethodHandle list_view_set_row_text_callback = bind(
            "pt_list_view_set_row_text_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle list_view_get_selection_count = bind(
            "pt_list_view_get_selection_count", desc(i32, ptr));
    private static final MethodHandle list_view_is_row_selected = bind(
            "pt_list_view_is_row_selected", desc(bool, ptr, i32));
    private static final MethodHandle list_view_get_selection = bind(
            "pt_list_view_get_selection", desc(i32, ptr, ptr, i32));
    private static final MethodHandle list_view_select_range = bind(
            "pt_list_view_select_range", FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle list_view_select_toggle = bind(
            "pt_list_view_select_toggle", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle list_view_clear_selection = bind(
            "pt_list_view_clear_selection", FunctionDescriptor.ofVoid(ptr));

    /* Icons, tables, trees, tabs and sidebars */
    private static final MethodHandle icon_create = bind("pt_icon_create", desc(ptr, ptr, i32));
    private static final MethodHandle icon_button_create = bind("pt_icon_button_create",
            desc(ptr, ptr, i32, ptr));
    private static final MethodHandle widget_set_icon = bind("pt_widget_set_icon",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle widget_get_icon = bind("pt_widget_get_icon", desc(i32, ptr));
    private static final MethodHandle icon_set_size = bind("pt_icon_set_size",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle icon_get_size = bind("pt_icon_get_size", desc(f32, ptr));
    private static final MethodHandle table_create = bind("pt_table_create", desc(ptr, ptr));
    private static final MethodHandle table_set_column_count = bind("pt_table_set_column_count",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle table_get_column_count = bind("pt_table_get_column_count",
            desc(i32, ptr));
    private static final MethodHandle table_set_column_title = bind("pt_table_set_column_title",
            FunctionDescriptor.ofVoid(ptr, i32, ptr));
    private static final MethodHandle table_set_column_width = bind("pt_table_set_column_width",
            FunctionDescriptor.ofVoid(ptr, i32, f32));
    private static final MethodHandle table_get_column_width = bind("pt_table_get_column_width",
            desc(f32, ptr, i32));
    private static final MethodHandle table_set_column_align = bind("pt_table_set_column_align",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle table_set_column_resizable = bind(
            "pt_table_set_column_resizable", FunctionDescriptor.ofVoid(ptr, i32, bool));
    private static final MethodHandle table_set_column_hidden = bind(
            "pt_table_set_column_hidden", FunctionDescriptor.ofVoid(ptr, i32, bool));
    private static final MethodHandle table_get_column_hidden = bind(
            "pt_table_get_column_hidden", desc(bool, ptr, i32));
    private static final MethodHandle table_set_row_count = bind("pt_table_set_row_count",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle table_get_row_count = bind("pt_table_get_row_count",
            desc(i32, ptr));
    private static final MethodHandle table_set_row_height = bind("pt_table_set_row_height",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle table_get_row_height = bind("pt_table_get_row_height",
            desc(f32, ptr));
    private static final MethodHandle table_set_header_height = bind("pt_table_set_header_height",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle table_set_on_bind_cell = bind("pt_table_set_on_bind_cell",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_set_on_select = bind("pt_table_set_on_select",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_set_on_activate = bind("pt_table_set_on_activate",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_set_on_sort = bind("pt_table_set_on_sort",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_set_on_sort_columns = bind(
            "pt_table_set_on_sort_columns", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_get_sort = bind("pt_table_get_sort", desc(i32, ptr, ptr));
    private static final MethodHandle table_set_sort = bind("pt_table_set_sort",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle table_get_sort_columns = bind("pt_table_get_sort_columns",
            desc(i32, ptr, ptr, ptr));
    private static final MethodHandle table_set_sort_columns = bind("pt_table_set_sort_columns",
            FunctionDescriptor.ofVoid(ptr, i32, ptr, ptr));
    private static final MethodHandle table_get_selected = bind("pt_table_get_selected",
            desc(i32, ptr));
    private static final MethodHandle table_set_selected = bind("pt_table_set_selected",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle table_set_row_key_callback = bind(
            "pt_table_set_row_key_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_get_selected_key = bind(
            "pt_table_get_selected_key", desc(i64, ptr));
    private static final MethodHandle table_set_selected_key = bind(
            "pt_table_set_selected_key", desc(bool, ptr, i64));
    private static final MethodHandle table_set_row_text_callback = bind(
            "pt_table_set_row_text_callback", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_refresh = bind("pt_table_refresh",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle table_refresh_rows = bind("pt_table_refresh_rows",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle table_scroll_to_row = bind("pt_table_scroll_to_row",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle table_scroll_to_column = bind("pt_table_scroll_to_column",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle table_set_editable = bind("pt_table_set_editable",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle table_get_editable = bind("pt_table_get_editable",
            desc(bool, ptr));
    private static final MethodHandle table_begin_edit = bind("pt_table_begin_edit",
            desc(bool, ptr, i32, i32));
    private static final MethodHandle table_commit_edit = bind("pt_table_commit_edit",
            desc(bool, ptr));
    private static final MethodHandle table_cancel_edit = bind("pt_table_cancel_edit",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle table_is_editing = bind("pt_table_is_editing",
            desc(bool, ptr));
    private static final MethodHandle table_set_on_cell_edited = bind(
            "pt_table_set_on_cell_edited", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_get_selection_count = bind(
            "pt_table_get_selection_count", desc(i32, ptr));
    private static final MethodHandle table_is_row_selected = bind("pt_table_is_row_selected",
            desc(bool, ptr, i32));
    private static final MethodHandle table_get_selection = bind("pt_table_get_selection",
            desc(i32, ptr, ptr, i32));
    private static final MethodHandle table_select_range = bind("pt_table_select_range",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle table_select_toggle = bind("pt_table_select_toggle",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle table_clear_selection = bind("pt_table_clear_selection",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle table_set_columns_reorderable = bind(
            "pt_table_set_columns_reorderable", FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle table_get_columns_reorderable = bind(
            "pt_table_get_columns_reorderable", desc(bool, ptr));
    private static final MethodHandle table_move_column = bind("pt_table_move_column",
            desc(bool, ptr, i32, i32));
    private static final MethodHandle table_set_on_columns_reordered = bind(
            "pt_table_set_on_columns_reordered", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle table_set_on_row_drag = bind("pt_table_set_on_row_drag",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));

    private static final MethodHandle tree_create = bind("pt_tree_create", desc(ptr, ptr));
    private static final MethodHandle tree_add_node = bind("pt_tree_add_node",
            desc(ptr, ptr, ptr, ptr));
    private static final MethodHandle tree_clear = bind("pt_tree_clear",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle tree_get_root_count = bind("pt_tree_get_root_count",
            desc(i32, ptr));
    private static final MethodHandle tree_get_root_node = bind("pt_tree_get_root_node",
            desc(ptr, ptr, i32));
    private static final MethodHandle tree_node_get_text = bind("pt_tree_node_get_text",
            desc(ptr, ptr));
    private static final MethodHandle tree_node_set_text = bind("pt_tree_node_set_text",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle tree_node_get_expanded = bind("pt_tree_node_get_expanded",
            desc(bool, ptr));
    private static final MethodHandle tree_node_set_expanded = bind("pt_tree_node_set_expanded",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle tree_node_has_children = bind("pt_tree_node_has_children",
            desc(bool, ptr));
    private static final MethodHandle tree_node_get_depth = bind("pt_tree_node_get_depth",
            desc(i32, ptr));
    private static final MethodHandle tree_node_parent = bind("pt_tree_node_parent", desc(ptr, ptr));
    private static final MethodHandle tree_node_get_child_count = bind(
            "pt_tree_node_get_child_count", desc(i32, ptr));
    private static final MethodHandle tree_node_get_child = bind("pt_tree_node_get_child",
            desc(ptr, ptr, i32));
    private static final MethodHandle tree_node_get_data = bind("pt_tree_node_get_data",
            desc(ptr, ptr));
    private static final MethodHandle tree_node_set_data = bind("pt_tree_node_set_data",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle tree_get_visible_count = bind("pt_tree_get_visible_count",
            desc(i32, ptr));
    private static final MethodHandle tree_get_visible_node = bind("pt_tree_get_visible_node",
            desc(ptr, ptr, i32));
    private static final MethodHandle tree_get_selected = bind("pt_tree_get_selected",
            desc(ptr, ptr));
    private static final MethodHandle tree_set_selected = bind("pt_tree_set_selected",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle tree_set_row_height = bind("pt_tree_set_row_height",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle tree_set_indent = bind("pt_tree_set_indent",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle tree_set_on_select = bind("pt_tree_set_on_select",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle tree_set_on_activate = bind("pt_tree_set_on_activate",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle tree_refresh = bind("pt_tree_refresh",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle tree_scroll_to_node = bind("pt_tree_scroll_to_node",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle tree_set_renamable = bind("pt_tree_set_renamable",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle tree_get_renamable = bind("pt_tree_get_renamable",
            desc(bool, ptr));
    private static final MethodHandle tree_begin_rename = bind("pt_tree_begin_rename",
            desc(bool, ptr, ptr));
    private static final MethodHandle tree_commit_rename = bind("pt_tree_commit_rename",
            desc(bool, ptr));
    private static final MethodHandle tree_cancel_rename = bind("pt_tree_cancel_rename",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle tree_is_renaming = bind("pt_tree_is_renaming",
            desc(bool, ptr));
    private static final MethodHandle tree_set_on_rename = bind("pt_tree_set_on_rename",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle tree_get_selection_count = bind(
            "pt_tree_get_selection_count", desc(i32, ptr));
    private static final MethodHandle tree_is_selected = bind("pt_tree_is_selected",
            desc(bool, ptr, ptr));
    private static final MethodHandle tree_get_selection = bind("pt_tree_get_selection",
            desc(i32, ptr, ptr, i32));
    private static final MethodHandle tree_select_range = bind("pt_tree_select_range",
            FunctionDescriptor.ofVoid(ptr, i32, i32));
    private static final MethodHandle tree_select_toggle = bind("pt_tree_select_toggle",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle tree_clear_selection = bind("pt_tree_clear_selection",
            FunctionDescriptor.ofVoid(ptr));

    /* Combobox */
    private static final MethodHandle combobox_create = bind("pt_combobox_create",
            desc(ptr, ptr));
    private static final MethodHandle combobox_add_item = bind("pt_combobox_add_item",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle combobox_insert_item = bind("pt_combobox_insert_item",
            FunctionDescriptor.ofVoid(ptr, i32, ptr));
    private static final MethodHandle combobox_remove_item = bind("pt_combobox_remove_item",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle combobox_clear = bind("pt_combobox_clear",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle combobox_get_item_count = bind(
            "pt_combobox_get_item_count", desc(i32, ptr));
    private static final MethodHandle combobox_get_item_text = bind(
            "pt_combobox_get_item_text", desc(ptr, ptr, i32));
    private static final MethodHandle combobox_set_item_text = bind(
            "pt_combobox_set_item_text", FunctionDescriptor.ofVoid(ptr, i32, ptr));
    private static final MethodHandle combobox_set_item_data = bind(
            "pt_combobox_set_item_data", FunctionDescriptor.ofVoid(ptr, i32, ptr));
    private static final MethodHandle combobox_get_item_data = bind(
            "pt_combobox_get_item_data", desc(ptr, ptr, i32));
    private static final MethodHandle combobox_set_item_disabled = bind(
            "pt_combobox_set_item_disabled", FunctionDescriptor.ofVoid(ptr, i32, bool));
    private static final MethodHandle combobox_get_item_disabled = bind(
            "pt_combobox_get_item_disabled", desc(bool, ptr, i32));
    private static final MethodHandle combobox_set_mode = bind("pt_combobox_set_mode",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle combobox_get_mode = bind("pt_combobox_get_mode",
            desc(i32, ptr));
    private static final MethodHandle combobox_get_selected = bind(
            "pt_combobox_get_selected", desc(i32, ptr));
    private static final MethodHandle combobox_set_selected = bind(
            "pt_combobox_set_selected", FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle combobox_get_text = bind("pt_combobox_get_text",
            desc(ptr, ptr));
    private static final MethodHandle combobox_set_text = bind("pt_combobox_set_text",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle combobox_get_placeholder = bind(
            "pt_combobox_get_placeholder", desc(ptr, ptr));
    private static final MethodHandle combobox_set_placeholder = bind(
            "pt_combobox_set_placeholder", FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle combobox_open = bind("pt_combobox_open",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle combobox_close = bind("pt_combobox_close",
            FunctionDescriptor.ofVoid(ptr));
    private static final MethodHandle combobox_is_open = bind("pt_combobox_is_open",
            desc(bool, ptr));
    private static final MethodHandle combobox_set_popup_max_height = bind(
            "pt_combobox_set_popup_max_height", FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle combobox_get_popup_max_height = bind(
            "pt_combobox_get_popup_max_height", desc(f32, ptr));
    private static final MethodHandle combobox_set_on_change = bind(
            "pt_combobox_set_on_change", FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle combobox_set_on_text_change = bind(
            "pt_combobox_set_on_text_change", FunctionDescriptor.ofVoid(ptr, ptr, ptr));

    private static final MethodHandle tabs_create = bind("pt_tabs_create", desc(ptr, ptr));
    private static final MethodHandle tabs_add_page = bind("pt_tabs_add_page", desc(ptr, ptr, ptr));
    private static final MethodHandle tabs_remove_page = bind("pt_tabs_remove_page",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle tabs_get_page_count = bind("pt_tabs_get_page_count",
            desc(i32, ptr));
    private static final MethodHandle tabs_get_page = bind("pt_tabs_get_page", desc(ptr, ptr, i32));
    private static final MethodHandle tabs_set_page_title = bind("pt_tabs_set_page_title",
            FunctionDescriptor.ofVoid(ptr, i32, ptr));
    private static final MethodHandle tabs_get_page_title = bind("pt_tabs_get_page_title",
            desc(ptr, ptr, i32));
    private static final MethodHandle tabs_get_active = bind("pt_tabs_get_active", desc(i32, ptr));
    private static final MethodHandle tabs_set_active = bind("pt_tabs_set_active",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle tabs_set_on_change = bind("pt_tabs_set_on_change",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));
    private static final MethodHandle tabs_set_strip_height = bind("pt_tabs_set_strip_height",
            FunctionDescriptor.ofVoid(ptr, f32));

    private static final MethodHandle sidebar_create = bind("pt_sidebar_create", desc(ptr, ptr));
    private static final MethodHandle sidebar_add_item = bind("pt_sidebar_add_item",
            desc(ptr, ptr, i32, ptr));
    private static final MethodHandle sidebar_get_item_count = bind("pt_sidebar_get_item_count",
            desc(i32, ptr));
    private static final MethodHandle sidebar_get_item = bind("pt_sidebar_get_item",
            desc(ptr, ptr, i32));
    private static final MethodHandle sidebar_get_active = bind("pt_sidebar_get_active",
            desc(i32, ptr));
    private static final MethodHandle sidebar_set_active = bind("pt_sidebar_set_active",
            FunctionDescriptor.ofVoid(ptr, i32));
    private static final MethodHandle sidebar_get_compact = bind("pt_sidebar_get_compact",
            desc(bool, ptr));
    private static final MethodHandle sidebar_set_compact = bind("pt_sidebar_set_compact",
            FunctionDescriptor.ofVoid(ptr, bool));
    private static final MethodHandle sidebar_set_header = bind("pt_sidebar_set_header",
            FunctionDescriptor.ofVoid(ptr, ptr));
    private static final MethodHandle sidebar_get_header = bind("pt_sidebar_get_header",
            desc(ptr, ptr));
    private static final MethodHandle sidebar_set_on_select = bind("pt_sidebar_set_on_select",
            FunctionDescriptor.ofVoid(ptr, ptr, ptr));

    /* Motion */
    private static final MethodHandle pt_ease = bind("pt_ease", desc(f32, i32, f32));
    private static final MethodHandle pt_ease_value = bind("pt_ease_value",
            desc(f32, i32, f32, f32, f32));
    private static final MethodHandle transition_init = bind("pt_transition_init",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle transition_to = bind("pt_transition_to",
            FunctionDescriptor.ofVoid(ptr, f32, i32, i32));
    private static final MethodHandle transition_advance = bind("pt_transition_advance",
            desc(bool, ptr, f32));
    private static final MethodHandle transition_set = bind("pt_transition_set",
            FunctionDescriptor.ofVoid(ptr, f32));
    private static final MethodHandle transition_active = bind("pt_transition_active",
            desc(bool, ptr));

    private static final FunctionDescriptor click_descriptor = FunctionDescriptor.ofVoid(ptr, ptr);
    private static final FunctionDescriptor text_descriptor = FunctionDescriptor.ofVoid(ptr, ptr,
            ptr);
    private static final FunctionDescriptor value_descriptor = FunctionDescriptor.ofVoid(ptr, f32,
            ptr);
    private static final FunctionDescriptor index_descriptor = FunctionDescriptor.ofVoid(ptr, i32,
            ptr);
    private static final FunctionDescriptor list_bind_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr, i32, ptr);
    private static final FunctionDescriptor close_descriptor = FunctionDescriptor.of(bool, ptr, ptr);
    private static final FunctionDescriptor key_descriptor = FunctionDescriptor.of(bool, ptr, i32,
            i32, ptr);
    private static final FunctionDescriptor a11y_descriptor = FunctionDescriptor.ofVoid(ptr, ptr,
            i32, ptr);
    private static final FunctionDescriptor dialog_descriptor = FunctionDescriptor.ofVoid(ptr, ptr);
    private static final FunctionDescriptor gesture_descriptor = FunctionDescriptor.ofVoid(ptr, ptr,
            ptr);
    private static final FunctionDescriptor drop_enter_descriptor = FunctionDescriptor.of(bool, ptr,
            ptr, ptr);
    private static final FunctionDescriptor drop_motion_descriptor = FunctionDescriptor.ofVoid(ptr,
            f32, f32, ptr);
    private static final FunctionDescriptor drop_leave_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr);
    private static final FunctionDescriptor drop_data_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr, ptr, f32, f32, ptr);
    private static final FunctionDescriptor drop_offer_enter_descriptor =
            FunctionDescriptor.of(ptr, ptr, ptr, ptr);
    private static final FunctionDescriptor drop_offer_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr, ptr, f32, f32, ptr);
    private static final FunctionDescriptor drop_stream_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr, i32, ptr, i64, ptr);
    private static final FunctionDescriptor drag_finished_descriptor =
            FunctionDescriptor.ofVoid(ptr, bool, i32, ptr);
    private static final long drop_mime_capacity = 256;
    private static final FunctionDescriptor table_bind_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr, i32, i32, ptr);
    private static final FunctionDescriptor sort_descriptor = FunctionDescriptor.ofVoid(ptr, i32,
            i32, ptr);
    private static final FunctionDescriptor tree_node_descriptor = FunctionDescriptor.ofVoid(ptr,
            ptr, ptr);
    private static final FunctionDescriptor stylus_descriptor = FunctionDescriptor.ofVoid(ptr, ptr,
            ptr);
    private static final FunctionDescriptor resize_descriptor = FunctionDescriptor.ofVoid(ptr,
            i32, i32, ptr);
    private static final FunctionDescriptor row_key_descriptor = FunctionDescriptor.of(i64, ptr,
            i32, ptr);
    private static final FunctionDescriptor row_text_descriptor = FunctionDescriptor.of(ptr, ptr,
            i32, ptr);
    private static final FunctionDescriptor sort_columns_descriptor = FunctionDescriptor.ofVoid(
            ptr, i32, ptr, ptr, ptr);
    private static final FunctionDescriptor table_edited_descriptor = FunctionDescriptor.ofVoid(
            ptr, i32, i32, ptr, ptr);
    private static final FunctionDescriptor table_reorder_descriptor = FunctionDescriptor.ofVoid(
            ptr, i32, i32, ptr);
    private static final FunctionDescriptor table_row_drag_descriptor = FunctionDescriptor.ofVoid(
            ptr, i32, ptr);
    private static final FunctionDescriptor tree_rename_descriptor = FunctionDescriptor.ofVoid(
            ptr, ptr, ptr, ptr);
    private static final long row_text_capacity = 256;

    private static MemorySegment click_upcall(ClickHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(ClickHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), click_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create click callback", error);
        }
    }

    private static MemorySegment text_upcall(TextHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(TextHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), text_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create text callback", error);
        }
    }

    private static MemorySegment value_upcall(ValueHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(ValueHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, float.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), value_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create value callback", error);
        }
    }

    private static MemorySegment index_upcall(IndexHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(IndexHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, int.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), index_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create index callback", error);
        }
    }

    private static MemorySegment list_bind_upcall(ListBindHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(ListBindHandler.class,
                    "handle", MethodType.methodType(void.class, MemorySegment.class,
                            MemorySegment.class, int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), list_bind_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create list bind callback", error);
        }
    }

    private static MemorySegment close_upcall(CloseHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(CloseHandler.class, "handle",
                    MethodType.methodType(boolean.class, MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), close_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create close callback", error);
        }
    }

    private static MemorySegment key_upcall(KeyHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(KeyHandler.class, "handle",
                    MethodType.methodType(boolean.class, MemorySegment.class, int.class, int.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), key_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create key callback", error);
        }
    }

    private static MemorySegment timer_upcall(TimerHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(TimerHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), click_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create timer callback", error);
        }
    }

    private static MemorySegment a11y_upcall(A11yHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(A11yHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class,
                            int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), a11y_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create accessibility callback", error);
        }
    }

    private static MemorySegment dialog_upcall(DialogHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(DialogHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), dialog_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create dialog callback", error);
        }
    }

    private static String nativeText(MemorySegment text) {
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    /* Mirrors nativeText for accessors whose C contract allows a NULL string. */
    private static String optionalText(MemorySegment text) {
        return text.address() == 0 ? null : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    private static void gesture_bridge(GestureHandler handler, MemorySegment window,
            MemorySegment event, MemorySegment data) {
        handler.handle(window, GestureEvent.read(event), data);
    }

    private static MemorySegment gesture_upcall(GestureHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "gesture_bridge",
                    MethodType.methodType(void.class, GestureHandler.class, MemorySegment.class,
                            MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), gesture_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create gesture callback", error);
        }
    }

    private static boolean drop_enter_bridge(DropEnterHandler handler, MemorySegment widget,
            MemorySegment mime_type, MemorySegment data) {
        return handler.handle(widget, nativeText(mime_type), data);
    }

    private static MemorySegment drop_enter_upcall(DropEnterHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "drop_enter_bridge",
                    MethodType.methodType(boolean.class, DropEnterHandler.class,
                            MemorySegment.class, MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drop_enter_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop enter callback", error);
        }
    }

    private static void drop_motion_bridge(DropMotionHandler handler, MemorySegment widget,
            float x, float y, MemorySegment data) {
        handler.handle(widget, x, y, data);
    }

    private static MemorySegment drop_motion_upcall(DropMotionHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "drop_motion_bridge",
                    MethodType.methodType(void.class, DropMotionHandler.class,
                            MemorySegment.class, float.class, float.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drop_motion_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop motion callback", error);
        }
    }

    private static void drop_leave_bridge(DropLeaveHandler handler, MemorySegment widget,
            MemorySegment data) {
        handler.handle(widget, data);
    }

    private static MemorySegment drop_leave_upcall(DropLeaveHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "drop_leave_bridge",
                    MethodType.methodType(void.class, DropLeaveHandler.class, MemorySegment.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drop_leave_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop leave callback", error);
        }
    }

    private static void drop_data_bridge(DropHandler handler, MemorySegment widget,
            MemorySegment mime_type, MemorySegment text, float x, float y, MemorySegment data) {
        handler.handle(widget, nativeText(mime_type), nativeText(text), x, y, data);
    }

    private static MemorySegment drop_data_upcall(DropHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "drop_data_bridge",
                    MethodType.methodType(void.class, DropHandler.class, MemorySegment.class,
                            MemorySegment.class, MemorySegment.class, float.class, float.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drop_data_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop callback", error);
        }
    }

    private static MemorySegment drop_offer_enter_bridge(DropOfferEnterHandler handler,
            MemorySegment buffer, MemorySegment widget, MemorySegment offer,
            MemorySegment data) {
        String mime = handler.handle(widget, offer, data);
        if (mime == null) {
            return MemorySegment.NULL;
        }
        byte[] encoded = mime.getBytes(StandardCharsets.UTF_8);
        int length = (int) Math.min(encoded.length, drop_mime_capacity - 1);
        MemorySegment.copy(encoded, 0, buffer, ValueLayout.JAVA_BYTE, 0, length);
        buffer.set(ValueLayout.JAVA_BYTE, length, (byte) 0);
        return buffer;
    }

    private static MemorySegment drop_offer_enter_upcall(DropOfferEnterHandler handler,
            Arena arena) {
        try {
            MemorySegment buffer = arena.allocate(drop_mime_capacity, 1);
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class,
                            "drop_offer_enter_bridge",
                            MethodType.methodType(MemorySegment.class,
                                    DropOfferEnterHandler.class, MemorySegment.class,
                                    MemorySegment.class, MemorySegment.class, MemorySegment.class))
                    .bindTo(handler)
                    .bindTo(buffer);
            return linker.upcallStub(target, drop_offer_enter_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop offer enter callback", error);
        }
    }

    private static void drop_offer_bridge(DropOfferHandler handler, MemorySegment widget,
            MemorySegment offer, MemorySegment mime_type, float x, float y, MemorySegment data) {
        handler.handle(widget, offer, nativeText(mime_type), x, y, data);
    }

    private static MemorySegment drop_offer_upcall(DropOfferHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "drop_offer_bridge",
                    MethodType.methodType(void.class, DropOfferHandler.class, MemorySegment.class,
                            MemorySegment.class, MemorySegment.class, float.class, float.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drop_offer_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop offer callback", error);
        }
    }

    /** Chunks are not NUL-terminated, so exactly size bytes are copied per call. */
    private static void drop_stream_bridge(DropStreamHandler handler, MemorySegment widget,
            MemorySegment stream, int state, MemorySegment chunk, long size,
            MemorySegment data) {
        byte[] bytes = chunk.address() == 0 || size <= 0
                ? new byte[0]
                : chunk.reinterpret(size).toArray(ValueLayout.JAVA_BYTE);
        handler.handle(widget, stream, state, bytes, drop_stream_mime(stream), data);
    }

    private static MemorySegment drop_stream_upcall(DropStreamHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "drop_stream_bridge",
                    MethodType.methodType(void.class, DropStreamHandler.class, MemorySegment.class,
                            MemorySegment.class, int.class, MemorySegment.class, long.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drop_stream_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drop stream callback", error);
        }
    }

    private static void drag_finished_bridge(DragFinishedHandler handler, MemorySegment window,
            boolean dropped, int action, MemorySegment data) {
        handler.handle(window, dropped, action, data);
    }

    private static MemorySegment drag_finished_upcall(DragFinishedHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class,
                    "drag_finished_bridge", MethodType.methodType(void.class,
                            DragFinishedHandler.class, MemorySegment.class, boolean.class,
                            int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), drag_finished_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create drag finished callback", error);
        }
    }

    private static MemorySegment table_bind_upcall(TableBindHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(TableBindHandler.class,
                    "handle", MethodType.methodType(void.class, MemorySegment.class,
                            MemorySegment.class, int.class, int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), table_bind_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create table bind callback", error);
        }
    }

    private static MemorySegment sort_upcall(SortHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(SortHandler.class, "handle",
                    MethodType.methodType(void.class, MemorySegment.class, int.class, int.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), sort_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create sort callback", error);
        }
    }

    private static MemorySegment tree_node_upcall(TreeNodeHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(TreeNodeHandler.class,
                    "handle", MethodType.methodType(void.class, MemorySegment.class,
                            MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), tree_node_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create tree node callback", error);
        }
    }

    private static void stylus_bridge(StylusHandler handler, MemorySegment window,
            MemorySegment event, MemorySegment data) {
        handler.handle(window, StylusEvent.read(event), data);
    }

    private static MemorySegment stylus_upcall(StylusHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "stylus_bridge",
                    MethodType.methodType(void.class, StylusHandler.class, MemorySegment.class,
                            MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), stylus_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create stylus callback", error);
        }
    }

    private static void resize_bridge(ResizeHandler handler, MemorySegment window, int width,
            int height, MemorySegment data) {
        handler.handle(window, width, height, data);
    }

    private static MemorySegment resize_upcall(ResizeHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "resize_bridge",
                    MethodType.methodType(void.class, ResizeHandler.class, MemorySegment.class,
                            int.class, int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), resize_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create resize callback", error);
        }
    }

    private static long row_key_bridge(RowKeyHandler handler, MemorySegment view, int row,
            MemorySegment data) {
        return handler.handle(view, row, data);
    }

    private static MemorySegment row_key_upcall(RowKeyHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "row_key_bridge",
                    MethodType.methodType(long.class, RowKeyHandler.class, MemorySegment.class,
                            int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), row_key_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create row key callback", error);
        }
    }

    private static MemorySegment row_text_bridge(RowTextHandler handler, MemorySegment buffer,
            MemorySegment view, int row, MemorySegment data) {
        String text = handler.handle(view, row, data);
        if (text == null) {
            buffer.set(ValueLayout.JAVA_BYTE, 0, (byte) 0);
            return buffer;
        }
        byte[] encoded = text.getBytes(StandardCharsets.UTF_8);
        int length = (int) Math.min(encoded.length, row_text_capacity - 1);
        MemorySegment.copy(encoded, 0, buffer, ValueLayout.JAVA_BYTE, 0, length);
        buffer.set(ValueLayout.JAVA_BYTE, length, (byte) 0);
        return buffer;
    }

    private static MemorySegment row_text_upcall(RowTextHandler handler, Arena arena) {
        try {
            MemorySegment buffer = arena.allocate(row_text_capacity, 1);
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "row_text_bridge",
                            MethodType.methodType(MemorySegment.class, RowTextHandler.class,
                                    MemorySegment.class, MemorySegment.class, int.class,
                                    MemorySegment.class))
                    .bindTo(handler)
                    .bindTo(buffer);
            return linker.upcallStub(target, row_text_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create row text callback", error);
        }
    }

    private static void sort_columns_bridge(SortColumnsHandler handler, MemorySegment table,
            int count, MemorySegment columns, MemorySegment directions, MemorySegment data) {
        handler.handle(table, count, columns, directions, data);
    }

    private static MemorySegment sort_columns_upcall(SortColumnsHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class,
                    "sort_columns_bridge", MethodType.methodType(void.class,
                            SortColumnsHandler.class, MemorySegment.class, int.class,
                            MemorySegment.class, MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), sort_columns_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create sort columns callback", error);
        }
    }

    private static void table_edited_bridge(TableEditedHandler handler, MemorySegment table,
            int row, int column, MemorySegment text, MemorySegment data) {
        handler.handle(table, row, column,
                text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0), data);
    }

    private static MemorySegment table_edited_upcall(TableEditedHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class,
                    "table_edited_bridge", MethodType.methodType(void.class,
                            TableEditedHandler.class, MemorySegment.class, int.class, int.class,
                            MemorySegment.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), table_edited_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create table edit callback", error);
        }
    }

    private static MemorySegment table_reorder_upcall(TableReorderHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(TableReorderHandler.class,
                    "handle", MethodType.methodType(void.class, MemorySegment.class, int.class,
                            int.class, MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), table_reorder_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create column reorder callback", error);
        }
    }

    private static MemorySegment table_row_drag_upcall(TableRowDragHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findVirtual(TableRowDragHandler.class,
                    "handle", MethodType.methodType(void.class, MemorySegment.class, int.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), table_row_drag_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create row drag callback", error);
        }
    }

    private static void tree_rename_bridge(TreeRenameHandler handler, MemorySegment tree,
            MemorySegment node, MemorySegment text, MemorySegment data) {
        handler.handle(tree, node,
                text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0), data);
    }

    private static MemorySegment tree_rename_upcall(TreeRenameHandler handler, Arena arena) {
        try {
            MethodHandle target = MethodHandles.lookup().findStatic(Pt.class, "tree_rename_bridge",
                    MethodType.methodType(void.class, TreeRenameHandler.class,
                            MemorySegment.class, MemorySegment.class, MemorySegment.class,
                            MemorySegment.class));
            return linker.upcallStub(target.bindTo(handler), tree_rename_descriptor, arena);
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException("cannot create tree rename callback", error);
        }
    }

    /* ---------- public API ---------- */

    public static String last_error() {
        MemorySegment text = (MemorySegment) invoke(get_last_error);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static MemorySegment app_create(String app_id) {
        Objects.requireNonNull(app_id, "app_id");
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(app_create, cstring(arena, app_id));
        }
    }

    public static void app_destroy(MemorySegment app) {
        invoke(app_destroy, app);
    }

    public static int app_run(MemorySegment app) {
        return (int) invoke(app_run, app);
    }

    public static int app_run_for(MemorySegment app, int timeout_ms) {
        return (int) invoke(app_run_for, app, timeout_ms);
    }

    public static void app_quit(MemorySegment app) {
        invoke(app_quit, app);
    }

    public static boolean app_is_running(MemorySegment app) {
        return (boolean) invoke(app_is_running, app);
    }

    public static void app_set_color_scheme(MemorySegment app, int scheme) {
        invoke(app_set_color_scheme, app, scheme);
    }

    public static int app_get_color_scheme(MemorySegment app) {
        return (int) invoke(app_get_color_scheme, app);
    }

    public static boolean app_enable_a11y(MemorySegment app) {
        return (boolean) invoke(app_enable_a11y, app);
    }

    public static void app_disable_a11y(MemorySegment app) {
        invoke(app_disable_a11y, app);
    }

    public static int app_get_a11y_status(MemorySegment app) {
        return (int) invoke(app_get_a11y_status, app);
    }

    public static void app_set_accent_color(MemorySegment app, int color) {
        invoke(app_set_accent_color, app, color);
    }

    public static void app_set_font_family(MemorySegment app, String family) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(app_set_font_family, app, cstring(arena, family));
        }
    }

    public static void app_set_reduced_motion(MemorySegment app, boolean reduced) {
        invoke(app_set_reduced_motion, app, reduced);
    }

    public static boolean app_get_reduced_motion(MemorySegment app) {
        return (boolean) invoke(app_get_reduced_motion, app);
    }

    public static int app_get_theme_color(MemorySegment app, int token) {
        return (int) invoke(app_get_theme_color, app, token);
    }

    public static void app_set_theme_color(MemorySegment app, int token, int color) {
        invoke(app_set_theme_color, app, token, color);
    }

    public static void app_set_theme_metric(MemorySegment app, int metric, float value) {
        invoke(app_set_theme_metric, app, metric, value);
    }

    public static float app_get_theme_metric(MemorySegment app, int metric) {
        return (float) invoke(app_get_theme_metric, app, metric);
    }

    public static void app_set_quit_on_last_window_close(MemorySegment app, boolean enabled) {
        invoke(app_set_quit_on_last_window_close, app, enabled);
    }

    public static long app_add_timeout(MemorySegment app, int milliseconds, TimerHandler handler,
            Arena arena) {
        MemorySegment stub = timer_upcall(handler, arena);
        return (long) invoke(app_add_timeout, app, milliseconds, stub, MemorySegment.NULL);
    }

    public static void app_cancel_timeout(MemorySegment app, long timer_id) {
        invoke(app_cancel_timeout, app, timer_id);
    }

    public static void app_set_a11y_handler(MemorySegment app, A11yHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : a11y_upcall(handler, arena);
        invoke(app_set_a11y_callback, app, stub, MemorySegment.NULL);
    }

    public static MemorySegment window_create(MemorySegment app, String title, int width,
            int height) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(window_create, app, cstring(arena, title), width, height);
        }
    }

    public static void window_show(MemorySegment window) {
        invoke(window_show, window);
    }

    public static void window_hide(MemorySegment window) {
        invoke(window_hide, window);
    }

    public static void window_close(MemorySegment window) {
        invoke(window_close, window);
    }

    public static boolean window_is_open(MemorySegment window) {
        return (boolean) invoke(window_is_open, window);
    }

    public static MemorySegment window_root(MemorySegment window) {
        return (MemorySegment) invoke(window_root, window);
    }

    public static MemorySegment window_app(MemorySegment window) {
        return (MemorySegment) invoke(window_app, window);
    }

    public static int window_get_width(MemorySegment window) {
        return (int) invoke(window_get_width, window);
    }

    public static int window_get_height(MemorySegment window) {
        return (int) invoke(window_get_height, window);
    }

    public static float window_get_scale(MemorySegment window) {
        return (float) invoke(window_get_scale, window);
    }

    public static void window_set_resizable(MemorySegment window, boolean resizable) {
        invoke(window_set_resizable, window, resizable);
    }

    public static void window_set_resize_handler(MemorySegment window, ResizeHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : resize_upcall(handler, arena);
        invoke(window_set_resize_callback, window, stub, MemorySegment.NULL);
    }

    public static void window_set_popup_policy(MemorySegment window, int policy) {
        invoke(window_set_popup_policy, window, policy);
    }

    public static int window_get_popup_policy(MemorySegment window) {
        return (int) invoke(window_get_popup_policy, window);
    }

    public static boolean window_get_drag_active(MemorySegment window) {
        return (boolean) invoke(window_get_drag_active, window);
    }

    public static void window_cancel_drag(MemorySegment window) {
        invoke(window_cancel_drag, window);
    }

    public static void window_set_drag_timeout(MemorySegment window, int milliseconds) {
        invoke(window_set_drag_timeout, window, milliseconds);
    }

    public static int window_get_drag_timeout(MemorySegment window) {
        return (int) invoke(window_get_drag_timeout, window);
    }

    public static int window_get_drag_result(MemorySegment window) {
        return (int) invoke(window_get_drag_result, window);
    }

    public static void window_set_drag_finished_handler(MemorySegment window,
            DragFinishedHandler handler, Arena arena) {
        MemorySegment stub =
                handler == null ? MemorySegment.NULL : drag_finished_upcall(handler, arena);
        invoke(window_set_drag_finished_callback, window, stub, MemorySegment.NULL);
    }

    public static void window_set_title(MemorySegment window, String title) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(window_set_title, window, cstring(arena, title));
        }
    }

    public static void window_set_size(MemorySegment window, int width, int height) {
        invoke(window_set_size, window, width, height);
    }

    public static void window_set_min_size(MemorySegment window, int width, int height) {
        invoke(window_set_min_size, window, width, height);
    }

    public static void window_set_decorated(MemorySegment window, boolean decorated) {
        invoke(window_set_decorated, window, decorated);
    }

    public static boolean window_clipboard_set(MemorySegment window, String text) {
        try (Arena arena = Arena.ofConfined()) {
            return (boolean) invoke(window_clipboard_set, window, cstring(arena, text));
        }
    }

    public static String window_clipboard_get(MemorySegment window) {
        MemorySegment text = (MemorySegment) invoke(window_clipboard_get, window);
        if (text.address() == 0) {
            return "";
        }
        try {
            return text.reinterpret(Long.MAX_VALUE).getString(0);
        } finally {
            invoke(free, text);
        }
    }

    public static void window_request_redraw(MemorySegment window) {
        invoke(window_request_redraw, window);
    }

    public static void window_focus(MemorySegment window, MemorySegment widget) {
        invoke(window_focus, window, widget);
    }

    public static void window_set_theme_color(MemorySegment window, int token, int color) {
        invoke(window_set_theme_color, window, token, color);
    }

    public static int window_get_theme_color(MemorySegment window, int token) {
        return (int) invoke(window_get_theme_color, window, token);
    }

    public static void window_set_theme_metric(MemorySegment window, int metric, float value) {
        invoke(window_set_theme_metric, window, metric, value);
    }

    public static float window_get_theme_metric(MemorySegment window, int metric) {
        return (float) invoke(window_get_theme_metric, window, metric);
    }

    public static void window_clear_theme_overrides(MemorySegment window) {
        invoke(window_clear_theme_overrides, window);
    }

    public static void window_set_close_handler(MemorySegment window, CloseHandler handler,
            Arena arena) {
        MemorySegment stub = close_upcall(handler, arena);
        invoke(window_set_close_callback, window, stub, MemorySegment.NULL);
    }

    public static void window_set_key_handler(MemorySegment window, KeyHandler handler,
            Arena arena) {
        MemorySegment stub = key_upcall(handler, arena);
        invoke(window_set_key_callback, window, stub, MemorySegment.NULL);
    }

    public static void window_set_gesture_handler(MemorySegment window, GestureHandler handler,
            Arena arena) {
        MemorySegment stub = gesture_upcall(handler, arena);
        invoke(window_set_gesture_callback, window, stub, MemorySegment.NULL);
    }

    public static void window_set_long_press_delay(MemorySegment window, int milliseconds) {
        invoke(window_set_long_press_delay, window, milliseconds);
    }

    public static void window_set_momentum_scrolling(MemorySegment window, boolean enabled) {
        invoke(window_set_momentum_scrolling, window, enabled);
    }

    public static boolean window_get_momentum_scrolling(MemorySegment window) {
        return (boolean) invoke(window_get_momentum_scrolling, window);
    }

    public static boolean window_begin_drag(MemorySegment window, DragData[] entries) {
        Objects.requireNonNull(entries, "entries");
        if (entries.length == 0) {
            return (boolean) invoke(window_begin_drag, window, MemorySegment.NULL, 0);
        }
        long pointer_size = ValueLayout.ADDRESS.byteSize();
        long stride = drag_data_layout.byteSize();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment array = arena.allocate(drag_data_layout, entries.length);
            for (int index = 0; index < entries.length; ++index) {
                DragData entry = Objects.requireNonNull(entries[index], "drag entry");
                MemorySegment slot = array.asSlice(index * stride, stride);
                slot.set(ValueLayout.ADDRESS, 0, entry.mime_type() == null ? MemorySegment.NULL
                        : cstring(arena, entry.mime_type()));
                slot.set(ValueLayout.ADDRESS, pointer_size,
                        entry.data() == null ? MemorySegment.NULL : cstring(arena, entry.data()));
                slot.set(ValueLayout.JAVA_LONG, pointer_size * 2, Math.max(0L, entry.size()));
            }
            return (boolean) invoke(window_begin_drag, window, array, entries.length);
        }
    }

    public static void window_set_drag_image(MemorySegment window, int[] pixels, int width,
            int height) {
        Objects.requireNonNull(pixels, "pixels");
        height = Math.max(0, height);
        if (width <= 0 || height == 0) {
            invoke(window_clear_drag_image, window);
            return;
        }
        if (pixels.length < width * height) {
            throw new IllegalArgumentException("drag image has fewer pixels than width * height");
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment image = arena.allocate(ValueLayout.JAVA_INT, width * height);
            for (int index = 0; index < width * height; ++index) {
                image.setAtIndex(ValueLayout.JAVA_INT, index, pixels[index]);
            }
            invoke(window_set_drag_image, window, image, width, height);
        }
    }

    public static void window_clear_drag_image(MemorySegment window) {
        invoke(window_clear_drag_image, window);
    }

    public static void window_set_stylus_handler(MemorySegment window, StylusHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : stylus_upcall(handler, arena);
        invoke(window_set_stylus_callback, window, stub, MemorySegment.NULL);
    }

    public static int uri_list_count(String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            return (int) invoke(uri_list_count, cstring(arena, text));
        }
    }

    public static String uri_list_get(String text, int index) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment uri = (MemorySegment) invoke(uri_list_get, cstring(arena, text), index);
            if (uri.address() == 0) {
                return null;
            }
            try {
                return uri.reinterpret(Long.MAX_VALUE).getString(0);
            } finally {
                invoke(free, uri);
            }
        }
    }

    public static String[] uri_list(String text) {
        int count = uri_list_count(text);
        String[] uris = new String[count];
        for (int index = 0; index < count; ++index) {
            uris[index] = uri_list_get(text, index);
        }
        return uris;
    }

    public static MemorySegment menu_create(MemorySegment window) {
        return (MemorySegment) invoke(menu_create, window);
    }

    public static void menu_destroy(MemorySegment menu) {
        invoke(menu_destroy, menu);
    }

    public static void menu_add_item(MemorySegment menu, String label, String shortcut,
            ClickHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : click_upcall(handler, arena);
        try (Arena local = Arena.ofConfined()) {
            MemorySegment shortcut_text = shortcut == null || shortcut.isEmpty()
                    ? MemorySegment.NULL
                    : cstring(local, shortcut);
            invoke(menu_add_item, menu, cstring(local, label), shortcut_text, stub,
                    MemorySegment.NULL);
        }
    }

    public static void menu_add_separator(MemorySegment menu) {
        invoke(menu_add_separator, menu);
    }

    public static void menu_show_at(MemorySegment menu, float x, float y) {
        invoke(menu_show_at, menu, x, y);
    }

    public static void menu_show_below(MemorySegment menu, MemorySegment anchor) {
        invoke(menu_show_below, menu, anchor);
    }

    public static void menu_dismiss(MemorySegment menu) {
        invoke(menu_dismiss, menu);
    }

    public static boolean menu_is_open(MemorySegment menu) {
        return (boolean) invoke(menu_is_open, menu);
    }

    public static MemorySegment dialog_create(MemorySegment window, String title, String message) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(dialog_create, window, cstring(arena, title),
                    cstring(arena, message));
        }
    }

    public static void dialog_destroy(MemorySegment dialog) {
        invoke(dialog_destroy, dialog);
    }

    public static void dialog_set_title(MemorySegment dialog, String title) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(dialog_set_title, dialog, cstring(arena, title));
        }
    }

    public static void dialog_set_message(MemorySegment dialog, String message) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(dialog_set_message, dialog, cstring(arena, message));
        }
    }

    public static void dialog_add_action(MemorySegment dialog, String label, boolean primary,
            ClickHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : click_upcall(handler, arena);
        try (Arena local = Arena.ofConfined()) {
            invoke(dialog_add_action, dialog, cstring(local, label), primary, stub,
                    MemorySegment.NULL);
        }
    }

    public static void dialog_show(MemorySegment dialog) {
        invoke(dialog_show, dialog);
    }

    public static void dialog_close(MemorySegment dialog) {
        invoke(dialog_close, dialog);
    }

    public static boolean dialog_is_open(MemorySegment dialog) {
        return (boolean) invoke(dialog_is_open, dialog);
    }

    public static void dialog_set_on_close(MemorySegment dialog, DialogHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : dialog_upcall(handler, arena);
        invoke(dialog_set_on_close, dialog, stub, MemorySegment.NULL);
    }

    public static MemorySegment window_show_toast(MemorySegment window, String text, int kind,
            int duration_ms) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(window_show_toast, window, cstring(arena, text), kind,
                    duration_ms);
        }
    }

    public static void toast_dismiss(MemorySegment toast) {
        invoke(toast_dismiss, toast);
    }

    public static boolean toast_is_visible(MemorySegment toast) {
        return (boolean) invoke(toast_is_visible, toast);
    }

    public static MemorySegment box(MemorySegment parent, int direction) {
        return (MemorySegment) invoke(box_create, parent, direction);
    }

    public static MemorySegment row(MemorySegment parent) {
        return (MemorySegment) invoke(row_create, parent);
    }

    public static MemorySegment column(MemorySegment parent) {
        return (MemorySegment) invoke(column_create, parent);
    }

    public static MemorySegment label(MemorySegment parent, String text) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(label_create, parent, cstring(arena, text));
        }
    }

    public static MemorySegment button(MemorySegment parent, String text) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(button_create, parent, cstring(arena, text));
        }
    }

    public static MemorySegment entry(MemorySegment parent, String placeholder) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(entry_create, parent, cstring(arena, placeholder));
        }
    }

    public static MemorySegment text_area(MemorySegment parent, String placeholder) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(text_area_create, parent, cstring(arena, placeholder));
        }
    }

    public static MemorySegment wrap(MemorySegment parent) {
        return (MemorySegment) invoke(wrap_create, parent);
    }

    public static MemorySegment grid(MemorySegment parent, int columns) {
        return (MemorySegment) invoke(grid_create, parent, columns);
    }

    public static MemorySegment list_view(MemorySegment parent) {
        return (MemorySegment) invoke(list_view_create, parent);
    }

    public static MemorySegment icon(MemorySegment parent, int glyph) {
        return (MemorySegment) invoke(icon_create, parent, glyph);
    }

    public static MemorySegment icon_button(MemorySegment parent, int glyph,
            String accessible_name) {
        Objects.requireNonNull(accessible_name, "accessible_name");
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(icon_button_create, parent, glyph,
                    cstring(arena, accessible_name));
        }
    }

    public static MemorySegment table(MemorySegment parent) {
        return (MemorySegment) invoke(table_create, parent);
    }

    public static MemorySegment tree(MemorySegment parent) {
        return (MemorySegment) invoke(tree_create, parent);
    }

    public static MemorySegment tabs(MemorySegment parent) {
        return (MemorySegment) invoke(tabs_create, parent);
    }

    public static MemorySegment sidebar(MemorySegment parent) {
        return (MemorySegment) invoke(sidebar_create, parent);
    }

    public static MemorySegment toggle(MemorySegment parent) {
        return (MemorySegment) invoke(toggle_create, parent);
    }

    public static MemorySegment checkbox(MemorySegment parent, String text) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(checkbox_create, parent, cstring(arena, text));
        }
    }

    public static MemorySegment radio(MemorySegment parent, String text) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(radio_create, parent, cstring(arena, text));
        }
    }

    public static MemorySegment slider(MemorySegment parent) {
        return (MemorySegment) invoke(slider_create, parent);
    }

    public static MemorySegment progress(MemorySegment parent) {
        return (MemorySegment) invoke(progress_create, parent);
    }

    public static MemorySegment spinner(MemorySegment parent) {
        return (MemorySegment) invoke(spinner_create, parent);
    }

    public static MemorySegment segmented(MemorySegment parent, String[] labels) {
        Objects.requireNonNull(labels, "labels");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment array = arena.allocate(ValueLayout.ADDRESS, labels.length);
            for (int index = 0; index < labels.length; index++) {
                array.setAtIndex(ValueLayout.ADDRESS, index, cstring(arena, labels[index]));
            }
            return (MemorySegment) invoke(segmented_create, parent, array, labels.length);
        }
    }

    public static MemorySegment scroll(MemorySegment parent) {
        return (MemorySegment) invoke(scroll_create, parent);
    }

    public static MemorySegment divider(MemorySegment parent) {
        return (MemorySegment) invoke(divider_create, parent);
    }

    public static MemorySegment card(MemorySegment parent) {
        return (MemorySegment) invoke(card_create, parent);
    }

    public static MemorySegment spacer(MemorySegment parent) {
        return (MemorySegment) invoke(spacer_create, parent);
    }

    public static MemorySegment chip(MemorySegment parent, String text) {
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(chip_create, parent, cstring(arena, text));
        }
    }

    public static MemorySegment skeleton(MemorySegment parent) {
        return (MemorySegment) invoke(skeleton_create, parent);
    }

    public static void widget_set_text(MemorySegment widget, String text) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(widget_set_text, widget, cstring(arena, text));
        }
    }

    public static String widget_get_text(MemorySegment widget) {
        MemorySegment text = (MemorySegment) invoke(widget_get_text, widget);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static boolean widget_set_text_spans(MemorySegment widget, TextSpan[] spans) {
        if (spans == null || spans.length == 0) {
            return (boolean) invoke(widget_set_text_spans, widget, MemorySegment.NULL, 0);
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment array = arena.allocate(text_span_layout, spans.length);
            for (int i = 0; i < spans.length; ++i) {
                TextSpan span = spans[i];
                MemorySegment slot =
                        array.asSlice(i * text_span_layout.byteSize(), text_span_layout.byteSize());
                slot.set(ValueLayout.JAVA_INT, 0, span.start());
                slot.set(ValueLayout.JAVA_INT, 4, span.length());
                slot.set(ValueLayout.JAVA_INT, 8, span.weight());
                slot.set(ValueLayout.JAVA_INT, 12, span.slant());
                slot.set(ValueLayout.JAVA_INT, 16, span.color());
                slot.set(ValueLayout.JAVA_INT, 20, span.flags());
                MemorySegment family = slot.asSlice(24, 64);
                family.fill((byte) 0);
                String name = span.family();
                if (name != null && !name.isEmpty()) {
                    byte[] encoded = name.getBytes(StandardCharsets.UTF_8);
                    MemorySegment source = MemorySegment.ofArray(encoded);
                    MemorySegment.copy(source, 0, family, 0, Math.min(encoded.length, 63));
                }
                slot.set(ValueLayout.JAVA_FLOAT, 88, span.size());
            }
            return (boolean) invoke(widget_set_text_spans, widget, array, spans.length);
        }
    }

    public static void widget_clear_text_spans(MemorySegment widget) {
        invoke(widget_clear_text_spans, widget);
    }

    public static int widget_get_text_span_count(MemorySegment widget) {
        return (int) invoke(widget_get_text_span_count, widget);
    }

    public static TextSpan widget_get_text_span(MemorySegment widget, int index) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(text_span_layout);
            if (!(boolean) invoke(widget_get_text_span, widget, index, out)) {
                return null;
            }
            return new TextSpan(out.get(ValueLayout.JAVA_INT, 0),
                    out.get(ValueLayout.JAVA_INT, 4), out.get(ValueLayout.JAVA_INT, 8),
                    out.get(ValueLayout.JAVA_INT, 12), out.get(ValueLayout.JAVA_INT, 16),
                    out.get(ValueLayout.JAVA_INT, 20), read_family(out), out.get(
                            ValueLayout.JAVA_FLOAT, 88));
        }
    }

    private static String read_family(MemorySegment span) {
        byte[] raw = span.asSlice(24, 64).toArray(ValueLayout.JAVA_BYTE);
        int length = 0;
        while (length < raw.length && raw[length] != 0) {
            ++length;
        }
        return new String(raw, 0, length, StandardCharsets.UTF_8);
    }

    public static void widget_set_input_purpose(MemorySegment widget, int purpose) {
        invoke(widget_set_input_purpose, widget, purpose);
    }

    public static int widget_get_input_purpose(MemorySegment widget) {
        return (int) invoke(widget_get_input_purpose, widget);
    }

    public static void widget_set_size(MemorySegment widget, float width, float height) {
        invoke(widget_set_size, widget, width, height);
    }

    public static void widget_set_width(MemorySegment widget, float width) {
        invoke(widget_set_width, widget, width);
    }

    public static void widget_set_height(MemorySegment widget, float height) {
        invoke(widget_set_height, widget, height);
    }

    public static void widget_set_min_width(MemorySegment widget, float width) {
        invoke(widget_set_min_width, widget, width);
    }

    public static void widget_set_min_height(MemorySegment widget, float height) {
        invoke(widget_set_min_height, widget, height);
    }

    public static void widget_set_max_width(MemorySegment widget, float width) {
        invoke(widget_set_max_width, widget, width);
    }

    public static void widget_set_max_height(MemorySegment widget, float height) {
        invoke(widget_set_max_height, widget, height);
    }

    public static void widget_destroy(MemorySegment widget) {
        invoke(widget_destroy, widget);
    }

    public static void widget_clear_children(MemorySegment widget) {
        invoke(widget_clear_children, widget);
    }

    public static MemorySegment widget_parent(MemorySegment widget) {
        return (MemorySegment) invoke(widget_parent, widget);
    }

    public static int widget_get_kind(MemorySegment widget) {
        return (int) invoke(widget_get_kind, widget);
    }

    /** Returns {x, y, width, height} in window logical pixels. */
    public static float[] widget_get_bounds(MemorySegment widget) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment x = arena.allocate(f32);
            MemorySegment y = arena.allocate(f32);
            MemorySegment width = arena.allocate(f32);
            MemorySegment height = arena.allocate(f32);
            invoke(widget_get_bounds, widget, x, y, width, height);
            return new float[] {x.get(f32, 0), y.get(f32, 0), width.get(f32, 0),
                    height.get(f32, 0)};
        }
    }

    public static boolean widget_has_focus(MemorySegment widget) {
        return (boolean) invoke(widget_has_focus, widget);
    }

    public static void widget_set_margin(MemorySegment widget, float top, float right,
            float bottom, float left) {
        invoke(widget_set_margin, widget, top, right, bottom, left);
    }

    public static void widget_set_padding(MemorySegment widget, float top, float right,
            float bottom, float left) {
        invoke(widget_set_padding, widget, top, right, bottom, left);
    }

    public static void widget_set_gap(MemorySegment widget, float gap) {
        invoke(widget_set_gap, widget, gap);
    }

    public static void widget_set_grow(MemorySegment widget, float grow) {
        invoke(widget_set_grow, widget, grow);
    }

    public static void widget_set_align_self(MemorySegment widget, int align) {
        invoke(widget_set_align_self, widget, align);
    }

    public static void box_set_direction(MemorySegment box, int direction) {
        invoke(box_set_direction, box, direction);
    }

    public static void box_set_align(MemorySegment box, int align) {
        invoke(box_set_align, box, align);
    }

    public static void box_set_justify(MemorySegment box, int justify) {
        invoke(box_set_justify, box, justify);
    }

    public static void box_set_gap(MemorySegment box, float gap) {
        invoke(box_set_gap, box, gap);
    }

    public static void widget_set_background(MemorySegment widget, int color) {
        invoke(widget_set_background, widget, color);
    }

    public static void widget_set_foreground(MemorySegment widget, int color) {
        invoke(widget_set_foreground, widget, color);
    }

    public static void widget_set_border_color(MemorySegment widget, int color) {
        invoke(widget_set_border_color, widget, color);
    }

    public static void widget_set_border_width(MemorySegment widget, float width) {
        invoke(widget_set_border_width, widget, width);
    }

    public static void widget_set_radius(MemorySegment widget, float radius) {
        invoke(widget_set_radius, widget, radius);
    }

    public static void widget_set_font_size(MemorySegment widget, float size) {
        invoke(widget_set_font_size, widget, size);
    }

    public static void widget_set_font_weight(MemorySegment widget, int weight) {
        invoke(widget_set_font_weight, widget, weight);
    }

    public static void widget_set_text_align(MemorySegment widget, int align) {
        invoke(widget_set_text_align, widget, align);
    }

    public static void widget_set_visible(MemorySegment widget, boolean visible) {
        invoke(widget_set_visible, widget, visible);
    }

    public static void widget_set_enabled(MemorySegment widget, boolean enabled) {
        invoke(widget_set_enabled, widget, enabled);
    }

    public static void widget_set_busy(MemorySegment widget, boolean busy) {
        invoke(widget_set_busy, widget, busy);
    }

    public static boolean widget_get_busy(MemorySegment widget) {
        return (boolean) invoke(widget_get_busy, widget);
    }

    public static void widget_set_invalid(MemorySegment widget, boolean invalid) {
        invoke(widget_set_invalid, widget, invalid);
    }

    public static boolean widget_get_invalid(MemorySegment widget) {
        return (boolean) invoke(widget_get_invalid, widget);
    }

    public static void widget_set_wrap(MemorySegment widget, boolean wrap) {
        invoke(widget_set_label_wrap, widget, wrap);
    }

    public static void button_set_variant(MemorySegment button, int variant) {
        invoke(button_set_variant, button, variant);
    }

    public static void button_set_on_click(MemorySegment button, ClickHandler handler,
            Arena arena) {
        MemorySegment stub = click_upcall(handler, arena);
        invoke(button_set_on_click, button, stub, MemorySegment.NULL);
    }

    public static void chip_set_icon(MemorySegment chip, int glyph) {
        invoke(chip_set_icon, chip, glyph);
    }

    public static void chip_set_removable(MemorySegment chip, boolean removable) {
        invoke(chip_set_removable, chip, removable);
    }

    public static boolean chip_get_removable(MemorySegment chip) {
        return (boolean) invoke(chip_get_removable, chip);
    }

    public static void chip_set_selected(MemorySegment chip, boolean selected) {
        invoke(chip_set_selected, chip, selected);
    }

    public static boolean chip_get_selected(MemorySegment chip) {
        return (boolean) invoke(chip_get_selected, chip);
    }

    public static void chip_set_on_click(MemorySegment chip, ClickHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : click_upcall(handler, arena);
        invoke(chip_set_on_click, chip, stub, MemorySegment.NULL);
    }

    public static void chip_set_on_remove(MemorySegment chip, ClickHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : click_upcall(handler, arena);
        invoke(chip_set_on_remove, chip, stub, MemorySegment.NULL);
    }

    public static void entry_set_placeholder(MemorySegment entry, String placeholder) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(entry_set_placeholder, entry, cstring(arena, placeholder));
        }
    }

    public static void entry_set_password(MemorySegment entry, boolean password) {
        invoke(entry_set_password, entry, password);
    }

    public static void entry_set_on_change(MemorySegment entry, TextHandler handler,
            Arena arena) {
        MemorySegment stub = text_upcall(handler, arena);
        invoke(entry_set_on_change, entry, stub, MemorySegment.NULL);
    }

    public static void entry_set_on_submit(MemorySegment entry, ClickHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : click_upcall(handler, arena);
        invoke(entry_set_on_submit, entry, stub, MemorySegment.NULL);
    }

    public static void entry_select_all(MemorySegment entry) {
        invoke(entry_select_all, entry);
    }

    public static void entry_set_selection(MemorySegment entry, int start, int end) {
        invoke(entry_set_selection, entry, start, end);
    }

    public static int[] entry_get_selection(MemorySegment entry) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment start = arena.allocate(i32);
            MemorySegment end = arena.allocate(i32);
            boolean selected = (boolean) invoke(entry_get_selection, entry, start, end);
            if (!selected) {
                return null;
            }
            return new int[] {start.get(i32, 0), end.get(i32, 0)};
        }
    }

    public static boolean entry_undo(MemorySegment entry) {
        return (boolean) invoke(entry_undo, entry);
    }

    public static boolean entry_redo(MemorySegment entry) {
        return (boolean) invoke(entry_redo, entry);
    }

    public static void text_area_set_placeholder(MemorySegment area, String placeholder) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(text_area_set_placeholder, area, cstring(arena, placeholder));
        }
    }

    public static void text_area_set_on_change(MemorySegment area, TextHandler handler,
            Arena arena) {
        MemorySegment stub = text_upcall(handler, arena);
        invoke(text_area_set_on_change, area, stub, MemorySegment.NULL);
    }

    public static void text_area_set_grow_to_content(MemorySegment area, boolean enabled) {
        invoke(text_area_set_grow_to_content, area, enabled);
    }

    public static boolean text_area_get_grow_to_content(MemorySegment area) {
        return (boolean) invoke(text_area_get_grow_to_content, area);
    }

    public static void text_area_scroll_to_caret(MemorySegment area) {
        invoke(text_area_scroll_to_caret, area);
    }

    public static void text_area_select_all(MemorySegment area) {
        invoke(text_area_select_all, area);
    }

    public static void text_area_set_selection(MemorySegment area, int start, int end) {
        invoke(text_area_set_selection, area, start, end);
    }

    public static int[] text_area_get_selection(MemorySegment area) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment start = arena.allocate(i32);
            MemorySegment end = arena.allocate(i32);
            boolean selected = (boolean) invoke(text_area_get_selection, area, start, end);
            if (!selected) {
                return null;
            }
            return new int[] {start.get(i32, 0), end.get(i32, 0)};
        }
    }

    public static boolean text_area_undo(MemorySegment area) {
        return (boolean) invoke(text_area_undo, area);
    }

    public static boolean text_area_redo(MemorySegment area) {
        return (boolean) invoke(text_area_redo, area);
    }

    public static void wrap_set_gap(MemorySegment wrap, float row_gap, float column_gap) {
        invoke(wrap_set_gap, wrap, row_gap, column_gap);
    }

    public static void grid_set_columns(MemorySegment grid, int columns) {
        invoke(grid_set_columns, grid, columns);
    }

    public static int grid_get_columns(MemorySegment grid) {
        return (int) invoke(grid_get_columns, grid);
    }

    public static void grid_set_gap(MemorySegment grid, float row_gap, float column_gap) {
        invoke(grid_set_gap, grid, row_gap, column_gap);
    }

    public static void widget_set_grid_span(MemorySegment widget, int span) {
        invoke(widget_set_grid_span, widget, span);
    }

    public static int widget_get_grid_span(MemorySegment widget) {
        return (int) invoke(widget_get_grid_span, widget);
    }

    public static void list_view_set_row_count(MemorySegment list, int count) {
        invoke(list_view_set_row_count, list, count);
    }

    public static int list_view_get_row_count(MemorySegment list) {
        return (int) invoke(list_view_get_row_count, list);
    }

    public static void list_view_set_row_height(MemorySegment list, float height) {
        invoke(list_view_set_row_height, list, height);
    }

    public static float list_view_get_row_height(MemorySegment list) {
        return (float) invoke(list_view_get_row_height, list);
    }

    public static void list_view_set_on_bind_row(MemorySegment list, ListBindHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : list_bind_upcall(handler, arena);
        invoke(list_view_set_on_bind_row, list, stub, MemorySegment.NULL);
    }

    public static void list_view_set_on_select(MemorySegment list, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : index_upcall(handler, arena);
        invoke(list_view_set_on_select, list, stub, MemorySegment.NULL);
    }

    public static void list_view_set_on_activate(MemorySegment list, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : index_upcall(handler, arena);
        invoke(list_view_set_on_activate, list, stub, MemorySegment.NULL);
    }

    public static int list_view_get_selected(MemorySegment list) {
        return (int) invoke(list_view_get_selected, list);
    }

    public static void list_view_set_selected(MemorySegment list, int index) {
        invoke(list_view_set_selected, list, index);
    }

    public static void list_view_refresh(MemorySegment list) {
        invoke(list_view_refresh, list);
    }

    public static void list_view_refresh_rows(MemorySegment list, int first, int count) {
        invoke(list_view_refresh_rows, list, first, count);
    }

    public static void list_view_scroll_to_row(MemorySegment list, int index) {
        invoke(list_view_scroll_to_row, list, index);
    }

    public static void list_view_set_row_key_handler(MemorySegment list, RowKeyHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : row_key_upcall(handler, arena);
        invoke(list_view_set_row_key_callback, list, stub, MemorySegment.NULL);
    }

    public static long list_view_get_selected_key(MemorySegment list) {
        return (long) invoke(list_view_get_selected_key, list);
    }

    public static boolean list_view_set_selected_key(MemorySegment list, long key) {
        return (boolean) invoke(list_view_set_selected_key, list, key);
    }

    public static void list_view_set_row_text_handler(MemorySegment list, RowTextHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : row_text_upcall(handler, arena);
        invoke(list_view_set_row_text_callback, list, stub, MemorySegment.NULL);
    }

    public static int list_view_get_selection_count(MemorySegment list) {
        return (int) invoke(list_view_get_selection_count, list);
    }

    public static boolean list_view_is_row_selected(MemorySegment list, int row) {
        return (boolean) invoke(list_view_is_row_selected, list, row);
    }

    public static int[] list_view_get_selection(MemorySegment list) {
        int count = list_view_get_selection_count(list);
        if (count <= 0) {
            return new int[0];
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rows = arena.allocate(ValueLayout.JAVA_INT, count);
            int written = (int) invoke(list_view_get_selection, list, rows, count);
            int[] result = new int[written];
            for (int index = 0; index < written; ++index) {
                result[index] = rows.getAtIndex(ValueLayout.JAVA_INT, index);
            }
            return result;
        }
    }

    public static void list_view_select_range(MemorySegment list, int first, int last) {
        invoke(list_view_select_range, list, first, last);
    }

    public static void list_view_select_toggle(MemorySegment list, int row) {
        invoke(list_view_select_toggle, list, row);
    }

    public static void list_view_clear_selection(MemorySegment list) {
        invoke(list_view_clear_selection, list);
    }

    /* ---------- icons, tables, trees, tabs and sidebars ---------- */

    public static void widget_set_icon(MemorySegment widget, int glyph) {
        invoke(widget_set_icon, widget, glyph);
    }

    public static int widget_get_icon(MemorySegment widget) {
        return (int) invoke(widget_get_icon, widget);
    }

    public static void icon_set_size(MemorySegment icon, float size) {
        invoke(icon_set_size, icon, size);
    }

    public static float icon_get_size(MemorySegment icon) {
        return (float) invoke(icon_get_size, icon);
    }

    public static void table_set_column_count(MemorySegment table, int count) {
        invoke(table_set_column_count, table, count);
    }

    public static int table_get_column_count(MemorySegment table) {
        return (int) invoke(table_get_column_count, table);
    }

    public static void table_set_column_title(MemorySegment table, int column, String title) {
        Objects.requireNonNull(title, "title");
        try (Arena arena = Arena.ofConfined()) {
            invoke(table_set_column_title, table, column, cstring(arena, title));
        }
    }

    public static void table_set_column_width(MemorySegment table, int column, float width) {
        invoke(table_set_column_width, table, column, width);
    }

    public static float table_get_column_width(MemorySegment table, int column) {
        return (float) invoke(table_get_column_width, table, column);
    }

    public static void table_set_column_align(MemorySegment table, int column, int align) {
        invoke(table_set_column_align, table, column, align);
    }

    public static void table_set_column_resizable(MemorySegment table, int column,
            boolean resizable) {
        invoke(table_set_column_resizable, table, column, resizable);
    }

    public static void table_set_column_hidden(MemorySegment table, int column,
            boolean hidden) {
        invoke(table_set_column_hidden, table, column, hidden);
    }

    public static boolean table_get_column_hidden(MemorySegment table, int column) {
        return (boolean) invoke(table_get_column_hidden, table, column);
    }

    public static void table_set_row_count(MemorySegment table, int count) {
        invoke(table_set_row_count, table, count);
    }

    public static int table_get_row_count(MemorySegment table) {
        return (int) invoke(table_get_row_count, table);
    }

    public static void table_set_row_height(MemorySegment table, float height) {
        invoke(table_set_row_height, table, height);
    }

    public static float table_get_row_height(MemorySegment table) {
        return (float) invoke(table_get_row_height, table);
    }

    public static void table_set_header_height(MemorySegment table, float height) {
        invoke(table_set_header_height, table, height);
    }

    public static void table_set_on_bind_cell(MemorySegment table, TableBindHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : table_bind_upcall(handler, arena);
        invoke(table_set_on_bind_cell, table, stub, MemorySegment.NULL);
    }

    public static void table_set_on_select(MemorySegment table, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : index_upcall(handler, arena);
        invoke(table_set_on_select, table, stub, MemorySegment.NULL);
    }

    public static void table_set_on_activate(MemorySegment table, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : index_upcall(handler, arena);
        invoke(table_set_on_activate, table, stub, MemorySegment.NULL);
    }

    public static void table_set_on_sort(MemorySegment table, SortHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : sort_upcall(handler, arena);
        invoke(table_set_on_sort, table, stub, MemorySegment.NULL);
    }

    public static void table_set_on_sort_columns(MemorySegment table, SortColumnsHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : sort_columns_upcall(handler, arena);
        invoke(table_set_on_sort_columns, table, stub, MemorySegment.NULL);
    }

    /** Returns {direction, column} for the current table sort. */
    public static int[] table_get_sort(MemorySegment table) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment column = arena.allocate(i32);
            int direction = (int) invoke(table_get_sort, table, column);
            return new int[] {direction, column.get(i32, 0)};
        }
    }

    public static void table_set_sort(MemorySegment table, int column, int direction) {
        invoke(table_set_sort, table, column, direction);
    }

    /** Returns the sort columns as {column0, direction0, column1, direction1, ...}. */
    public static int[] table_get_sort_columns(MemorySegment table) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment columns = arena.allocate(ValueLayout.JAVA_INT,
                    table_max_sort_columns);
            MemorySegment directions = arena.allocate(ValueLayout.JAVA_INT,
                    table_max_sort_columns);
            int count = (int) invoke(table_get_sort_columns, table, columns, directions);
            int[] result = new int[count * 2];
            for (int index = 0; index < count; ++index) {
                result[index * 2] = columns.getAtIndex(ValueLayout.JAVA_INT, index);
                result[index * 2 + 1] = directions.getAtIndex(ValueLayout.JAVA_INT, index);
            }
            return result;
        }
    }

    public static void table_set_sort_columns(MemorySegment table, int[] columns,
            int[] directions) {
        Objects.requireNonNull(columns, "columns");
        Objects.requireNonNull(directions, "directions");
        int count = Math.min(Math.min(columns.length, directions.length),
                table_max_sort_columns);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment columnArray = arena.allocate(ValueLayout.JAVA_INT, count);
            MemorySegment directionArray = arena.allocate(ValueLayout.JAVA_INT, count);
            for (int index = 0; index < count; ++index) {
                columnArray.setAtIndex(ValueLayout.JAVA_INT, index, columns[index]);
                directionArray.setAtIndex(ValueLayout.JAVA_INT, index, directions[index]);
            }
            invoke(table_set_sort_columns, table, count, columnArray, directionArray);
        }
    }

    public static int table_get_selected(MemorySegment table) {
        return (int) invoke(table_get_selected, table);
    }

    public static void table_set_selected(MemorySegment table, int row) {
        invoke(table_set_selected, table, row);
    }

    public static void table_set_row_key_handler(MemorySegment table, RowKeyHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : row_key_upcall(handler, arena);
        invoke(table_set_row_key_callback, table, stub, MemorySegment.NULL);
    }

    public static long table_get_selected_key(MemorySegment table) {
        return (long) invoke(table_get_selected_key, table);
    }

    public static boolean table_set_selected_key(MemorySegment table, long key) {
        return (boolean) invoke(table_set_selected_key, table, key);
    }

    public static void table_set_row_text_handler(MemorySegment table, RowTextHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : row_text_upcall(handler, arena);
        invoke(table_set_row_text_callback, table, stub, MemorySegment.NULL);
    }

    public static void table_refresh(MemorySegment table) {
        invoke(table_refresh, table);
    }

    public static void table_refresh_rows(MemorySegment table, int first, int count) {
        invoke(table_refresh_rows, table, first, count);
    }

    public static void table_scroll_to_row(MemorySegment table, int row) {
        invoke(table_scroll_to_row, table, row);
    }

    public static void table_scroll_to_column(MemorySegment table, int column) {
        invoke(table_scroll_to_column, table, column);
    }

    public static void table_set_editable(MemorySegment table, boolean editable) {
        invoke(table_set_editable, table, editable);
    }

    public static boolean table_get_editable(MemorySegment table) {
        return (boolean) invoke(table_get_editable, table);
    }

    public static boolean table_begin_edit(MemorySegment table, int row, int column) {
        return (boolean) invoke(table_begin_edit, table, row, column);
    }

    public static boolean table_commit_edit(MemorySegment table) {
        return (boolean) invoke(table_commit_edit, table);
    }

    public static void table_cancel_edit(MemorySegment table) {
        invoke(table_cancel_edit, table);
    }

    public static boolean table_is_editing(MemorySegment table) {
        return (boolean) invoke(table_is_editing, table);
    }

    public static void table_set_on_cell_edited(MemorySegment table, TableEditedHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : table_edited_upcall(handler, arena);
        invoke(table_set_on_cell_edited, table, stub, MemorySegment.NULL);
    }

    public static int table_get_selection_count(MemorySegment table) {
        return (int) invoke(table_get_selection_count, table);
    }

    public static boolean table_is_row_selected(MemorySegment table, int row) {
        return (boolean) invoke(table_is_row_selected, table, row);
    }

    public static int[] table_get_selection(MemorySegment table) {
        int count = table_get_selection_count(table);
        if (count <= 0) {
            return new int[0];
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rows = arena.allocate(ValueLayout.JAVA_INT, count);
            int written = (int) invoke(table_get_selection, table, rows, count);
            int[] result = new int[written];
            for (int index = 0; index < written; ++index) {
                result[index] = rows.getAtIndex(ValueLayout.JAVA_INT, index);
            }
            return result;
        }
    }

    public static void table_select_range(MemorySegment table, int first, int last) {
        invoke(table_select_range, table, first, last);
    }

    public static void table_select_toggle(MemorySegment table, int row) {
        invoke(table_select_toggle, table, row);
    }

    public static void table_clear_selection(MemorySegment table) {
        invoke(table_clear_selection, table);
    }

    public static void table_set_columns_reorderable(MemorySegment table, boolean reorderable) {
        invoke(table_set_columns_reorderable, table, reorderable);
    }

    public static boolean table_get_columns_reorderable(MemorySegment table) {
        return (boolean) invoke(table_get_columns_reorderable, table);
    }

    public static boolean table_move_column(MemorySegment table, int from, int to) {
        return (boolean) invoke(table_move_column, table, from, to);
    }

    public static void table_set_on_columns_reordered(MemorySegment table,
            TableReorderHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : table_reorder_upcall(handler, arena);
        invoke(table_set_on_columns_reordered, table, stub, MemorySegment.NULL);
    }

    public static void table_set_on_row_drag(MemorySegment table, TableRowDragHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : table_row_drag_upcall(handler, arena);
        invoke(table_set_on_row_drag, table, stub, MemorySegment.NULL);
    }

    public static MemorySegment tree_add_node(MemorySegment tree, MemorySegment parent,
            String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(tree_add_node, tree,
                    parent == null ? MemorySegment.NULL : parent, cstring(arena, text));
        }
    }

    public static void tree_clear(MemorySegment tree) {
        invoke(tree_clear, tree);
    }

    public static int tree_get_root_count(MemorySegment tree) {
        return (int) invoke(tree_get_root_count, tree);
    }

    public static MemorySegment tree_get_root_node(MemorySegment tree, int index) {
        return (MemorySegment) invoke(tree_get_root_node, tree, index);
    }

    public static String tree_node_get_text(MemorySegment node) {
        MemorySegment text = (MemorySegment) invoke(tree_node_get_text, node);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void tree_node_set_text(MemorySegment node, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(tree_node_set_text, node, cstring(arena, text));
        }
    }

    public static boolean tree_node_get_expanded(MemorySegment node) {
        return (boolean) invoke(tree_node_get_expanded, node);
    }

    public static void tree_node_set_expanded(MemorySegment node, boolean expanded) {
        invoke(tree_node_set_expanded, node, expanded);
    }

    public static boolean tree_node_has_children(MemorySegment node) {
        return (boolean) invoke(tree_node_has_children, node);
    }

    public static int tree_node_get_depth(MemorySegment node) {
        return (int) invoke(tree_node_get_depth, node);
    }

    public static MemorySegment tree_node_parent(MemorySegment node) {
        return (MemorySegment) invoke(tree_node_parent, node);
    }

    public static int tree_node_get_child_count(MemorySegment node) {
        return (int) invoke(tree_node_get_child_count, node);
    }

    public static MemorySegment tree_node_get_child(MemorySegment node, int index) {
        return (MemorySegment) invoke(tree_node_get_child, node, index);
    }

    public static MemorySegment tree_node_get_data(MemorySegment node) {
        return (MemorySegment) invoke(tree_node_get_data, node);
    }

    public static void tree_node_set_data(MemorySegment node, MemorySegment data) {
        invoke(tree_node_set_data, node, data == null ? MemorySegment.NULL : data);
    }

    public static int tree_get_visible_count(MemorySegment tree) {
        return (int) invoke(tree_get_visible_count, tree);
    }

    public static MemorySegment tree_get_visible_node(MemorySegment tree, int row) {
        return (MemorySegment) invoke(tree_get_visible_node, tree, row);
    }

    public static MemorySegment tree_get_selected(MemorySegment tree) {
        return (MemorySegment) invoke(tree_get_selected, tree);
    }

    public static void tree_set_selected(MemorySegment tree, MemorySegment node) {
        invoke(tree_set_selected, tree, node == null ? MemorySegment.NULL : node);
    }

    public static void tree_set_row_height(MemorySegment tree, float height) {
        invoke(tree_set_row_height, tree, height);
    }

    public static void tree_set_indent(MemorySegment tree, float indent) {
        invoke(tree_set_indent, tree, indent);
    }

    public static void tree_set_on_select(MemorySegment tree, TreeNodeHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : tree_node_upcall(handler, arena);
        invoke(tree_set_on_select, tree, stub, MemorySegment.NULL);
    }

    public static void tree_set_on_activate(MemorySegment tree, TreeNodeHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : tree_node_upcall(handler, arena);
        invoke(tree_set_on_activate, tree, stub, MemorySegment.NULL);
    }

    public static void tree_refresh(MemorySegment tree) {
        invoke(tree_refresh, tree);
    }

    public static void tree_scroll_to_node(MemorySegment tree, MemorySegment node) {
        invoke(tree_scroll_to_node, tree, node);
    }

    public static void tree_set_renamable(MemorySegment tree, boolean renamable) {
        invoke(tree_set_renamable, tree, renamable);
    }

    public static boolean tree_get_renamable(MemorySegment tree) {
        return (boolean) invoke(tree_get_renamable, tree);
    }

    public static boolean tree_begin_rename(MemorySegment tree, MemorySegment node) {
        return (boolean) invoke(tree_begin_rename, tree, node);
    }

    public static boolean tree_commit_rename(MemorySegment tree) {
        return (boolean) invoke(tree_commit_rename, tree);
    }

    public static void tree_cancel_rename(MemorySegment tree) {
        invoke(tree_cancel_rename, tree);
    }

    public static boolean tree_is_renaming(MemorySegment tree) {
        return (boolean) invoke(tree_is_renaming, tree);
    }

    public static void tree_set_on_rename(MemorySegment tree, TreeRenameHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : tree_rename_upcall(handler, arena);
        invoke(tree_set_on_rename, tree, stub, MemorySegment.NULL);
    }

    public static int tree_get_selection_count(MemorySegment tree) {
        return (int) invoke(tree_get_selection_count, tree);
    }

    public static boolean tree_is_selected(MemorySegment tree, MemorySegment node) {
        return (boolean) invoke(tree_is_selected, tree, node);
    }

    public static MemorySegment[] tree_get_selection(MemorySegment tree) {
        int count = tree_get_selection_count(tree);
        if (count <= 0) {
            return new MemorySegment[0];
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodes = arena.allocate(ValueLayout.ADDRESS, count);
            int written = (int) invoke(tree_get_selection, tree, nodes, count);
            MemorySegment[] result = new MemorySegment[written];
            for (int index = 0; index < written; ++index) {
                result[index] = nodes.getAtIndex(ValueLayout.ADDRESS, index);
            }
            return result;
        }
    }

    public static void tree_select_range(MemorySegment tree, int first_row, int last_row) {
        invoke(tree_select_range, tree, first_row, last_row);
    }

    public static void tree_select_toggle(MemorySegment tree, MemorySegment node) {
        invoke(tree_select_toggle, tree, node);
    }

    public static void tree_clear_selection(MemorySegment tree) {
        invoke(tree_clear_selection, tree);
    }

    /* ---------- combobox ---------- */

    public static MemorySegment combobox(MemorySegment parent) {
        return (MemorySegment) invoke(combobox_create, parent);
    }

    public static void combobox_add_item(MemorySegment combobox, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(combobox_add_item, combobox, cstring(arena, text));
        }
    }

    public static void combobox_insert_item(MemorySegment combobox, int index, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(combobox_insert_item, combobox, index, cstring(arena, text));
        }
    }

    public static void combobox_remove_item(MemorySegment combobox, int index) {
        invoke(combobox_remove_item, combobox, index);
    }

    public static void combobox_clear(MemorySegment combobox) {
        invoke(combobox_clear, combobox);
    }

    public static int combobox_get_item_count(MemorySegment combobox) {
        return (int) invoke(combobox_get_item_count, combobox);
    }

    public static String combobox_get_item_text(MemorySegment combobox, int index) {
        MemorySegment text = (MemorySegment) invoke(combobox_get_item_text, combobox, index);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void combobox_set_item_text(MemorySegment combobox, int index, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(combobox_set_item_text, combobox, index, cstring(arena, text));
        }
    }

    public static void combobox_set_item_data(MemorySegment combobox, int index,
            MemorySegment data) {
        invoke(combobox_set_item_data, combobox, index, data);
    }

    public static MemorySegment combobox_get_item_data(MemorySegment combobox, int index) {
        return (MemorySegment) invoke(combobox_get_item_data, combobox, index);
    }

    public static void combobox_set_item_disabled(MemorySegment combobox, int index,
            boolean disabled) {
        invoke(combobox_set_item_disabled, combobox, index, disabled);
    }

    public static boolean combobox_get_item_disabled(MemorySegment combobox, int index) {
        return (boolean) invoke(combobox_get_item_disabled, combobox, index);
    }

    public static void combobox_set_mode(MemorySegment combobox, int mode) {
        invoke(combobox_set_mode, combobox, mode);
    }

    public static int combobox_get_mode(MemorySegment combobox) {
        return (int) invoke(combobox_get_mode, combobox);
    }

    public static int combobox_get_selected(MemorySegment combobox) {
        return (int) invoke(combobox_get_selected, combobox);
    }

    public static void combobox_set_selected(MemorySegment combobox, int index) {
        invoke(combobox_set_selected, combobox, index);
    }

    public static String combobox_get_text(MemorySegment combobox) {
        MemorySegment text = (MemorySegment) invoke(combobox_get_text, combobox);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void combobox_set_text(MemorySegment combobox, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(combobox_set_text, combobox, cstring(arena, text));
        }
    }

    public static String combobox_get_placeholder(MemorySegment combobox) {
        MemorySegment text = (MemorySegment) invoke(combobox_get_placeholder, combobox);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void combobox_set_placeholder(MemorySegment combobox, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(combobox_set_placeholder, combobox, cstring(arena, text));
        }
    }

    public static void combobox_open(MemorySegment combobox) {
        invoke(combobox_open, combobox);
    }

    public static void combobox_close(MemorySegment combobox) {
        invoke(combobox_close, combobox);
    }

    public static boolean combobox_is_open(MemorySegment combobox) {
        return (boolean) invoke(combobox_is_open, combobox);
    }

    public static void combobox_set_popup_max_height(MemorySegment combobox, float height) {
        invoke(combobox_set_popup_max_height, combobox, height);
    }

    public static float combobox_get_popup_max_height(MemorySegment combobox) {
        return (float) invoke(combobox_get_popup_max_height, combobox);
    }

    public static void combobox_set_on_change(MemorySegment combobox, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : index_upcall(handler, arena);
        invoke(combobox_set_on_change, combobox, stub, MemorySegment.NULL);
    }

    public static void combobox_set_on_text_change(MemorySegment combobox, TextHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL
                : text_upcall(handler, arena);
        invoke(combobox_set_on_text_change, combobox, stub, MemorySegment.NULL);
    }

    public static MemorySegment tabs_add_page(MemorySegment tabs, String title) {
        Objects.requireNonNull(title, "title");
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(tabs_add_page, tabs, cstring(arena, title));
        }
    }

    public static void tabs_remove_page(MemorySegment tabs, int index) {
        invoke(tabs_remove_page, tabs, index);
    }

    public static int tabs_get_page_count(MemorySegment tabs) {
        return (int) invoke(tabs_get_page_count, tabs);
    }

    public static MemorySegment tabs_get_page(MemorySegment tabs, int index) {
        return (MemorySegment) invoke(tabs_get_page, tabs, index);
    }

    public static void tabs_set_page_title(MemorySegment tabs, int index, String title) {
        Objects.requireNonNull(title, "title");
        try (Arena arena = Arena.ofConfined()) {
            invoke(tabs_set_page_title, tabs, index, cstring(arena, title));
        }
    }

    public static String tabs_get_page_title(MemorySegment tabs, int index) {
        MemorySegment text = (MemorySegment) invoke(tabs_get_page_title, tabs, index);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static int tabs_get_active(MemorySegment tabs) {
        return (int) invoke(tabs_get_active, tabs);
    }

    public static void tabs_set_active(MemorySegment tabs, int index) {
        invoke(tabs_set_active, tabs, index);
    }

    public static void tabs_set_on_change(MemorySegment tabs, IndexHandler handler, Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : index_upcall(handler, arena);
        invoke(tabs_set_on_change, tabs, stub, MemorySegment.NULL);
    }

    public static void tabs_set_strip_height(MemorySegment tabs, float height) {
        invoke(tabs_set_strip_height, tabs, height);
    }

    public static MemorySegment sidebar_add_item(MemorySegment sidebar, int glyph, String label) {
        Objects.requireNonNull(label, "label");
        try (Arena arena = Arena.ofConfined()) {
            return (MemorySegment) invoke(sidebar_add_item, sidebar, glyph,
                    cstring(arena, label));
        }
    }

    public static int sidebar_get_item_count(MemorySegment sidebar) {
        return (int) invoke(sidebar_get_item_count, sidebar);
    }

    public static MemorySegment sidebar_get_item(MemorySegment sidebar, int index) {
        return (MemorySegment) invoke(sidebar_get_item, sidebar, index);
    }

    public static int sidebar_get_active(MemorySegment sidebar) {
        return (int) invoke(sidebar_get_active, sidebar);
    }

    public static void sidebar_set_active(MemorySegment sidebar, int index) {
        invoke(sidebar_set_active, sidebar, index);
    }

    public static boolean sidebar_get_compact(MemorySegment sidebar) {
        return (boolean) invoke(sidebar_get_compact, sidebar);
    }

    public static void sidebar_set_compact(MemorySegment sidebar, boolean compact) {
        invoke(sidebar_set_compact, sidebar, compact);
    }

    public static void sidebar_set_header(MemorySegment sidebar, String text) {
        Objects.requireNonNull(text, "text");
        try (Arena arena = Arena.ofConfined()) {
            invoke(sidebar_set_header, sidebar, cstring(arena, text));
        }
    }

    public static String sidebar_get_header(MemorySegment sidebar) {
        MemorySegment text = (MemorySegment) invoke(sidebar_get_header, sidebar);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void sidebar_set_on_select(MemorySegment sidebar, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = handler == null ? MemorySegment.NULL : index_upcall(handler, arena);
        invoke(sidebar_set_on_select, sidebar, stub, MemorySegment.NULL);
    }

    public static float ease(int easing, float t) {
        return (float) invoke(pt_ease, easing, t);
    }

    public static float ease_value(int easing, float start, float end, float t) {
        return (float) invoke(pt_ease_value, easing, start, end, t);
    }

    public static void entry_set_drag_enabled(MemorySegment entry, boolean enabled) {
        invoke(entry_set_drag_enabled, entry, enabled);
    }

    public static boolean entry_get_drag_enabled(MemorySegment entry) {
        return (boolean) invoke(entry_get_drag_enabled, entry);
    }

    public static boolean widget_focus(MemorySegment widget) {
        return (boolean) invoke(widget_focus, widget);
    }

    public static boolean widget_get_drop_hover(MemorySegment widget) {
        return (boolean) invoke(widget_get_drop_hover, widget);
    }

    public static void widget_set_drop_target(MemorySegment widget, DropEnterHandler enter,
            DropMotionHandler motion, DropLeaveHandler leave, DropHandler drop, Arena arena) {
        widget_set_drop_target(widget, enter, motion, leave, drop, null, null, arena);
    }

    /** Structured variant: enter_offer and drop_offer take precedence over enter and drop. */
    public static void widget_set_drop_target(MemorySegment widget, DropEnterHandler enter,
            DropMotionHandler motion, DropLeaveHandler leave, DropHandler drop,
            DropOfferEnterHandler enter_offer, DropOfferHandler drop_offer, Arena arena) {
        if (enter == null && enter_offer == null) {
            invoke(widget_set_drop_target, widget, MemorySegment.NULL);
            return;
        }
        long pointer_size = ValueLayout.ADDRESS.byteSize();
        MemorySegment target = arena.allocate(drop_target_layout);
        target.set(ValueLayout.ADDRESS, 0,
                enter == null ? MemorySegment.NULL : drop_enter_upcall(enter, arena));
        target.set(ValueLayout.ADDRESS, pointer_size,
                motion == null ? MemorySegment.NULL : drop_motion_upcall(motion, arena));
        target.set(ValueLayout.ADDRESS, pointer_size * 2,
                leave == null ? MemorySegment.NULL : drop_leave_upcall(leave, arena));
        target.set(ValueLayout.ADDRESS, pointer_size * 3,
                drop == null ? MemorySegment.NULL : drop_data_upcall(drop, arena));
        target.set(ValueLayout.ADDRESS, pointer_size * 4, MemorySegment.NULL);
        target.set(ValueLayout.ADDRESS, pointer_size * 5, enter_offer == null ? MemorySegment.NULL
                : drop_offer_enter_upcall(enter_offer, arena));
        target.set(ValueLayout.ADDRESS, pointer_size * 6, drop_offer == null ? MemorySegment.NULL
                : drop_offer_upcall(drop_offer, arena));
        invoke(widget_set_drop_target, widget, target);
    }

    public static void widget_clear_drop_target(MemorySegment widget) {
        invoke(widget_set_drop_target, widget, MemorySegment.NULL);
    }

    public static int drop_offer_mime_count(MemorySegment offer) {
        return (int) invoke(drop_offer_mime_count, offer);
    }

    public static String drop_offer_mime(MemorySegment offer, int index) {
        return optionalText((MemorySegment) invoke(drop_offer_mime, offer, index));
    }

    public static boolean drop_offer_has_mime(MemorySegment offer, String mime_type) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment mime =
                    mime_type == null ? MemorySegment.NULL : cstring(arena, mime_type);
            return (boolean) invoke(drop_offer_has_mime, offer, mime);
        }
    }

    public static String drop_offer_selected_mime(MemorySegment offer) {
        return optionalText((MemorySegment) invoke(drop_offer_selected_mime, offer));
    }

    /** Bit mask of the pt_drag_action values advertised by the source. */
    public static int drop_offer_actions(MemorySegment offer) {
        return (int) invoke(drop_offer_actions, offer);
    }

    /** Starts an asynchronous read; returns a NULL handle when no stream could open. */
    public static MemorySegment drop_offer_read(MemorySegment offer, String mime_type,
            long max_bytes, DropStreamHandler handler, Arena arena) {
        Objects.requireNonNull(mime_type, "mime_type");
        if (handler == null) {
            return MemorySegment.NULL;
        }
        try (Arena scratch = Arena.ofConfined()) {
            return (MemorySegment) invoke(drop_offer_read, offer, cstring(scratch, mime_type),
                    Math.max(0L, max_bytes), drop_stream_upcall(handler, arena),
                    MemorySegment.NULL);
        }
    }

    public static void drop_stream_cancel(MemorySegment stream) {
        invoke(drop_stream_cancel, stream);
    }

    public static String drop_stream_mime(MemorySegment stream) {
        return optionalText((MemorySegment) invoke(drop_stream_mime, stream));
    }

    public static long drop_stream_size(MemorySegment stream) {
        return (long) invoke(drop_stream_size, stream);
    }

    public static boolean drop_stream_active(MemorySegment stream) {
        return (boolean) invoke(drop_stream_active, stream);
    }

    public static boolean toggle_get_checked(MemorySegment widget) {
        return (boolean) invoke(toggle_get_checked, widget);
    }

    public static void toggle_set_checked(MemorySegment widget, boolean checked) {
        invoke(toggle_set_checked, widget, checked);
    }

    public static void toggle_set_on_change(MemorySegment widget, ValueHandler handler,
            Arena arena) {
        MemorySegment stub = value_upcall(handler, arena);
        invoke(toggle_set_on_change, widget, stub, MemorySegment.NULL);
    }

    public static float slider_get_value(MemorySegment widget) {
        return (float) invoke(slider_get_value, widget);
    }

    public static void slider_set_value(MemorySegment widget, float value) {
        invoke(slider_set_value, widget, value);
    }

    public static void slider_set_on_change(MemorySegment widget, ValueHandler handler,
            Arena arena) {
        MemorySegment stub = value_upcall(handler, arena);
        invoke(slider_set_on_change, widget, stub, MemorySegment.NULL);
    }

    public static boolean widget_get_checked(MemorySegment widget) {
        return (boolean) invoke(widget_get_checked, widget);
    }

    public static void widget_set_checked(MemorySegment widget, boolean checked) {
        invoke(widget_set_checked, widget, checked);
    }

    public static void widget_set_on_checked(MemorySegment widget, ValueHandler handler,
            Arena arena) {
        MemorySegment stub = value_upcall(handler, arena);
        invoke(widget_set_on_checked, widget, stub, MemorySegment.NULL);
    }

    public static float progress_get_value(MemorySegment widget) {
        return (float) invoke(progress_get_value, widget);
    }

    public static void progress_set_value(MemorySegment widget, float value) {
        invoke(progress_set_value, widget, value);
    }

    public static boolean progress_get_indeterminate(MemorySegment widget) {
        return (boolean) invoke(progress_get_indeterminate, widget);
    }

    public static void progress_set_indeterminate(MemorySegment widget, boolean indeterminate) {
        invoke(progress_set_indeterminate, widget, indeterminate);
    }

    public static int segmented_get_selected(MemorySegment widget) {
        return (int) invoke(segmented_get_selected, widget);
    }

    public static void segmented_set_selected(MemorySegment widget, int index) {
        invoke(segmented_set_selected, widget, index);
    }

    public static void segmented_set_on_change(MemorySegment widget, IndexHandler handler,
            Arena arena) {
        MemorySegment stub = index_upcall(handler, arena);
        invoke(segmented_set_on_change, widget, stub, MemorySegment.NULL);
    }

    public static void scroll_set_offset(MemorySegment widget, float offset) {
        invoke(scroll_set_offset, widget, offset);
    }

    public static float scroll_get_offset(MemorySegment widget) {
        return (float) invoke(scroll_get_offset, widget);
    }

    public static float scroll_get_max_offset(MemorySegment widget) {
        return (float) invoke(scroll_get_max_offset, widget);
    }

    public static void widget_set_tooltip(MemorySegment widget, String text) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(widget_set_tooltip, widget, cstring(arena, text));
        }
    }

    public static void widget_set_tooltip_delay(MemorySegment widget, int milliseconds) {
        invoke(widget_set_tooltip_delay, widget, milliseconds);
    }

    public static int widget_get_a11y_role(MemorySegment widget) {
        return (int) invoke(widget_get_a11y_role, widget);
    }

    public static void widget_set_a11y_role(MemorySegment widget, int role) {
        invoke(widget_set_a11y_role, widget, role);
    }

    public static String widget_get_a11y_name(MemorySegment widget) {
        MemorySegment text = (MemorySegment) invoke(widget_get_a11y_name, widget);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void widget_set_a11y_name(MemorySegment widget, String name) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(widget_set_a11y_name, widget, cstring(arena, name));
        }
    }

    public static String widget_get_a11y_description(MemorySegment widget) {
        MemorySegment text = (MemorySegment) invoke(widget_get_a11y_description, widget);
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    public static void widget_set_a11y_description(MemorySegment widget, String description) {
        try (Arena arena = Arena.ofConfined()) {
            invoke(widget_set_a11y_description, widget, cstring(arena, description));
        }
    }

    public static int widget_get_a11y_state(MemorySegment widget) {
        return (int) invoke(widget_get_a11y_state, widget);
    }

    public static A11yText widget_get_a11y_text(MemorySegment widget) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment text = arena.allocate(ptr);
            MemorySegment caret = arena.allocate(i32);
            MemorySegment start = arena.allocate(i32);
            MemorySegment end = arena.allocate(i32);
            boolean found = (boolean) invoke(widget_get_a11y_text, widget, text, caret, start, end);
            if (!found) {
                return null;
            }
            MemorySegment value = text.get(ptr, 0);
            String string = value.address() == 0
                    ? ""
                    : value.reinterpret(Long.MAX_VALUE).getString(0);
            return new A11yText(string, caret.get(i32, 0), start.get(i32, 0), end.get(i32, 0));
        }
    }

    public static Float widget_get_a11y_value(MemorySegment widget) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(f32);
            boolean found = (boolean) invoke(widget_get_a11y_value, widget, value);
            return found ? value.get(f32, 0) : null;
        }
    }

    public static final class Transition implements AutoCloseable {
        private static final long bytes = 28;
        private final Arena arena;
        private final MemorySegment state;

        public Transition(float value) {
            arena = Arena.ofConfined();
            state = arena.allocate(bytes, 4);
            invoke(transition_init, state, value);
        }

        public float value() {
            return state.get(f32, 0);
        }

        public boolean active() {
            return (boolean) invoke(transition_active, state);
        }

        public Transition to(float target, int duration_ms, int easing) {
            invoke(transition_to, state, target, duration_ms, easing);
            return this;
        }

        public boolean advance(float dt) {
            return (boolean) invoke(transition_advance, state, dt);
        }

        public Transition set(float value) {
            invoke(transition_set, state, value);
            return this;
        }

        public MemorySegment state() {
            return state;
        }

        @Override
        public void close() {
            arena.close();
        }
    }
}

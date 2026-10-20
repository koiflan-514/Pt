/*
 * Navigation shell checks: vector icons, tabs and sidebar.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct nav_state {
    int changes;
    uint32_t index;
} nav_state;

static void on_index(pt_widget *widget, uint32_t index, void *data) {
    (void)widget;
    nav_state *state = data;
    ++state->changes;
    state->index = index;
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 0);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 0);
}

static void test_icons(pt_app *app) {
    pt_window *window = pt_window_create(app, "icons", 240, 200);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *icon = pt_icon_create(root, pt_icon_search);
    pt_check(icon != NULL);
    pt_widget_set_align_self(icon, pt_align_start);
    pt_check(pt_widget_get_kind(icon) == pt_widget_icon);
    pt_check_near(pt_icon_get_size(icon), 20.0, 0.01);
    pt_icon_set_size(icon, 24.0f);
    pt_check_near(pt_icon_get_size(icon), 24.0, 0.01);
    pt_icon_set_size(icon, 2.0f);
    pt_check_near(pt_icon_get_size(icon), 8.0, 0.01);
    pt_icon_set_size(icon, 24.0f);
    pt_widget_set_icon(icon, pt_icon_menu);
    pt_check(pt_widget_get_icon(icon) == pt_icon_menu);
    pt_check(pt_widget_get_a11y_role(icon) == pt_a11y_role_icon);

    pt_widget *button =
        pt_icon_button_create(root, pt_icon_folder, "Open folder");
    pt_check(button != NULL);
    pt_widget_set_align_self(button, pt_align_start);
    pt_check(pt_widget_get_kind(button) == pt_widget_button);
    pt_check(pt_widget_get_icon(button) == pt_icon_folder);
    pt_check(strcmp(pt_widget_get_a11y_name(button), "Open folder") == 0);
    pt_check(pt_widget_get_a11y_role(button) == pt_a11y_role_push_button);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(240, 200, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect icon_rect = bounds_of(icon);
    pt_check_near(icon_rect.w, 24.0, 0.01);
    pt_check_near(icon_rect.h, 24.0, 0.01);
    pt_rect button_rect = bounds_of(button);
    pt_check_near(button_rect.w, 36.0, 0.01);
    pt_check_near(button_rect.h, 36.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_tabs(pt_app *app) {
    pt_window *window = pt_window_create(app, "tabs", 520, 360);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *tabs = pt_tabs_create(root);
    pt_check(tabs != NULL);
    pt_widget_set_align_self(tabs, pt_align_start);
    pt_widget_set_width(tabs, 380.0f);
    pt_widget_set_height(tabs, 240.0f);
    pt_check(pt_tabs_get_page_count(tabs) == 0);
    pt_check(pt_tabs_get_active(tabs) == 0);

    pt_widget *overview = pt_tabs_add_page(tabs, "Overview");
    pt_widget *activity = pt_tabs_add_page(tabs, "Activity");
    pt_widget *settings = pt_tabs_add_page(tabs, "Settings");
    pt_check(overview && activity && settings);
    pt_check(pt_tabs_get_page_count(tabs) == 3);
    pt_check(pt_tabs_get_page(tabs, 0) == overview);
    pt_check(pt_tabs_get_page(tabs, 1) == activity);
    pt_check(pt_tabs_get_page(tabs, 2) == settings);
    pt_check(pt_tabs_get_page(tabs, 3) == NULL);
    pt_check(strcmp(pt_tabs_get_page_title(tabs, 1), "Activity") == 0);
    pt_check(pt_tabs_get_page_title(tabs, 9)[0] == '\0');
    pt_tabs_set_page_title(tabs, 2, "Preferences");
    pt_check(strcmp(pt_tabs_get_page_title(tabs, 2), "Preferences") == 0);
    pt_widget *body = pt_label_create(overview, "Overview body");
    pt_check(body != NULL);
    pt_check(pt_tabs_get_active(tabs) == 0);

    nav_state changes = {0};
    pt_tabs_set_on_change(tabs, on_index, &changes);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 360, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect tabs_rect = bounds_of(tabs);
    pt_check_near(tabs_rect.x, 0.0, 0.01);
    pt_check_near(tabs_rect.y, 44.0, 0.01);
    pt_check_near(tabs_rect.w, 380.0, 0.01);
    pt_check_near(tabs_rect.h, 240.0, 0.01);
    pt_check(overview->visible);
    pt_check(!activity->visible);
    pt_check(!settings->visible);
    pt_rect page_rect = bounds_of(overview);
    pt_check_near(page_rect.y, 88.0, 0.01);
    pt_check_near(page_rect.h, 196.0, 0.01);
    pt_check(pt_widget_get_a11y_role(tabs) == pt_a11y_role_tab_list);
    pt_check(pt_widget_get_a11y_role(overview) == pt_a11y_role_tab_panel);
    uint32_t state = pt_widget_get_a11y_state(overview);
    pt_check((state & pt_a11y_state_selected) != 0);
    state = pt_widget_get_a11y_state(activity);
    pt_check((state & pt_a11y_state_selected) == 0);
    pt_tabs_state *tab_state = tabs->extra;
    pt_check(!tab_state->overflow);
    pt_check(tab_state->indicator_ready);
    pt_check_near(tab_state->indicator_x, tab_state->tab_x[0], 0.01);
    pt_check_near(tab_state->indicator_width, tab_state->tab_width[0], 0.01);

    /* Clicking the second tab changes pages and notifies once. */
    click_at(window, tab_state->tab_x[1] + tab_state->tab_width[1] * 0.5f,
             tabs_rect.y + 22.0f);
    pt_check(pt_tabs_get_active(tabs) == 1);
    pt_check(changes.changes == 1);
    pt_check(changes.index == 1);
    pt_check(!overview->visible);
    pt_check(activity->visible);
    pt_check(settings->visible == false);
    pt_check(window->focused == tabs);

    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(pt_tabs_get_active(tabs) == 2);
    pt_check(changes.changes == 2);
    pt_check(changes.index == 2);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(pt_tabs_get_active(tabs) == 1);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(pt_tabs_get_active(tabs) == 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(pt_tabs_get_active(tabs) == 2);
    pt_check(changes.changes == 5);
    pt_tabs_set_active(tabs, 1);
    pt_check(pt_tabs_get_active(tabs) == 1);
    pt_check(changes.changes == 5);
    pt_tabs_set_active(tabs, 2);
    pt_check(changes.changes == 5);

    /* Indicator glides toward the target unless reduced motion is on. */
    pt_check(pt_window_paint(window, canvas));
    float start_indicator = tab_state->indicator_x;
    float target = tab_state->target_x;
    pt_check(fabsf(start_indicator - target) > 1.0f);
    pt_check(pt_tabs_update_animation(tabs, 0.016f));
    pt_check(fabsf(tab_state->indicator_x - start_indicator) > 0.01f);
    for (int i = 0; i < 240; ++i) {
        pt_tabs_update_animation(tabs, 0.016f);
    }
    pt_check_near(tab_state->indicator_x, target, 0.01);
    pt_check_near(tab_state->indicator_width, tab_state->target_width, 0.01);
    app->reduced_motion = true;
    pt_tabs_set_active(tabs, 0);
    pt_check(pt_window_paint(window, canvas));
    pt_check(fabsf(tab_state->indicator_x - tab_state->target_x) > 1.0f);
    pt_check(pt_tabs_update_animation(tabs, 0.016f));
    pt_check_near(tab_state->indicator_x, tab_state->target_x, 0.01);
    app->reduced_motion = false;

    /* Overflowing strips scroll via the arrow zones. */
    pt_widget_set_width(tabs, 260.0f);
    for (int i = 0; i < 5; ++i) {
        pt_tabs_add_page(tabs, "Additional page");
    }
    pt_check(pt_tabs_get_page_count(tabs) == 8);
    pt_check(pt_window_paint(window, canvas));
    pt_check(tab_state->overflow);
    float before = tab_state->scroll_x;
    click_at(window, tabs_rect.x + 260.0f - 14.0f, tabs_rect.y + 22.0f);
    pt_check(tab_state->scroll_x > before);
    float scrolled = tab_state->scroll_x;
    click_at(window, tabs_rect.x + 14.0f, tabs_rect.y + 22.0f);
    pt_check(tab_state->scroll_x < scrolled);
    pt_check(changes.changes == 5);

    /* Removing pages shifts titles and clamps the active index. */
    pt_tabs_set_active(tabs, 2);
    pt_widget *removed = pt_tabs_get_page(tabs, 0);
    pt_tabs_remove_page(tabs, 0);
    pt_check(removed != NULL);
    pt_check(pt_tabs_get_page_count(tabs) == 7);
    pt_check(pt_tabs_get_page(tabs, 0) == activity);
    pt_check(strcmp(pt_tabs_get_page_title(tabs, 0), "Activity") == 0);
    pt_check(pt_tabs_get_active(tabs) == 1);
    pt_check(pt_tabs_get_page(tabs, 7) == NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(!activity->visible);
    pt_check(settings->visible);
    for (uint32_t i = pt_tabs_get_page_count(tabs); i > 0; --i) {
        pt_tabs_remove_page(tabs, i - 1);
    }
    pt_check(pt_tabs_get_page_count(tabs) == 0);
    pt_check(pt_tabs_get_active(tabs) == 0);
    pt_check(pt_window_paint(window, canvas));

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_sidebar(pt_app *app) {
    pt_window *window = pt_window_create(app, "sidebar", 360, 400);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *sidebar = pt_sidebar_create(root);
    pt_check(sidebar != NULL);
    pt_widget_set_align_self(sidebar, pt_align_start);
    pt_check(pt_sidebar_get_item_count(sidebar) == 0);
    pt_check(pt_sidebar_get_active(sidebar) == pt_list_index_none);
    pt_check(!pt_sidebar_get_compact(sidebar));
    pt_check(pt_sidebar_get_header(sidebar)[0] == '\0');
    pt_sidebar_set_header(sidebar, "Workspace");
    pt_check(strcmp(pt_sidebar_get_header(sidebar), "Workspace") == 0);
    pt_widget *home = pt_sidebar_add_item(sidebar, pt_icon_home, "Home");
    pt_widget *docs = pt_sidebar_add_item(sidebar, pt_icon_document, "Documents");
    pt_widget *prefs = pt_sidebar_add_item(sidebar, pt_icon_settings, "Settings");
    pt_widget *alerts = pt_sidebar_add_item(sidebar, pt_icon_bell, "Notifications");
    pt_check(home && docs && prefs && alerts);
    pt_check(pt_sidebar_get_item_count(sidebar) == 4);
    pt_check(pt_sidebar_get_item(sidebar, 0) == home);
    pt_check(pt_sidebar_get_item(sidebar, 3) == alerts);
    pt_check(pt_sidebar_get_item(sidebar, 4) == NULL);
    pt_check(strcmp(pt_widget_get_text(docs), "Documents") == 0);
    pt_check(pt_widget_get_icon(prefs) == pt_icon_settings);
    pt_check(pt_widget_get_a11y_role(sidebar) == pt_a11y_role_list);
    pt_check(pt_widget_get_a11y_role(home) == pt_a11y_role_list_item);
    pt_check(strcmp(pt_widget_get_a11y_name(alerts), "Notifications") == 0);

    nav_state selection = {0};
    pt_sidebar_set_on_select(sidebar, on_index, &selection);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 400, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect sidebar_rect = bounds_of(sidebar);
    pt_check_near(sidebar_rect.w, 228.0, 0.01);
    pt_check_near(sidebar_rect.h, 240.0, 0.01);
    pt_rect docs_rect = bounds_of(docs);
    pt_check_near(docs_rect.x, 10.0, 0.01);
    pt_check_near(docs_rect.y, 44.0 + 56.0 + 48.0, 0.01);
    pt_check_near(docs_rect.h, 44.0, 0.01);

    click_at(window, docs_rect.x + 40.0f, docs_rect.y + docs_rect.h * 0.5f);
    pt_check(pt_sidebar_get_active(sidebar) == 1);
    pt_check(selection.changes == 1);
    pt_check(selection.index == 1);
    pt_check(docs->row_selected);
    pt_check(!home->row_selected);
    pt_check(window->focused == sidebar);
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(pt_sidebar_get_active(sidebar) == 2);
    pt_check(selection.changes == 2);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(pt_sidebar_get_active(sidebar) == 3);
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(pt_sidebar_get_active(sidebar) == 3);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(pt_sidebar_get_active(sidebar) == 0);
    pt_window_input_key(window, XKB_KEY_Return, 0, NULL, true);
    pt_check(selection.changes == 5);
    pt_check(selection.index == 0);
    pt_sidebar_set_active(sidebar, 2);
    pt_check(pt_sidebar_get_active(sidebar) == 2);
    pt_check(selection.changes == 5);
    pt_sidebar_set_active(sidebar, 99);
    pt_check(pt_sidebar_get_active(sidebar) == pt_list_index_none);
    pt_check(selection.changes == 5);

    /* Compact mode collapses to an icon rail but keeps accessible names. */
    pt_sidebar_set_active(sidebar, 1);
    pt_sidebar_set_compact(sidebar, true);
    pt_check(pt_sidebar_get_compact(sidebar));
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(bounds_of(sidebar).w, 72.0, 0.01);
    pt_rect compact_docs = bounds_of(docs);
    pt_check_near(compact_docs.w, 52.0, 0.01);
    pt_check(strcmp(pt_widget_get_a11y_name(docs), "Documents") == 0);
    pt_sidebar_set_compact(sidebar, false);
    pt_check(!pt_sidebar_get_compact(sidebar));
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(bounds_of(sidebar).w, 228.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.navigation");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("navigation");
    }
    test_icons(app);
    test_tabs(app);
    test_sidebar(app);
    pt_app_destroy(app);
    return pt_test_finish("navigation");
}

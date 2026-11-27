/*
 * AT-SPI2 bridge checks: lifecycle, registry embedding, exported tree,
 * properties, actions, editable text, values, extents, introspection and
 * change events over a real accessibility bus when one is available.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef PT_HAVE_DBUS
#include <dbus/dbus.h>
#include <ftw.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define atspi_role_alert 2
#define atspi_role_dialog 16
#define atspi_role_frame 23
#define atspi_role_image 27
#define atspi_role_label 29
#define atspi_role_list 31
#define atspi_role_list_item 32
#define atspi_role_password_text 40
#define atspi_role_push_button 43
#define atspi_role_slider 51
#define atspi_role_table 55
#define atspi_role_table_cell 56
#define atspi_role_table_column_header 57
#define atspi_role_entry 79
#define atspi_role_application 75
#define atspi_role_page_tab_list 38
#define atspi_role_table_row 90
#define atspi_role_tree 65
#define atspi_role_tree_item 91
#define atspi_state_collapsed 5
#define atspi_state_enabled 8
#define atspi_state_expandable 9
#define atspi_state_expanded 10
#define atspi_state_focusable 11
#define atspi_state_multiselectable 18
#define atspi_state_selectable 22
#define atspi_state_selected 23
#define atspi_state_showing 25
#define atspi_state_selectable_text 38
#define atspi_state_read_only 43

typedef struct a11y_ref {
    char bus[128];
    char path[256];
} a11y_ref;

typedef struct signal_watch {
    const char *path;
    int text_changed;
    int selection_changed;
    int announcement;
    int announcement_politeness;
    char announcement_text[128];
    int showing_changed;
    char showing_path[256];
    bool showing_value;
    int selected_changed;
    char selected_path[256];
    int focus;
    char focus_path[256];
    int children_changed;
    char children_path[256];
} signal_watch;

static int button_clicks;

static DBusHandlerResult watch_signals(DBusConnection *connection, DBusMessage *message,
                                      void *data) {
    (void)connection;
    signal_watch *watch = data;
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char *iface = dbus_message_get_interface(message);
    const char *member = dbus_message_get_member(message);
    const char *path = dbus_message_get_path(message);
    if (!iface || !member || !path) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (strcmp(iface, "org.a11y.atspi.Event.Focus") == 0 &&
        strcmp(member, "Focus") == 0) {
        ++watch->focus;
        snprintf(watch->focus_path, sizeof(watch->focus_path), "%s", path);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (strcmp(iface, "org.a11y.atspi.Event.Object") != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (strcmp(member, "ChildrenChanged") == 0) {
        ++watch->children_changed;
        snprintf(watch->children_path, sizeof(watch->children_path), "%s", path);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (strcmp(member, "TextChanged") == 0) {
        if (watch->path && strcmp(path, watch->path) == 0) {
            ++watch->text_changed;
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (strcmp(member, "SelectionChanged") == 0) {
        ++watch->selection_changed;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (strcmp(member, "Announcement") == 0) {
        DBusMessageIter iter;
        if (!dbus_message_iter_init(message, &iter)) {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        dbus_message_iter_next(&iter);
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus_int32_t politeness = 0;
            dbus_message_iter_get_basic(&iter, &politeness);
            watch->announcement_politeness = (int)politeness;
        }
        dbus_message_iter_next(&iter);
        dbus_message_iter_next(&iter);
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
            DBusMessageIter variant;
            dbus_message_iter_recurse(&iter, &variant);
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                const char *text = NULL;
                dbus_message_iter_get_basic(&variant, &text);
                snprintf(watch->announcement_text, sizeof(watch->announcement_text), "%s",
                         text ? text : "");
            }
        }
        ++watch->announcement;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (strcmp(member, "StateChanged") == 0) {
        DBusMessageIter iter;
        if (!dbus_message_iter_init(message, &iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        const char *detail = NULL;
        dbus_message_iter_get_basic(&iter, &detail);
        dbus_message_iter_next(&iter);
        dbus_int32_t value = 0;
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus_message_iter_get_basic(&iter, &value);
        }
        if (detail && strcmp(detail, "showing") == 0) {
            ++watch->showing_changed;
            watch->showing_value = value != 0;
            snprintf(watch->showing_path, sizeof(watch->showing_path), "%s", path);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (detail && strcmp(detail, "selected") == 0 && value != 0) {
            ++watch->selected_changed;
            snprintf(watch->selected_path, sizeof(watch->selected_path), "%s", path);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void on_button_click(pt_widget *button, void *data) {
    (void)button;
    (void)data;
    ++button_clicks;
}

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    (void)table;
    (void)data;
    char text[32];
    snprintf(text, sizeof(text), "r%uc%u", row, column);
    pt_widget_set_text(cell, text);
}

static DBusConnection *open_a11y_client(void) {
    DBusError error;
    dbus_error_init(&error);
    const char *address = getenv("AT_SPI_BUS_ADDRESS");
    DBusConnection *bus = NULL;
    if (address && *address) {
        bus = dbus_connection_open_private(address, &error);
        if (bus && !dbus_bus_register(bus, &error)) {
            dbus_connection_close(bus);
            dbus_connection_unref(bus);
            bus = NULL;
        }
    } else {
        DBusConnection *session = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        if (!session) {
            dbus_error_free(&error);
            return NULL;
        }
        dbus_connection_set_exit_on_disconnect(session, FALSE);
        DBusMessage *request = dbus_message_new_method_call(
            "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
        DBusMessage *reply =
            request ? dbus_connection_send_with_reply_and_block(session, request, 3000,
                                                               &error)
                    : NULL;
        if (request) {
            dbus_message_unref(request);
        }
        const char *bus_address = NULL;
        if (reply) {
            dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &bus_address,
                                  DBUS_TYPE_INVALID);
        }
        char copy[512];
        snprintf(copy, sizeof(copy), "%s", bus_address ? bus_address : "");
        if (reply) {
            dbus_message_unref(reply);
        }
        dbus_connection_close(session);
        dbus_connection_unref(session);
        if (copy[0] != '\0') {
            bus = dbus_connection_open_private(copy, &error);
            if (bus && !dbus_bus_register(bus, &error)) {
                dbus_connection_close(bus);
                dbus_connection_unref(bus);
                bus = NULL;
            }
        }
    }
    dbus_error_free(&error);
    if (bus) {
        dbus_connection_set_exit_on_disconnect(bus, FALSE);
    }
    return bus;
}

/* Sends a call and pumps both the application's event loop (which serves
 * incoming accessibility requests) and the client connection until the
 * asynchronous reply arrives. */
static DBusMessage *a11y_call(pt_app *app, DBusConnection *client, DBusMessage *message) {
    if (!message) {
        return NULL;
    }
    DBusPendingCall *pending = NULL;
    if (!dbus_connection_send_with_reply(client, message, &pending, 5000)) {
        dbus_message_unref(message);
        return NULL;
    }
    dbus_message_unref(message);
    dbus_connection_flush(client);
    for (int i = 0; i < 500 && !dbus_pending_call_get_completed(pending); ++i) {
        pt_platform_dispatch(app, 5);
        dbus_connection_read_write_dispatch(client, 0);
    }
    DBusMessage *reply = NULL;
    if (dbus_pending_call_get_completed(pending)) {
        reply = dbus_pending_call_steal_reply(pending);
    }
    dbus_pending_call_unref(pending);
    return reply;
}

static DBusMessage *a11y_method(pt_app *app, DBusConnection *client, const char *bus,
                                const char *path, const char *iface, const char *method) {
    return a11y_call(app, client,
                     dbus_message_new_method_call(bus, path, iface, method));
}

static DBusMessage *a11y_method_int1(pt_app *app, DBusConnection *client, const char *bus,
                                     const char *path, const char *iface,
                                     const char *method, int32_t value) {
    DBusMessage *message = dbus_message_new_method_call(bus, path, iface, method);
    if (!message) {
        return NULL;
    }
    dbus_int32_t wire = (dbus_int32_t)value;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &wire, DBUS_TYPE_INVALID);
    return a11y_call(app, client, message);
}

static DBusMessage *a11y_method_int2(pt_app *app, DBusConnection *client, const char *bus,
                                     const char *path, const char *iface,
                                     const char *method, int32_t first, int32_t second) {
    DBusMessage *message = dbus_message_new_method_call(bus, path, iface, method);
    if (!message) {
        return NULL;
    }
    dbus_int32_t wire_first = (dbus_int32_t)first;
    dbus_int32_t wire_second = (dbus_int32_t)second;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &wire_first, DBUS_TYPE_INT32,
                             &wire_second, DBUS_TYPE_INVALID);
    return a11y_call(app, client, message);
}

static DBusMessage *a11y_method_int3(pt_app *app, DBusConnection *client, const char *bus,
                                     const char *path, const char *iface,
                                     const char *method, int32_t first, int32_t second,
                                     int32_t third) {
    DBusMessage *message = dbus_message_new_method_call(bus, path, iface, method);
    if (!message) {
        return NULL;
    }
    dbus_int32_t wire_first = (dbus_int32_t)first;
    dbus_int32_t wire_second = (dbus_int32_t)second;
    dbus_int32_t wire_third = (dbus_int32_t)third;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &wire_first, DBUS_TYPE_INT32,
                             &wire_second, DBUS_TYPE_INT32, &wire_third,
                             DBUS_TYPE_INVALID);
    return a11y_call(app, client, message);
}

static DBusMessage *a11y_get_property(pt_app *app, DBusConnection *client,
                                      const char *bus, const char *path,
                                      const char *iface, const char *property) {
    DBusMessage *message = dbus_message_new_method_call(
        bus, path, "org.freedesktop.DBus.Properties", "Get");
    if (!message) {
        return NULL;
    }
    dbus_message_append_args(message, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING,
                             &property, DBUS_TYPE_INVALID);
    return a11y_call(app, client, message);
}

static bool reply_string(DBusMessage *reply, char *out, size_t size) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter)) {
        return false;
    }
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        iter = variant;
    }
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING &&
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
        return false;
    }
    const char *text = NULL;
    dbus_message_iter_get_basic(&iter, &text);
    snprintf(out, size, "%s", text ? text : "");
    return true;
}

static bool reply_text_range(DBusMessage *reply, char *out, size_t size, int32_t *start,
                             int32_t *end) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
        return false;
    }
    const char *text = NULL;
    dbus_message_iter_get_basic(&iter, &text);
    snprintf(out, size, "%s", text ? text : "");
    dbus_message_iter_next(&iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_int32_t first = 0;
    dbus_message_iter_get_basic(&iter, &first);
    dbus_message_iter_next(&iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_int32_t second = 0;
    dbus_message_iter_get_basic(&iter, &second);
    *start = (int32_t)first;
    *end = (int32_t)second;
    return true;
}

static bool reply_int(DBusMessage *reply, int32_t *out) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter)) {
        return false;
    }
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        iter = variant;
    }
    int type = dbus_message_iter_get_arg_type(&iter);
    if (type == DBUS_TYPE_INT32) {
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(&iter, &value);
        *out = (int32_t)value;
        return true;
    }
    if (type == DBUS_TYPE_UINT32) {
        dbus_uint32_t value = 0;
        dbus_message_iter_get_basic(&iter, &value);
        *out = (int32_t)value;
        return true;
    }
    return false;
}

static bool reply_int_pair(DBusMessage *reply, int32_t *first, int32_t *second) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_int32_t a = 0;
    dbus_message_iter_get_basic(&iter, &a);
    dbus_message_iter_next(&iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_int32_t b = 0;
    dbus_message_iter_get_basic(&iter, &b);
    *first = (int32_t)a;
    *second = (int32_t)b;
    return true;
}

static bool reply_double(DBusMessage *reply, double *out) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter)) {
        return false;
    }
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        iter = variant;
    }
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_DOUBLE) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, out);
    return true;
}

static bool reply_bool(DBusMessage *reply, bool *out) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter)) {
        return false;
    }
    dbus_bool_t value = FALSE;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BOOLEAN) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, &value);
    *out = value != FALSE;
    return true;
}

static bool reply_ref(DBusMessage *reply, a11y_ref *out) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter)) {
        return false;
    }
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        iter = variant;
    }
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRUCT) {
        return false;
    }
    DBusMessageIter structure;
    dbus_message_iter_recurse(&iter, &structure);
    const char *bus = NULL;
    const char *path = NULL;
    if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&structure, &bus);
        dbus_message_iter_next(&structure);
        if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&structure, &path);
        }
    }
    if (!bus || !path) {
        return false;
    }
    snprintf(out->bus, sizeof(out->bus), "%s", bus);
    snprintf(out->path, sizeof(out->path), "%s", path);
    return true;
}

static int reply_refs(DBusMessage *reply, a11y_ref *refs, int max) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return 0;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return 0;
    }
    DBusMessageIter array;
    dbus_message_iter_recurse(&iter, &array);
    int count = 0;
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT &&
           count < max) {
        DBusMessageIter structure;
        dbus_message_iter_recurse(&array, &structure);
        const char *bus = NULL;
        const char *path = NULL;
        if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&structure, &bus);
            dbus_message_iter_next(&structure);
            if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_OBJECT_PATH) {
                dbus_message_iter_get_basic(&structure, &path);
            }
        }
        if (bus && path) {
            snprintf(refs[count].bus, sizeof(refs[count].bus), "%s", bus);
            snprintf(refs[count].path, sizeof(refs[count].path), "%s", path);
            ++count;
        }
        dbus_message_iter_next(&array);
    }
    return count;
}

static int reply_int_array(DBusMessage *reply, int32_t *out, int max) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return -1;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return -1;
    }
    DBusMessageIter array;
    dbus_message_iter_recurse(&iter, &array);
    int count = 0;
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_INT32 && count < max) {
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(&array, &value);
        out[count++] = (int32_t)value;
        dbus_message_iter_next(&array);
    }
    return count;
}

static bool reply_position(DBusMessage *reply, int32_t *row, int32_t *column) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
        return false;
    }
    DBusMessageIter variant;
    dbus_message_iter_recurse(&iter, &variant);
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRUCT) {
        return false;
    }
    DBusMessageIter structure;
    dbus_message_iter_recurse(&variant, &structure);
    dbus_int32_t values[2] = {0};
    int count = 0;
    while (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_INT32 && count < 2) {
        dbus_message_iter_get_basic(&structure, &values[count]);
        ++count;
        dbus_message_iter_next(&structure);
    }
    if (count != 2) {
        return false;
    }
    *row = (int32_t)values[0];
    *column = (int32_t)values[1];
    return true;
}

static bool reply_extents_at(DBusMessage *reply, bool *valid, int32_t *row,
                             int32_t *column, bool *selected) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BOOLEAN) {
        return false;
    }
    dbus_bool_t wire_valid = FALSE;
    dbus_message_iter_get_basic(&iter, &wire_valid);
    dbus_message_iter_next(&iter);
    dbus_int32_t values[4] = {0};
    for (int i = 0; i < 4; ++i) {
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
            return false;
        }
        dbus_message_iter_get_basic(&iter, &values[i]);
        dbus_message_iter_next(&iter);
    }
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BOOLEAN) {
        return false;
    }
    dbus_bool_t wire_selected = FALSE;
    dbus_message_iter_get_basic(&iter, &wire_selected);
    *valid = wire_valid != FALSE;
    *row = (int32_t)values[0];
    *column = (int32_t)values[1];
    *selected = wire_selected != FALSE;
    return true;
}

static bool reply_row_column_span(DBusMessage *reply, bool *valid, int32_t *row,
                                  int32_t *column, int32_t *row_span,
                                  int32_t *column_span) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BOOLEAN) {
        return false;
    }
    dbus_bool_t wire_valid = FALSE;
    dbus_message_iter_get_basic(&iter, &wire_valid);
    int32_t values[4] = {0};
    for (int i = 0; i < 4; ++i) {
        dbus_message_iter_next(&iter);
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
            return false;
        }
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(&iter, &value);
        values[i] = (int32_t)value;
    }
    *valid = wire_valid != FALSE;
    *row = values[0];
    *column = values[1];
    *row_span = values[2];
    *column_span = values[3];
    return true;
}

typedef struct a11y_relation_result {
    uint32_t type;
    int count;
    a11y_ref refs[4];
} a11y_relation_result;

static int reply_relations(DBusMessage *reply, a11y_relation_result *out, int max) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return 0;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return 0;
    }
    DBusMessageIter array;
    dbus_message_iter_recurse(&iter, &array);
    int count = 0;
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT && count < max) {
        DBusMessageIter structure;
        dbus_message_iter_recurse(&array, &structure);
        a11y_relation_result *result = &out[count];
        memset(result, 0, sizeof(*result));
        if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_UINT32) {
            dbus_uint32_t type = 0;
            dbus_message_iter_get_basic(&structure, &type);
            result->type = type;
        }
        dbus_message_iter_next(&structure);
        if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_ARRAY) {
            DBusMessageIter refs;
            dbus_message_iter_recurse(&structure, &refs);
            while (dbus_message_iter_get_arg_type(&refs) == DBUS_TYPE_STRUCT &&
                   result->count < 4) {
                DBusMessageIter ref;
                dbus_message_iter_recurse(&refs, &ref);
                const char *bus = NULL;
                const char *path = NULL;
                if (dbus_message_iter_get_arg_type(&ref) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&ref, &bus);
                    dbus_message_iter_next(&ref);
                    if (dbus_message_iter_get_arg_type(&ref) == DBUS_TYPE_OBJECT_PATH) {
                        dbus_message_iter_get_basic(&ref, &path);
                    }
                }
                if (bus && path) {
                    snprintf(result->refs[result->count].bus,
                             sizeof(result->refs[result->count].bus), "%s", bus);
                    snprintf(result->refs[result->count].path,
                             sizeof(result->refs[result->count].path), "%s", path);
                    ++result->count;
                }
                dbus_message_iter_next(&refs);
            }
        }
        ++count;
        dbus_message_iter_next(&array);
    }
    return count;
}

static const a11y_relation_result *find_relation(const a11y_relation_result *results,
                                                 int count, uint32_t type) {
    for (int i = 0; i < count; ++i) {
        if (results[i].type == type) {
            return &results[i];
        }
    }
    return NULL;
}

static bool relation_has_path(const a11y_relation_result *relation, const char *path) {
    if (!relation) {
        return false;
    }
    for (int i = 0; i < relation->count; ++i) {
        if (strcmp(relation->refs[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static int a11y_children(pt_app *app, DBusConnection *client, const char *bus,
                         const char *path, a11y_ref *refs, int max) {
    DBusMessage *reply =
        a11y_method(app, client, bus, path, "org.a11y.atspi.Accessible", "GetChildren");
    int count = reply_refs(reply, refs, max);
    if (reply) {
        dbus_message_unref(reply);
    }
    return count;
}

static int a11y_role(pt_app *app, DBusConnection *client, const char *bus,
                     const char *path) {
    DBusMessage *reply =
        a11y_method(app, client, bus, path, "org.a11y.atspi.Accessible", "GetRole");
    int32_t role = -1;
    if (!reply_int(reply, &role)) {
        role = -1;
    }
    if (reply) {
        dbus_message_unref(reply);
    }
    return role;
}

static bool a11y_name(pt_app *app, DBusConnection *client, const char *bus,
                      const char *path, char *name, size_t size) {
    DBusMessage *reply = a11y_get_property(app, client, bus, path,
                                           "org.a11y.atspi.Accessible", "Name");
    bool ok = reply_string(reply, name, size);
    if (reply) {
        dbus_message_unref(reply);
    }
    return ok;
}

static bool find_child(pt_app *app, DBusConnection *client, const char *bus,
                       const char *parent, int role, const char *name,
                       a11y_ref *out) {
    a11y_ref children[32];
    int count = a11y_children(app, client, bus, parent, children, 32);
    for (int i = 0; i < count; ++i) {
        if (a11y_role(app, client, children[i].bus, children[i].path) != role) {
            continue;
        }
        char child_name[256];
        if (name && (!a11y_name(app, client, children[i].bus, children[i].path,
                                child_name, sizeof(child_name)) ||
                     strcmp(child_name, name) != 0)) {
            continue;
        }
        *out = children[i];
        return true;
    }
    return false;
}

static bool a11y_interfaces_include(pt_app *app, DBusConnection *client, const char *bus,
                                    const char *path, const char *wanted) {
    DBusMessage *reply = a11y_method(app, client, bus, path,
                                     "org.a11y.atspi.Accessible", "GetInterfaces");
    bool found = false;
    if (reply && dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter array;
            dbus_message_iter_recurse(&iter, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
                const char *iface = NULL;
                dbus_message_iter_get_basic(&array, &iface);
                if (iface && strcmp(iface, wanted) == 0) {
                    found = true;
                    break;
                }
                dbus_message_iter_next(&array);
            }
        }
    }
    if (reply) {
        dbus_message_unref(reply);
    }
    return found;
}

static bool a11y_state_bit(pt_app *app, DBusConnection *client, const char *bus,
                           const char *path, unsigned bit) {
    DBusMessage *reply =
        a11y_method(app, client, bus, path, "org.a11y.atspi.Accessible", "GetState");
    bool set = false;
    if (reply && dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter array;
            dbus_message_iter_recurse(&iter, &array);
            unsigned index = 0;
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_UINT32) {
                dbus_uint32_t word = 0;
                dbus_message_iter_get_basic(&array, &word);
                if (index == bit / 32 && (word & (1u << (bit % 32))) != 0) {
                    set = true;
                }
                ++index;
                dbus_message_iter_next(&array);
            }
        }
    }
    if (reply) {
        dbus_message_unref(reply);
    }
    return set;
}

static bool a11y_attribute(pt_app *app, DBusConnection *client, const char *bus,
                           const char *path, const char *key, char *out, size_t size) {
    DBusMessage *reply = a11y_method(app, client, bus, path, "org.a11y.atspi.Accessible",
                                     "GetAttributes");
    bool found = false;
    if (reply && dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter array;
            dbus_message_iter_recurse(&iter, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter entry;
                dbus_message_iter_recurse(&array, &entry);
                const char *name = NULL;
                const char *value = NULL;
                if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&entry, &name);
                    dbus_message_iter_next(&entry);
                    if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&entry, &value);
                    }
                }
                if (name && value && strcmp(name, key) == 0) {
                    if (out && size > 0) {
                        snprintf(out, size, "%s", value);
                    }
                    found = true;
                    break;
                }
                dbus_message_iter_next(&array);
            }
        }
    }
    if (reply) {
        dbus_message_unref(reply);
    }
    return found;
}

/* Text.GetAttributes returns an attribute run: a {ss} dict plus the run's
 * start/end offsets (Unicode character offsets, per the Text contract). */
static bool reply_attributes(DBusMessage *reply, const char *key, char *out, size_t size,
                             int32_t *start, int32_t *end) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return false;
    }
    bool found = false;
    DBusMessageIter array;
    dbus_message_iter_recurse(&iter, &array);
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&array, &entry);
        const char *name = NULL;
        const char *value = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&entry, &name);
            dbus_message_iter_next(&entry);
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&entry, &value);
            }
        }
        if (name && value && strcmp(name, key) == 0) {
            snprintf(out, size, "%s", value);
            found = true;
        }
        dbus_message_iter_next(&array);
    }
    dbus_message_iter_next(&iter);
    dbus_int32_t first = -1;
    dbus_int32_t second = -1;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, &first);
    dbus_message_iter_next(&iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, &second);
    *start = (int32_t)first;
    *end = (int32_t)second;
    return found;
}

static bool reply_default_attributes(DBusMessage *reply, const char *key, char *out,
                                     size_t size) {
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        return false;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return false;
    }
    DBusMessageIter array;
    dbus_message_iter_recurse(&iter, &array);
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&array, &entry);
        const char *name = NULL;
        const char *value = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&entry, &name);
            dbus_message_iter_next(&entry);
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&entry, &value);
            }
        }
        if (name && value && strcmp(name, key) == 0) {
            snprintf(out, size, "%s", value);
            return true;
        }
        dbus_message_iter_next(&array);
    }
    return false;
}

static bool a11y_child_ref(pt_app *app, DBusConnection *client, const char *bus,
                           const char *parent, int index, a11y_ref *out) {
    DBusMessage *message = dbus_message_new_method_call(
        bus, parent, "org.a11y.atspi.Accessible", "GetChildAtIndex");
    if (!message) {
        return false;
    }
    dbus_int32_t child = index;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &child, DBUS_TYPE_INVALID);
    DBusMessage *reply = a11y_call(app, client, message);
    bool ok = reply_ref(reply, out);
    if (reply) {
        dbus_message_unref(reply);
    }
    return ok;
}

static bool a11y_extents(pt_app *app, DBusConnection *client, const char *bus,
                         const char *path, int *x, int *y, int *width, int *height) {    DBusMessage *message = dbus_message_new_method_call(
        bus, path, "org.a11y.atspi.Component", "GetExtents");
    if (!message) {
        return false;
    }
    dbus_uint32_t coord_type = 0;
    dbus_message_append_args(message, DBUS_TYPE_UINT32, &coord_type, DBUS_TYPE_INVALID);
    DBusMessage *reply = a11y_call(app, client, message);
    bool ok = false;
    if (reply && dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRUCT) {
            DBusMessageIter structure;
            dbus_message_iter_recurse(&iter, &structure);
            int32_t values[4] = {0};
            int index = 0;
            while (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_INT32 &&
                   index < 4) {
                dbus_int32_t value = 0;
                dbus_message_iter_get_basic(&structure, &value);
                values[index++] = value;
                dbus_message_iter_next(&structure);
            }
            if (index == 4) {
                *x = values[0];
                *y = values[1];
                *width = values[2];
                *height = values[3];
                ok = true;
            }
        }
    }
    if (reply) {
        dbus_message_unref(reply);
    }
    return ok;
}

static bool a11y_component_at_point(pt_app *app, DBusConnection *client, const char *bus,
                                    const char *path, int px, int py, a11y_ref *out) {
    DBusMessage *message = dbus_message_new_method_call(
        bus, path, "org.a11y.atspi.Component", "GetAccessibleAtPoint");
    if (!message) {
        return false;
    }
    dbus_int32_t x = (dbus_int32_t)px;
    dbus_int32_t y = (dbus_int32_t)py;
    dbus_uint32_t coord_type = 0;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                             DBUS_TYPE_UINT32, &coord_type, DBUS_TYPE_INVALID);
    DBusMessage *reply = a11y_call(app, client, message);
    bool ok = reply_ref(reply, out);
    if (reply) {
        dbus_message_unref(reply);
    }
    return ok;
}

static void test_bridge_live(pt_app *app, pt_widget *search, pt_widget *password,
                             pt_widget *volume, pt_widget *save, pt_widget *table,
                             pt_widget *tree, pt_tree_node *tree_root_node,
                             pt_tree_node *tree_child_node, pt_widget *tabs,
                             pt_widget *sidebar, pt_widget *notes) {
    DBusConnection *client = open_a11y_client();
    pt_check(client != NULL);
    if (!client) {
        return;
    }
    signal_watch watch = {0};
    dbus_connection_add_filter(client, watch_signals, &watch, NULL);

    char attribute[128];
    a11y_ref roots[16];
    int root_count = a11y_children(app, client, "org.a11y.atspi.Registry",
                                   "/org/a11y/atspi/accessible/root", roots, 16);
    pt_check(root_count > 0);
    a11y_ref app_root = {0};
    for (int i = 0; i < root_count; ++i) {
        char name[256];
        int role = a11y_role(app, client, roots[i].bus, roots[i].path);
        bool named = a11y_name(app, client, roots[i].bus, roots[i].path, name,
                               sizeof(name));
        if (role == atspi_role_application && named &&
            strcmp(name, "pt.tests.a11y-bridge") == 0) {
            app_root = roots[i];
            break;
        }
    }
    pt_check(app_root.path[0] != '\0');
    if (app_root.path[0] == '\0') {
        dbus_connection_close(client);
        dbus_connection_unref(client);
        return;
    }

    pt_check(a11y_role(app, client, app_root.bus, app_root.path) ==
             atspi_role_application);
    pt_check(a11y_state_bit(app, client, app_root.bus, app_root.path,
                            atspi_state_enabled));

    DBusMessage *reply = a11y_get_property(app, client, app_root.bus, app_root.path,
                                           "org.a11y.atspi.Application", "ToolkitName");
    char text[256] = {0};
    pt_check(reply_string(reply, text, sizeof(text)) && strcmp(text, "Pt") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    a11y_ref windows[8];
    pt_check(a11y_children(app, client, app_root.bus, app_root.path, windows, 8) == 1);
    pt_check(a11y_role(app, client, windows[0].bus, windows[0].path) == atspi_role_frame);
    pt_check(a11y_name(app, client, windows[0].bus, windows[0].path, text,
                       sizeof(text)) &&
             strcmp(text, "Bridge window") == 0);

    DBusMessage *child_message = dbus_message_new_method_call(
        windows[0].bus, windows[0].path, "org.a11y.atspi.Accessible",
        "GetChildAtIndex");
    dbus_int32_t zero = 0;
    dbus_message_append_args(child_message, DBUS_TYPE_INT32, &zero, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, child_message);
    a11y_ref root_widget = {0};
    pt_check(reply_ref(reply, &root_widget));
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(root_widget.path[0] != '\0');

    a11y_ref button = {0};
    a11y_ref entry = {0};
    a11y_ref password_entry = {0};
    a11y_ref slider = {0};
    a11y_ref label = {0};
    a11y_ref table_ref = {0};
    a11y_ref tree_ref = {0};
    a11y_ref notes_ref = {0};
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_push_button, "Save", &button));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path, atspi_role_entry,
                        "hello", &entry));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_password_text, "Password", &password_entry));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path, atspi_role_slider,
                        NULL, &slider));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path, atspi_role_label,
                        "Form", &label));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path, atspi_role_table,
                        NULL, &table_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path, atspi_role_tree,
                        NULL, &tree_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path, atspi_role_entry,
                        "Notes", &notes_ref));
    a11y_ref tabs_ref = {0};
    a11y_ref sidebar_ref = {0};
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_page_tab_list, NULL, &tabs_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_list, NULL, &sidebar_ref));
    if (button.path[0] == '\0' || entry.path[0] == '\0' ||
        password_entry.path[0] == '\0' || slider.path[0] == '\0' ||
        label.path[0] == '\0' || table_ref.path[0] == '\0' ||
        tree_ref.path[0] == '\0' || notes_ref.path[0] == '\0' ||
        tabs_ref.path[0] == '\0' || sidebar_ref.path[0] == '\0') {
        dbus_connection_close(client);
        dbus_connection_unref(client);
        return;
    }

    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(client, "type='signal',interface='org.a11y.atspi.Event.Object'",
                       &error);
    dbus_bus_add_match(client, "type='signal',interface='org.a11y.atspi.Event.Focus'",
                       &error);
    dbus_connection_flush(client);
    bool matched = !dbus_error_is_set(&error);
    dbus_error_free(&error);
    pt_check(matched);

    pt_check(a11y_state_bit(app, client, button.bus, button.path,
                            atspi_state_focusable));
    pt_check(a11y_interfaces_include(app, client, button.bus, button.path,
                                     "org.a11y.atspi.Action"));
    pt_check(a11y_interfaces_include(app, client, entry.bus, entry.path,
                                     "org.a11y.atspi.Text"));
    pt_check(a11y_interfaces_include(app, client, entry.bus, entry.path,
                                     "org.a11y.atspi.EditableText"));
    pt_check(!a11y_interfaces_include(app, client, label.bus, label.path,
                                      "org.a11y.atspi.Action"));
    pt_check(a11y_interfaces_include(app, client, label.bus, label.path,
                                     "org.a11y.atspi.Text"));
    pt_check(!a11y_interfaces_include(app, client, label.bus, label.path,
                                      "org.a11y.atspi.EditableText"));
    pt_check(a11y_interfaces_include(app, client, notes_ref.bus, notes_ref.path,
                                     "org.a11y.atspi.Text"));
    pt_check(a11y_interfaces_include(app, client, notes_ref.bus, notes_ref.path,
                                     "org.a11y.atspi.EditableText"));
    pt_check(a11y_interfaces_include(app, client, password_entry.bus,
                                     password_entry.path, "org.a11y.atspi.Text"));
    pt_check(a11y_interfaces_include(app, client, password_entry.bus,
                                     password_entry.path,
                                     "org.a11y.atspi.EditableText"));

    /* Text that a reader can inspect but not edit says READ_ONLY; both text
     * inputs, including a multi-line text area, support selection. */
    pt_check(a11y_state_bit(app, client, label.bus, label.path,
                            atspi_state_read_only));
    pt_check(!a11y_state_bit(app, client, label.bus, label.path,
                             atspi_state_selectable_text));
    pt_check(a11y_state_bit(app, client, entry.bus, entry.path,
                            atspi_state_selectable_text));
    pt_check(!a11y_state_bit(app, client, entry.bus, entry.path,
                             atspi_state_read_only));
    pt_check(a11y_state_bit(app, client, notes_ref.bus, notes_ref.path,
                            atspi_state_selectable_text));
    pt_check(!a11y_state_bit(app, client, notes_ref.bus, notes_ref.path,
                             atspi_state_read_only));
    pt_check(a11y_state_bit(app, client, password_entry.bus, password_entry.path,
                            atspi_state_selectable_text));
    pt_check(!a11y_state_bit(app, client, password_entry.bus, password_entry.path,
                             atspi_state_read_only));

    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    pt_check(a11y_extents(app, client, button.bus, button.path, &x, &y, &width, &height));
    pt_check(width > 0 && height > 0);
    pt_check(x == (int)save->x && y == (int)save->y);

    reply = a11y_method(app, client, button.bus, button.path, "org.a11y.atspi.Action",
                        "GetActions");
    int action_count = 0;
    if (reply && dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter array;
            dbus_message_iter_recurse(&iter, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
                ++action_count;
                dbus_message_iter_next(&array);
            }
        }
    }
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(action_count == 1);

    DBusMessage *action = dbus_message_new_method_call(
        button.bus, button.path, "org.a11y.atspi.Action", "DoAction");
    dbus_int32_t action_index = 0;
    dbus_message_append_args(action, DBUS_TYPE_INT32, &action_index, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, action);
    bool activated = false;
    pt_check(reply_bool(reply, &activated) && activated);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(button_clicks == 1);

    DBusMessage *get_text = dbus_message_new_method_call(
        entry.bus, entry.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t start = 0;
    dbus_int32_t end = 5;
    dbus_message_append_args(get_text, DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end,
                             DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_text);
    pt_check(reply_string(reply, text, sizeof(text)) && strcmp(text, "hello") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *get_tail = dbus_message_new_method_call(
        entry.bus, entry.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t tail_start = 1;
    dbus_int32_t tail_end = -1;
    dbus_message_append_args(get_tail, DBUS_TYPE_INT32, &tail_start, DBUS_TYPE_INT32,
                             &tail_end, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_tail);
    pt_check(reply_string(reply, text, sizeof(text)) && strcmp(text, "ello") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    reply = a11y_get_property(app, client, entry.bus, entry.path,
                              "org.a11y.atspi.Text", "CharacterCount");
    int32_t chars = 0;
    pt_check(reply_int(reply, &chars) && chars == 5);
    if (reply) {
        dbus_message_unref(reply);
    }

    /* A label is readable text, not an editable field. */
    DBusMessage *get_label_text = dbus_message_new_method_call(
        label.bus, label.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t label_start = 0;
    dbus_int32_t label_end = -1;
    dbus_message_append_args(get_label_text, DBUS_TYPE_INT32, &label_start,
                             DBUS_TYPE_INT32, &label_end, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_label_text);
    pt_check(reply_string(reply, text, sizeof(text)) && strcmp(text, "Form") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, label.bus, label.path,
                              "org.a11y.atspi.Text", "CharacterCount");
    int32_t label_chars = 0;
    pt_check(reply_int(reply, &label_chars) && label_chars == 4);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *get_word = dbus_message_new_method_call(
        label.bus, label.path, "org.a11y.atspi.Text", "GetStringAtOffset");
    dbus_int32_t word_offset = 0;
    dbus_uint32_t word_granularity = 1;
    dbus_message_append_args(get_word, DBUS_TYPE_INT32, &word_offset,
                             DBUS_TYPE_UINT32, &word_granularity, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_word);
    int32_t word_start = -1;
    int32_t word_end = -1;
    pt_check(reply_text_range(reply, text, sizeof(text), &word_start, &word_end) &&
             strcmp(text, "Form") == 0 && word_start == 0 && word_end == 4);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *get_label_char = dbus_message_new_method_call(
        label.bus, label.path, "org.a11y.atspi.Text", "GetCharacterAtOffset");
    dbus_int32_t label_char_offset = 1;
    dbus_message_append_args(get_label_char, DBUS_TYPE_INT32, &label_char_offset,
                             DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_label_char);
    int32_t label_char = 0;
    pt_check(reply_int(reply, &label_char) && label_char == 'o');
    if (reply) {
        dbus_message_unref(reply);
    }

    /* A text area is both readable and editable, including through UTF-8
     * character offsets. */
    DBusMessage *get_notes = dbus_message_new_method_call(
        notes_ref.bus, notes_ref.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t notes_start = 0;
    dbus_int32_t notes_end = -1;
    dbus_message_append_args(get_notes, DBUS_TYPE_INT32, &notes_start,
                             DBUS_TYPE_INT32, &notes_end, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_notes);
    pt_check(reply_string(reply, text, sizeof(text)) &&
             strcmp(text, "caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, notes_ref.bus, notes_ref.path,
                              "org.a11y.atspi.Text", "CharacterCount");
    pt_check(reply_int(reply, &chars) && chars == 7);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *get_notes_slice = dbus_message_new_method_call(
        notes_ref.bus, notes_ref.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t slice_start = 2;
    dbus_int32_t slice_end = 6;
    dbus_message_append_args(get_notes_slice, DBUS_TYPE_INT32, &slice_start,
                             DBUS_TYPE_INT32, &slice_end, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_notes_slice);
    pt_check(reply_string(reply, text, sizeof(text)) &&
             strcmp(text, "f\xc3\xa9 \xe4\xb8\x96") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    reply = a11y_method_int3(app, client, notes_ref.bus, notes_ref.path,
                             "org.a11y.atspi.Text", "SetSelection", 0, 1, 4);
    bool selected_text = false;
    pt_check(reply_bool(reply, &selected_text) && selected_text);
    if (reply) {
        dbus_message_unref(reply);
    }
    uint32_t selected_start = 0;
    uint32_t selected_end = 0;
    pt_check(pt_text_area_get_selection(notes, &selected_start, &selected_end) &&
             selected_start == 1 && selected_end == 5);
    reply = a11y_method(app, client, notes_ref.bus, notes_ref.path,
                        "org.a11y.atspi.Text", "GetSelection");
    int32_t selection_start = -1;
    int32_t selection_end = -1;
    pt_check(reply_int_pair(reply, &selection_start, &selection_end) &&
             selection_start == 1 && selection_end == 4);
    if (reply) {
        dbus_message_unref(reply);
    }

    reply = a11y_method_int1(app, client, notes_ref.bus, notes_ref.path,
                             "org.a11y.atspi.Text", "SetCaretOffset", 4);
    bool caret_set = false;
    pt_check(reply_bool(reply, &caret_set) && caret_set);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method(app, client, notes_ref.bus, notes_ref.path,
                        "org.a11y.atspi.Text", "GetSelection");
    pt_check(reply_int_pair(reply, &selection_start, &selection_end) &&
             selection_start == 4 && selection_end == 4);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *insert_text = dbus_message_new_method_call(
        notes_ref.bus, notes_ref.path, "org.a11y.atspi.EditableText", "InsertText");
    dbus_int32_t insert_offset = 4;
    const char *insert = "!";
    dbus_int32_t insert_length = 1;
    dbus_message_append_args(insert_text, DBUS_TYPE_INT32, &insert_offset,
                             DBUS_TYPE_STRING, &insert, DBUS_TYPE_INT32,
                             &insert_length, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, insert_text);
    bool inserted = false;
    pt_check(reply_bool(reply, &inserted) && inserted);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(strcmp(pt_widget_get_text(notes),
                    "caf\xc3\xa9! \xe4\xb8\x96\xe7\x95\x8c") == 0);

    reply = a11y_method_int2(app, client, notes_ref.bus, notes_ref.path,
                             "org.a11y.atspi.EditableText", "DeleteText", 4, 5);
    bool deleted = false;
    pt_check(reply_bool(reply, &deleted) && deleted);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(strcmp(pt_widget_get_text(notes),
                    "caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c") == 0);

    /* A password entry advertises Text and its real character count, but must
     * not return the text or a character code. */
    pt_check(strcmp(pt_widget_get_text(password), "secret") == 0);
    DBusMessage *get_password = dbus_message_new_method_call(
        password_entry.bus, password_entry.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t password_start = 0;
    dbus_int32_t password_end = -1;
    dbus_message_append_args(get_password, DBUS_TYPE_INT32, &password_start,
                             DBUS_TYPE_INT32, &password_end, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_password);
    pt_check(reply_string(reply, text, sizeof(text)) && text[0] == '\0');
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, password_entry.bus, password_entry.path,
                              "org.a11y.atspi.Text", "CharacterCount");
    pt_check(reply_int(reply, &chars) && chars == 6);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, password_entry.bus, password_entry.path,
                             "org.a11y.atspi.Text", "GetCharacterAtOffset", 0);
    int32_t password_char = -1;
    pt_check(reply_int(reply, &password_char) && password_char == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    /* The RTL direction stays visible even though the characters do not. */
    pt_check(a11y_attribute(app, client, password_entry.bus, password_entry.path,
                            "direction", attribute, sizeof(attribute)) &&
             strcmp(attribute, "rtl") == 0);
    reply = a11y_method(app, client, password_entry.bus, password_entry.path,
                        "org.a11y.atspi.Text", "GetDefaultAttributes");
    pt_check(reply_default_attributes(reply, "direction", attribute, sizeof(attribute)) &&
             strcmp(attribute, "rtl") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, password_entry.bus, password_entry.path,
                             "org.a11y.atspi.Text", "GetAttributes", 0);
    int32_t password_run_start = -1;
    int32_t password_run_end = -1;
    pt_check(reply_attributes(reply, "direction", attribute, sizeof(attribute),
                              &password_run_start, &password_run_end) &&
             strcmp(attribute, "rtl") == 0 && password_run_start == 0 &&
             password_run_end == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *set_text = dbus_message_new_method_call(
        entry.bus, entry.path, "org.a11y.atspi.EditableText", "SetTextContents");
    const char *new_text = "bye";
    dbus_message_append_args(set_text, DBUS_TYPE_STRING, &new_text, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, set_text);
    bool edited = false;
    pt_check(reply_bool(reply, &edited) && edited);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(strcmp(pt_widget_get_text(search), "bye") == 0);

    reply = a11y_get_property(app, client, slider.bus, slider.path,
                              "org.a11y.atspi.Value", "CurrentValue");
    double value = 0.0;
    pt_check(reply_double(reply, &value) && fabs(value - 0.25) < 0.001);
    if (reply) {
        dbus_message_unref(reply);
    }

    DBusMessage *set_value = dbus_message_new_method_call(
        slider.bus, slider.path, "org.freedesktop.DBus.Properties", "Set");
    const char *value_iface = "org.a11y.atspi.Value";
    const char *value_property = "CurrentValue";
    double new_value = 0.6;
    DBusMessageIter iter;
    dbus_message_iter_init_append(set_value, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &value_iface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &value_property);
    DBusMessageIter variant;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "d", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &new_value);
    dbus_message_iter_close_container(&iter, &variant);
    reply = a11y_call(app, client, set_value);
    pt_check(reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check_near(pt_slider_get_value(volume), 0.6, 0.001);

    /* Table and tree metadata travels through GetAttributes. */
    pt_check(a11y_attribute(app, client, table_ref.bus, table_ref.path, "sort-columns",
                            attribute, sizeof(attribute)));
    pt_check(strcmp(attribute, "2 ascending, 1 descending") == 0);
    a11y_ref header = {0};
    a11y_ref header0 = {0};
    a11y_ref header1 = {0};
    pt_check(a11y_child_ref(app, client, table_ref.bus, table_ref.path, 0, &header));
    pt_check(a11y_child_ref(app, client, header.bus, header.path, 0, &header0));
    pt_check(a11y_child_ref(app, client, header.bus, header.path, 1, &header1));
    pt_check(a11y_attribute(app, client, header0.bus, header0.path, "column-index",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "0") == 0);
    pt_check(a11y_attribute(app, client, header0.bus, header0.path, "sort", attribute,
                            sizeof(attribute)) &&
             strcmp(attribute, "none") == 0);
    pt_check(!a11y_attribute(app, client, header0.bus, header0.path, "sort-priority", NULL,
                             0));
    pt_check(a11y_attribute(app, client, header1.bus, header1.path, "sort", attribute,
                            sizeof(attribute)) &&
             strcmp(attribute, "descending") == 0);
    pt_check(a11y_attribute(app, client, header1.bus, header1.path, "sort-priority",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "2") == 0);

    a11y_ref row0 = {0};
    a11y_ref cell00 = {0};
    pt_check(a11y_child_ref(app, client, table_ref.bus, table_ref.path, 1, &row0));
    pt_check(a11y_attribute(app, client, row0.bus, row0.path, "row-index", attribute,
                            sizeof(attribute)) &&
             strcmp(attribute, "0") == 0);
    pt_check(a11y_attribute(app, client, row0.bus, row0.path, "selected", attribute,
                            sizeof(attribute)) &&
             strcmp(attribute, "true") == 0);
    pt_check(a11y_child_ref(app, client, row0.bus, row0.path, 0, &cell00));
    pt_check(a11y_attribute(app, client, cell00.bus, cell00.path, "row-index", attribute,
                            sizeof(attribute)) &&
             strcmp(attribute, "0") == 0);
    pt_check(a11y_attribute(app, client, cell00.bus, cell00.path, "column-index",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "0") == 0);

    a11y_ref tree_root = {0};
    a11y_ref tree_child = {0};
    pt_check(a11y_child_ref(app, client, tree_ref.bus, tree_ref.path, 0, &tree_root));
    pt_check(a11y_child_ref(app, client, tree_ref.bus, tree_ref.path, 1, &tree_child));
    pt_check(a11y_attribute(app, client, tree_root.bus, tree_root.path, "level",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "0") == 0);
    pt_check(a11y_attribute(app, client, tree_root.bus, tree_root.path, "expanded",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "true") == 0);
    pt_check(a11y_attribute(app, client, tree_child.bus, tree_child.path, "level",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "1") == 0);
    pt_check(!a11y_attribute(app, client, tree_child.bus, tree_child.path, "expanded", NULL,
                             0));
    pt_check(a11y_attribute(app, client, tree_child.bus, tree_child.path, "selected",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "true") == 0);

    /* The states a screen reader needs to offer the right gestures: the views
     * say their selection can hold more than one row, a row says it can be
     * added to that selection, and an open branch says it is open. */
    pt_check(a11y_state_bit(app, client, table_ref.bus, table_ref.path,
                            atspi_state_multiselectable));
    pt_check(a11y_state_bit(app, client, tree_ref.bus, tree_ref.path,
                            atspi_state_multiselectable));
    /* A sidebar and a tab strip both hold one choice, not a set. */
    pt_check(!a11y_state_bit(app, client, sidebar_ref.bus, sidebar_ref.path,
                             atspi_state_multiselectable));
    pt_check(!a11y_state_bit(app, client, tabs_ref.bus, tabs_ref.path,
                             atspi_state_multiselectable));
    pt_check(a11y_state_bit(app, client, row0.bus, row0.path, atspi_state_selectable));
    pt_check(a11y_state_bit(app, client, row0.bus, row0.path, atspi_state_selected));
    pt_check(a11y_state_bit(app, client, tree_root.bus, tree_root.path,
                            atspi_state_expandable));
    pt_check(a11y_state_bit(app, client, tree_root.bus, tree_root.path,
                            atspi_state_expanded));
    pt_check(!a11y_state_bit(app, client, tree_root.bus, tree_root.path,
                             atspi_state_collapsed));
    /* A leaf cannot open, so it reports neither half of the expand pair. */
    pt_check(!a11y_state_bit(app, client, tree_child.bus, tree_child.path,
                             atspi_state_expandable));
    pt_check(!a11y_state_bit(app, client, tree_child.bus, tree_child.path,
                             atspi_state_collapsed));
    /* AT-SPI names the folded half, so closing a branch says "collapsed"
     * instead of going quiet the way the model layer does. */
    pt_tree_node_set_expanded(tree_root_node, false);
    pt_check(a11y_state_bit(app, client, tree_root.bus, tree_root.path,
                            atspi_state_collapsed));
    pt_check(!a11y_state_bit(app, client, tree_root.bus, tree_root.path,
                             atspi_state_expanded));
    pt_tree_node_set_expanded(tree_root_node, true);

    /* Real AT-SPI Table, TableCell and Selection interfaces. */
    pt_check(a11y_interfaces_include(app, client, table_ref.bus, table_ref.path,
                                     "org.a11y.atspi.Table"));
    pt_check(a11y_interfaces_include(app, client, table_ref.bus, table_ref.path,
                                     "org.a11y.atspi.Selection"));
    pt_check(a11y_interfaces_include(app, client, cell00.bus, cell00.path,
                                     "org.a11y.atspi.TableCell"));
    pt_check(a11y_interfaces_include(app, client, tree_ref.bus, tree_ref.path,
                                     "org.a11y.atspi.Selection"));
    pt_check(a11y_interfaces_include(app, client, tabs_ref.bus, tabs_ref.path,
                                     "org.a11y.atspi.Selection"));
    pt_check(a11y_interfaces_include(app, client, sidebar_ref.bus, sidebar_ref.path,
                                     "org.a11y.atspi.Selection"));
    pt_check(!a11y_interfaces_include(app, client, tree_ref.bus, tree_ref.path,
                                      "org.a11y.atspi.Table"));

    int32_t number = 0;
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Table", "NRows");
    pt_check(reply_int(reply, &number) && number == 5);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Table", "NColumns");
    pt_check(reply_int(reply, &number) && number == 3);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Table", "NSelectedRows");
    pt_check(reply_int(reply, &number) && number == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Table", "NSelectedColumns");
    pt_check(reply_int(reply, &number) && number == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    a11y_ref row1 = {0};
    a11y_ref cell01 = {0};
    pt_check(a11y_child_ref(app, client, table_ref.bus, table_ref.path, 2, &row1));
    pt_check(a11y_child_ref(app, client, row0.bus, row0.path, 1, &cell01));

    reply = a11y_method_int2(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetIndexAt", 0, 1);
    pt_check(reply_int(reply, &number) && number == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetRowAtIndex", 1);
    pt_check(reply_int(reply, &number) && number == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetColumnAtIndex", 1);
    pt_check(reply_int(reply, &number) && number == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetColumnDescription", 1);
    pt_check(reply_string(reply, text, sizeof(text)) && strcmp(text, "Modified") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int2(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetAccessibleAt", 0, 1);
    a11y_ref accessible_cell = {0};
    pt_check(reply_ref(reply, &accessible_cell) &&
             strcmp(accessible_cell.path, cell01.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetColumnHeader", 1);
    a11y_ref column_header = {0};
    pt_check(reply_ref(reply, &column_header) &&
             strcmp(column_header.path, header1.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    reply = a11y_method(app, client, table_ref.bus, table_ref.path,
                        "org.a11y.atspi.Table", "GetSelectedRows");
    int32_t selected_rows[8];
    int selected_count = reply_int_array(reply, selected_rows, 8);
    pt_check(selected_count == 1 && selected_rows[0] == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    bool bool_value = false;
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "IsRowSelected", 0);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "IsColumnSelected", 0);
    pt_check(reply_bool(reply, &bool_value) && !bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int2(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "IsSelected", 0, 0);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "GetRowColumnExtentsAtIndex", 1);
    bool extents_valid = false;
    bool extents_selected = false;
    int32_t extents_row = -1;
    int32_t extents_column = -1;
    bool extents_ok = reply_extents_at(reply, &extents_valid, &extents_row,
                                       &extents_column, &extents_selected);
    pt_check(extents_ok && extents_valid && extents_row == 0 && extents_column == 1 &&
             extents_selected);
    if (reply) {
        dbus_message_unref(reply);
    }

    int selection_events = watch.selection_changed;
    int selected_events = watch.selected_changed;
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "AddRowSelection", 1);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == 1);
    for (int i = 0; i < 100 &&
                    (watch.selection_changed == selection_events ||
                     watch.selected_changed == selected_events);
         ++i) {
        dbus_connection_read_write_dispatch(client, 10);
        pt_platform_dispatch(app, 0);
    }
    pt_check(watch.selection_changed > selection_events);
    pt_check(watch.selected_changed > selected_events);
    pt_check(strcmp(watch.selected_path, row1.path) == 0);
    /* Adding a row keeps the row that was already selected, so the table now
     * reports two, and both interfaces agree on the set. */
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Selection", "NSelectedChildren");
    pt_check(reply_int(reply, &number) && number == 2);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method(app, client, table_ref.bus, table_ref.path,
                        "org.a11y.atspi.Table", "GetSelectedRows");
    selected_count = reply_int_array(reply, selected_rows, 8);
    pt_check(selected_count == 2 && selected_rows[0] == 0 && selected_rows[1] == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "GetSelectedChild", 1);
    a11y_ref second_selected = {0};
    pt_check(reply_ref(reply, &second_selected) &&
             strcmp(second_selected.path, row1.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "RemoveRowSelection", 1);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    /* Removing one row leaves the rest of the set alone and moves the caret to
     * the lowest row still selected. */
    pt_check(pt_table_get_selected(table) == 0);
    pt_check(pt_table_get_selection_count(table) == 1);
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Table", "AddRowSelection", 0);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == 0);

    /* Selection interface over the same table. */
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Selection", "NSelectedChildren");
    pt_check(reply_int(reply, &number) && number == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "GetSelectedChild", 0);
    a11y_ref selected_child = {0};
    pt_check(reply_ref(reply, &selected_child) &&
             strcmp(selected_child.path, row0.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "IsChildSelected", 1);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "SelectChild", 2);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == 1);
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "DeselectSelectedChild", 0);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == pt_list_index_none);
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "SelectChild", 2);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "DeselectChild", 2);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == pt_list_index_none);
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "SelectChild", 1);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == 0);
    reply = a11y_method(app, client, table_ref.bus, table_ref.path,
                        "org.a11y.atspi.Selection", "SelectAll");
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    /* The row views can hold a whole selection, so SelectAll reaches every one of
     * the five rows through the same set the rest of the interfaces read. */
    pt_check(pt_table_get_selection_count(table) == 5);
    reply = a11y_get_property(app, client, table_ref.bus, table_ref.path,
                              "org.a11y.atspi.Selection", "NSelectedChildren");
    pt_check(reply_int(reply, &number) && number == 5);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "DeselectSelectedChild", 2);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selection_count(table) == 4);
    pt_check(!pt_table_is_row_selected(table, 2));
    reply = a11y_method(app, client, table_ref.bus, table_ref.path,
                        "org.a11y.atspi.Selection", "ClearSelection");
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_table_get_selected(table) == pt_list_index_none);
    reply = a11y_method_int1(app, client, table_ref.bus, table_ref.path,
                             "org.a11y.atspi.Selection", "SelectChild", 1);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }

    /* TableCell reports its position, span, header cells and owning table. */
    reply = a11y_get_property(app, client, cell00.bus, cell00.path,
                              "org.a11y.atspi.TableCell", "RowSpan");
    pt_check(reply_int(reply, &number) && number == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, cell00.bus, cell00.path,
                              "org.a11y.atspi.TableCell", "ColumnSpan");
    pt_check(reply_int(reply, &number) && number == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, cell00.bus, cell00.path,
                              "org.a11y.atspi.TableCell", "Position");
    int32_t position_row = -1;
    int32_t position_column = -1;
    pt_check(reply_position(reply, &position_row, &position_column) &&
             position_row == 0 && position_column == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_get_property(app, client, cell00.bus, cell00.path,
                              "org.a11y.atspi.TableCell", "Table");
    a11y_ref owning_table = {0};
    pt_check(reply_ref(reply, &owning_table) &&
             strcmp(owning_table.path, table_ref.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method(app, client, cell00.bus, cell00.path,
                        "org.a11y.atspi.TableCell", "GetRowColumnSpan");
    bool span_valid = false;
    int32_t span_row = -1;
    int32_t span_column = -1;
    int32_t row_span = 0;
    int32_t column_span = 0;
    pt_check(reply_row_column_span(reply, &span_valid, &span_row, &span_column,
                                   &row_span, &column_span) &&
             span_valid && span_row == 0 && span_column == 0 && row_span == 1 &&
             column_span == 1);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method(app, client, cell00.bus, cell00.path,
                        "org.a11y.atspi.TableCell", "GetColumnHeaderCells");
    a11y_ref header_cells[4];
    int header_cell_count = reply_refs(reply, header_cells, 4);
    pt_check(header_cell_count == 1 &&
             strcmp(header_cells[0].path, header0.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    /* Relations connect cells, headers, rows and the owning table. */
    a11y_relation_result relations[8];
    reply = a11y_method(app, client, cell00.bus, cell00.path,
                        "org.a11y.atspi.Accessible", "GetRelationSet");
    int relation_count = reply_relations(reply, relations, 8);
    if (reply) {
        dbus_message_unref(reply);
    }
    const a11y_relation_result *member_of =
        find_relation(relations, relation_count, 5);
    pt_check(relation_has_path(member_of, table_ref.path));
    const a11y_relation_result *labelled_by =
        find_relation(relations, relation_count, 2);
    pt_check(relation_has_path(labelled_by, header0.path));
    reply = a11y_method(app, client, tree_child.bus, tree_child.path,
                        "org.a11y.atspi.Accessible", "GetRelationSet");
    relation_count = reply_relations(reply, relations, 8);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(relation_has_path(find_relation(relations, relation_count, 5),
                               tree_ref.path));
    pt_check(relation_has_path(find_relation(relations, relation_count, 7),
                               tree_root.path));
    reply = a11y_method(app, client, tree_root.bus, tree_root.path,
                        "org.a11y.atspi.Accessible", "GetRelationSet");
    relation_count = reply_relations(reply, relations, 8);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(relation_has_path(find_relation(relations, relation_count, 8),
                               tree_child.path));

    /* Tree, tabs and sidebar expose Selection over their accessible children. */
    reply = a11y_method_int1(app, client, tree_ref.bus, tree_ref.path,
                             "org.a11y.atspi.Selection", "GetSelectedChild", 0);
    a11y_ref tree_selected = {0};
    pt_check(reply_ref(reply, &tree_selected) &&
             strcmp(tree_selected.path, tree_child.path) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method_int1(app, client, tree_ref.bus, tree_ref.path,
                             "org.a11y.atspi.Selection", "SelectChild", 0);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_tree_get_selected(tree) == tree_root_node);
    reply = a11y_method_int1(app, client, tree_ref.bus, tree_ref.path,
                             "org.a11y.atspi.Selection", "SelectChild", 1);
    pt_check(reply_bool(reply, &bool_value) && bool_value);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(pt_tree_get_selected(tree) == tree_child_node);

    a11y_ref tab_pages[4];
    a11y_ref side_items[4];
    int tab_count = a11y_children(app, client, tabs_ref.bus, tabs_ref.path, tab_pages, 4);
    int item_count =
        a11y_children(app, client, sidebar_ref.bus, sidebar_ref.path, side_items, 4);
    pt_check(tab_count == 2 && item_count == 2);
    if (tab_count == 2 && item_count == 2) {
        reply = a11y_method_int1(app, client, tabs_ref.bus, tabs_ref.path,
                                 "org.a11y.atspi.Selection", "GetSelectedChild", 0);
        a11y_ref tab_selected = {0};
        pt_check(reply_ref(reply, &tab_selected) &&
                 strcmp(tab_selected.path, tab_pages[1].path) == 0);
        if (reply) {
            dbus_message_unref(reply);
        }
        reply = a11y_method_int1(app, client, tabs_ref.bus, tabs_ref.path,
                                 "org.a11y.atspi.Selection", "SelectChild", 0);
        pt_check(reply_bool(reply, &bool_value) && bool_value);
        if (reply) {
            dbus_message_unref(reply);
        }
        pt_check(pt_tabs_get_active(tabs) == 0);
        reply = a11y_method_int1(app, client, tabs_ref.bus, tabs_ref.path,
                                 "org.a11y.atspi.Selection", "IsChildSelected", 0);
        pt_check(reply_bool(reply, &bool_value) && bool_value);
        if (reply) {
            dbus_message_unref(reply);
        }
        reply = a11y_method_int1(app, client, tabs_ref.bus, tabs_ref.path,
                                 "org.a11y.atspi.Selection", "SelectChild", 1);
        pt_check(reply_bool(reply, &bool_value) && bool_value);
        if (reply) {
            dbus_message_unref(reply);
        }
        pt_check(pt_tabs_get_active(tabs) == 1);

        reply = a11y_method_int1(app, client, sidebar_ref.bus, sidebar_ref.path,
                                 "org.a11y.atspi.Selection", "GetSelectedChild", 0);
        a11y_ref side_selected = {0};
        pt_check(reply_ref(reply, &side_selected) &&
                 strcmp(side_selected.path, side_items[1].path) == 0);
        if (reply) {
            dbus_message_unref(reply);
        }
        reply = a11y_method_int1(app, client, sidebar_ref.bus, sidebar_ref.path,
                                 "org.a11y.atspi.Selection", "SelectChild", 0);
        pt_check(reply_bool(reply, &bool_value) && bool_value);
        if (reply) {
            dbus_message_unref(reply);
        }
        pt_check(pt_sidebar_get_active(sidebar) == 0);
        reply = a11y_method_int1(app, client, sidebar_ref.bus, sidebar_ref.path,
                                 "org.a11y.atspi.Selection", "SelectChild", 1);
        pt_check(reply_bool(reply, &bool_value) && bool_value);
        if (reply) {
            dbus_message_unref(reply);
        }
        pt_check(pt_sidebar_get_active(sidebar) == 1);
    }

    /* Focus events reach the bus for ordinary widgets and overlay focus. */
    int focus_before = watch.focus;
    pt_check(pt_widget_focus(tabs));
    for (int i = 0; i < 100 && watch.focus == focus_before; ++i) {
        dbus_connection_read_write_dispatch(client, 10);
        pt_platform_dispatch(app, 0);
    }
    pt_check(watch.focus > focus_before);
    pt_check(strcmp(watch.focus_path, tabs_ref.path) == 0);
    pt_check(pt_widget_focus(save));

    /* Toasts are assertive live regions with announcements and showing state. */
    pt_window *a11y_window = pt_widget_window(search);
    int announcements_before = watch.announcement;
    int showing_before = watch.showing_changed;
    pt_toast *toast = pt_window_show_toast(a11y_window, "Disk almost full",
                                           pt_toast_warning, 0);
    pt_check(toast != NULL);
    for (int i = 0; i < 200 && (watch.announcement == announcements_before ||
                                watch.showing_changed == showing_before);
         ++i) {
        dbus_connection_read_write_dispatch(client, 10);
        pt_platform_dispatch(app, 0);
    }
    pt_check(watch.announcement == announcements_before + 1);
    pt_check(watch.announcement_politeness == 2);
    pt_check(strcmp(watch.announcement_text, "Disk almost full") == 0);
    pt_check(watch.showing_changed > showing_before && watch.showing_value);
    a11y_ref alert_ref = {0};
    pt_check(find_child(app, client, windows[0].bus, windows[0].path, atspi_role_alert,
                        "Disk almost full", &alert_ref));
    if (alert_ref.path[0] != '\0') {
        pt_check(strcmp(watch.showing_path, alert_ref.path) == 0);
        pt_check(a11y_state_bit(app, client, alert_ref.bus, alert_ref.path,
                                atspi_state_showing));
        pt_check(a11y_attribute(app, client, alert_ref.bus, alert_ref.path, "live",
                                attribute, sizeof(attribute)) &&
                 strcmp(attribute, "assertive") == 0);
        pt_check(a11y_attribute(app, client, alert_ref.bus, alert_ref.path, "atomic",
                                attribute, sizeof(attribute)) &&
                 strcmp(attribute, "true") == 0);
        reply = a11y_method(app, client, alert_ref.bus, alert_ref.path,
                            "org.a11y.atspi.Accessible", "GetRelationSet");
        relation_count = reply_relations(reply, relations, 8);
        if (reply) {
            dbus_message_unref(reply);
        }
        pt_check(relation_has_path(find_relation(relations, relation_count, 12),
                                   windows[0].path));
    }
    int showing_after_open = watch.showing_changed;
    pt_toast_dismiss(toast);
    for (int i = 0; i < 200 && watch.showing_changed == showing_after_open; ++i) {
        dbus_connection_read_write_dispatch(client, 10);
        pt_platform_dispatch(app, 0);
    }
    pt_check(watch.showing_changed > showing_after_open && !watch.showing_value);

    /* Dialogs announce their message assertively and move focus inside. */
    int dialog_focus_before = watch.focus;
    int dialog_announcements = watch.announcement;
    pt_dialog *dialog = pt_dialog_create(a11y_window, "Confirm",
                                         "Delete everything?");
    pt_dialog_add_action(dialog, "Cancel", false, NULL, NULL);
    pt_dialog_add_action(dialog, "Delete", true, NULL, NULL);
    pt_dialog_show(dialog);
    for (int i = 0; i < 200 && (watch.focus == dialog_focus_before ||
                                watch.announcement == dialog_announcements);
         ++i) {
        dbus_connection_read_write_dispatch(client, 10);
        pt_platform_dispatch(app, 0);
    }
    pt_check(watch.focus > dialog_focus_before);
    pt_check(watch.announcement > dialog_announcements);
    pt_check(watch.announcement_politeness == 2);
    pt_check(strcmp(watch.announcement_text, "Delete everything?") == 0);
    a11y_ref dialog_ref = {0};
    pt_check(find_child(app, client, windows[0].bus, windows[0].path, atspi_role_dialog,
                        "Confirm", &dialog_ref));
    if (dialog_ref.path[0] != '\0') {
        pt_check(a11y_state_bit(app, client, dialog_ref.bus, dialog_ref.path,
                                atspi_state_showing));
    }
    pt_dialog_destroy(dialog);

    DBusMessage *introspect = dbus_message_new_method_call(
        button.bus, button.path, "org.freedesktop.DBus.Introspectable", "Introspect");
    reply = a11y_call(app, client, introspect);
    char xml[4096];
    bool introspected = reply_string(reply, xml, sizeof(xml)) &&
                        strstr(xml, "org.a11y.atspi.Accessible") != NULL;
    pt_check(introspected);
    if (reply) {
        dbus_message_unref(reply);
    }

    watch.path = entry.path;
    pt_widget_set_text(search, "event");
    for (int i = 0; i < 200 && watch.text_changed == 0; ++i) {
        dbus_connection_read_write_dispatch(client, 20);
        pt_platform_dispatch(app, 0);
    }
    pt_check(watch.text_changed > 0);

    a11y_ref missing = {.bus = "", .path = "/org/pt/a11y/w99999"};
    snprintf(missing.bus, sizeof(missing.bus), "%s", app_root.bus);
    reply = a11y_method(app, client, missing.bus, missing.path,
                        "org.a11y.atspi.Accessible", "GetRole");
    pt_check(reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR &&
             strcmp(dbus_message_get_error_name(reply), DBUS_ERROR_UNKNOWN_OBJECT) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    dbus_connection_close(client);
    dbus_connection_unref(client);
}
/* Direction attributes: object-level UI direction on every control, and the
 * Text run's base direction with character-offset ranges. */
static void test_bridge_direction(pt_app *app, pt_widget *rtl_field) {
    DBusConnection *client = open_a11y_client();
    pt_check(client != NULL);
    if (!client) {
        return;
    }
    a11y_ref roots[16];
    int root_count = a11y_children(app, client, "org.a11y.atspi.Registry",
                                   "/org/a11y/atspi/accessible/root", roots, 16);
    pt_check(root_count > 0);
    a11y_ref app_root = {0};
    for (int i = 0; i < root_count; ++i) {
        char name[256];
        if (a11y_role(app, client, roots[i].bus, roots[i].path) == atspi_role_application &&
            a11y_name(app, client, roots[i].bus, roots[i].path, name, sizeof(name)) &&
            strcmp(name, "pt.tests.a11y-bridge") == 0) {
            app_root = roots[i];
            break;
        }
    }
    pt_check(app_root.path[0] != '\0');
    if (app_root.path[0] == '\0') {
        dbus_connection_close(client);
        dbus_connection_unref(client);
        return;
    }
    a11y_ref window_ref = {0};
    pt_check(find_child(app, client, app_root.bus, app_root.path, atspi_role_frame,
                        NULL, &window_ref));
    DBusMessage *child_message = dbus_message_new_method_call(
        window_ref.bus, window_ref.path, "org.a11y.atspi.Accessible", "GetChildAtIndex");
    dbus_int32_t zero = 0;
    dbus_message_append_args(child_message, DBUS_TYPE_INT32, &zero, DBUS_TYPE_INVALID);
    DBusMessage *reply = a11y_call(app, client, child_message);
    a11y_ref root_widget = {0};
    pt_check(reply_ref(reply, &root_widget));
    if (reply) {
        dbus_message_unref(reply);
    }
    a11y_ref hebrew_ref = {0};
    a11y_ref arabic_ref = {0};
    a11y_ref rtl_field_ref = {0};
    a11y_ref slider_ref = {0};
    a11y_ref save_ref = {0};
    pt_check(find_child(app, client, window_ref.bus, root_widget.path, atspi_role_label,
                        "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", &hebrew_ref));
    pt_check(find_child(app, client, window_ref.bus, root_widget.path, atspi_role_label,
                        "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", &arabic_ref));
    pt_check(find_child(app, client, window_ref.bus, root_widget.path, atspi_role_entry,
                        "RTL field", &rtl_field_ref));
    pt_check(find_child(app, client, window_ref.bus, root_widget.path, atspi_role_slider,
                        NULL, &slider_ref));
    pt_check(find_child(app, client, window_ref.bus, root_widget.path,
                        atspi_role_push_button, "Save", &save_ref));
    if (hebrew_ref.path[0] == '\0' || arabic_ref.path[0] == '\0' ||
        rtl_field_ref.path[0] == '\0' ||
        slider_ref.path[0] == '\0' || save_ref.path[0] == '\0') {
        pt_check(false);
        dbus_connection_close(client);
        dbus_connection_unref(client);
        return;
    }

    char attribute[128];
    int32_t run_start = -1;
    int32_t run_end = -1;
    char text[256];

    /* Hebrew: 4 scalars but 8 UTF-8 bytes; the run range is in scalars. */
    pt_check(a11y_attribute(app, client, hebrew_ref.bus, hebrew_ref.path, "direction",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "rtl") == 0);
    reply = a11y_method_int1(app, client, hebrew_ref.bus, hebrew_ref.path,
                             "org.a11y.atspi.Text", "GetAttributes", 0);
    pt_check(reply_attributes(reply, "direction", attribute, sizeof(attribute), &run_start,
                              &run_end) &&
             strcmp(attribute, "rtl") == 0 && run_start == 0 && run_end == 4);
    if (reply) {
        dbus_message_unref(reply);
    }
    DBusMessage *get_hebrew = dbus_message_new_method_call(
        hebrew_ref.bus, hebrew_ref.path, "org.a11y.atspi.Text", "GetText");
    dbus_int32_t hebrew_start = 0;
    dbus_int32_t hebrew_end = -1;
    dbus_message_append_args(get_hebrew, DBUS_TYPE_INT32, &hebrew_start, DBUS_TYPE_INT32,
                             &hebrew_end, DBUS_TYPE_INVALID);
    reply = a11y_call(app, client, get_hebrew);
    pt_check(reply_string(reply, text, sizeof(text)) &&
             strcmp(text, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d") == 0 && strlen(text) == 8);
    if (reply) {
        dbus_message_unref(reply);
    }
    reply = a11y_method(app, client, hebrew_ref.bus, hebrew_ref.path, "org.a11y.atspi.Text",
                        "GetDefaultAttributes");
    pt_check(reply_default_attributes(reply, "direction", attribute, sizeof(attribute)) &&
             strcmp(attribute, "rtl") == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    /* Arabic takes the same path and resolves through auto detection: a
     * label with no explicit direction whose text starts with RTL letters. */
    reply = a11y_method_int1(app, client, arabic_ref.bus, arabic_ref.path,
                             "org.a11y.atspi.Text", "GetAttributes", 2);
    pt_check(reply_attributes(reply, "direction", attribute, sizeof(attribute), &run_start,
                              &run_end) &&
             strcmp(attribute, "rtl") == 0 && run_start == 0 && run_end == 5);
    if (reply) {
        dbus_message_unref(reply);
    }

    /* An RTL UI direction with LTR text: the run direction follows the
     * explicit text direction, not the text contents. */
    pt_widget_set_text_direction(rtl_field, pt_text_direction_ltr);
    reply = a11y_method_int1(app, client, rtl_field_ref.bus, rtl_field_ref.path,
                             "org.a11y.atspi.Text", "GetAttributes", 0);
    pt_check(reply_attributes(reply, "direction", attribute, sizeof(attribute), &run_start,
                              &run_end) &&
             strcmp(attribute, "ltr") == 0 && run_start == 0 && run_end == 9);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_widget_set_text_direction(rtl_field, pt_text_direction_auto);
    reply = a11y_method_int1(app, client, rtl_field_ref.bus, rtl_field_ref.path,
                             "org.a11y.atspi.Text", "GetAttributes", 0);
    pt_check(reply_attributes(reply, "direction", attribute, sizeof(attribute), &run_start,
                              &run_end) &&
             strcmp(attribute, "rtl") == 0 && run_start == 0 && run_end == 9);
    if (reply) {
        dbus_message_unref(reply);
    }

    /* Auto detection resolves the strong LTR letters in "Save" to ltr.
     * A neutral-only label would instead fall back to the UI direction,
     * reported explicitly as "ltr" or "rtl". */
    a11y_ref button_ref = save_ref;
    reply = a11y_method_int1(app, client, button_ref.bus, button_ref.path,
                             "org.a11y.atspi.Text", "GetAttributes", 0);
    pt_check(reply_attributes(reply, "direction", attribute, sizeof(attribute), &run_start,
                              &run_end) &&
             strcmp(attribute, "ltr") == 0 && run_start == 0 && run_end == 4);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(a11y_attribute(app, client, button_ref.bus, button_ref.path, "direction",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "ltr") == 0);

    /* Non-text controls carry only the object-level direction. */
    pt_check(a11y_attribute(app, client, slider_ref.bus, slider_ref.path, "direction",
                            attribute, sizeof(attribute)) &&
             strcmp(attribute, "ltr") == 0);
    reply = a11y_method(app, client, slider_ref.bus, slider_ref.path, "org.a11y.atspi.Text",
                        "GetAttributes");
    pt_check(reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR &&
             strcmp(dbus_message_get_error_name(reply), DBUS_ERROR_UNKNOWN_METHOD) == 0);
    if (reply) {
        dbus_message_unref(reply);
    }

    dbus_connection_close(client);
    dbus_connection_unref(client);
}
#endif /* PT_HAVE_DBUS */

#ifdef PT_HAVE_DBUS
/* ---------- private accessibility bus (2.0 wave 7) ----------
 *
 * The live checks need a registry, but the user's session registry is shared
 * state: two concurrent runs or an AT-SPI client re-scanning the tree can make
 * an individual check flap. The test therefore re-executes itself under a
 * private dbus-run-session with a private XDG_RUNTIME_DIR, starts one
 * at-spi2-registryd on that session's accessibility bus and kills it on exit.
 * Nothing in the real session bus or registry is touched. When the tools are
 * unavailable the test falls back to the old in-process behaviour.
 */

static pid_t a11y_registry_pid = -1;

static void stop_a11y_registry(void) {
    if (a11y_registry_pid <= 0) {
        return;
    }
    kill(a11y_registry_pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t done = waitpid(a11y_registry_pid, &status, WNOHANG);
        if (done == a11y_registry_pid || (done < 0 && errno == ECHILD)) {
            a11y_registry_pid = -1;
            return;
        }
        struct timespec pause = {0, 50 * 1000 * 1000};
        nanosleep(&pause, NULL);
    }
    kill(a11y_registry_pid, SIGKILL);
    waitpid(a11y_registry_pid, NULL, 0);
    a11y_registry_pid = -1;
}

static bool find_executable(const char *name, char *out, size_t capacity) {
    if (!name || !name[0]) {
        return false;
    }
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) {
            snprintf(out, capacity, "%s", name);
            return true;
        }
        return false;
    }
    const char *path = getenv("PATH");
    if (!path) {
        path = "/usr/local/bin:/usr/bin:/bin";
    }
    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ':');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        char candidate[PATH_MAX];
        if (length > 0 && length + strlen(name) + 2 < sizeof(candidate)) {
            memcpy(candidate, cursor, length);
            candidate[length] = '/';
            snprintf(candidate + length + 1, sizeof(candidate) - length - 1, "%s", name);
            if (access(candidate, X_OK) == 0) {
                snprintf(out, capacity, "%s", candidate);
                return true;
            }
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    return false;
}

static bool find_registry_binary(char *out, size_t capacity) {
    static const char *const candidates[] = {
        "/usr/lib/at-spi2-registryd", "/usr/libexec/at-spi2-registryd",
        "at-spi2-registryd"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (find_executable(candidates[i], out, capacity)) {
            return true;
        }
    }
    return false;
}

static bool session_bus_reachable(void) {
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *session = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    dbus_error_free(&error);
    if (!session) {
        return false;
    }
    dbus_connection_close(session);
    dbus_connection_unref(session);
    return true;
}

static bool query_a11y_address(char *out, size_t capacity) {
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *session = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!session) {
        dbus_error_free(&error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(session, FALSE);
    DBusMessage *request = dbus_message_new_method_call(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    DBusMessage *reply = request ? dbus_connection_send_with_reply_and_block(
                                       session, request, 3000, &error)
                                 : NULL;
    if (request) {
        dbus_message_unref(request);
    }
    const char *address = NULL;
    bool ok = false;
    if (reply) {
        char *value = NULL;
        if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &value,
                                  DBUS_TYPE_INVALID) &&
            value && value[0]) {
            snprintf(out, capacity, "%s", value);
            ok = true;
        }
        dbus_message_unref(reply);
    }
    (void)address;
    dbus_error_free(&error);
    dbus_connection_close(session);
    dbus_connection_unref(session);
    return ok;
}

/* Starts the private registry on the accessibility bus the private session bus
 * hands out, then waits until its well-known name is owned. */
static void start_private_a11y_registry(void) {
    char address[512];
    char binary[PATH_MAX];
    if (!query_a11y_address(address, sizeof(address))) {
        printf("# private a11y bus has no address; live checks will skip\n");
        return;
    }
    if (!find_registry_binary(binary, sizeof(binary))) {
        printf("# at-spi2-registryd not found; live checks will skip\n");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setenv("AT_SPI_BUS_ADDRESS", address, 1);
        execl(binary, binary, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        printf("# cannot start at-spi2-registryd; live checks will skip\n");
        return;
    }
    a11y_registry_pid = pid;
    atexit(stop_a11y_registry);

    DBusError error;
    dbus_error_init(&error);
    DBusConnection *bus = dbus_connection_open_private(address, &error);
    if (bus && !dbus_bus_register(bus, &error)) {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
        bus = NULL;
    }
    bool owned = false;
    if (bus) {
        for (int attempt = 0; attempt < 40 && !owned; ++attempt) {
            dbus_bus_name_has_owner(bus, "org.a11y.atspi.Registry", &error);
            if (dbus_error_is_set(&error)) {
                dbus_error_free(&error);
            } else {
                /* name_has_owner returns false without an error when absent;
                 * probe the registry's root object instead. */
                DBusMessage *ping = dbus_message_new_method_call(
                    "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root",
                    "org.freedesktop.DBus.Peer", "Ping");
                if (ping) {
                    DBusMessage *reply =
                        dbus_connection_send_with_reply_and_block(bus, ping, 200, &error);
                    if (reply) {
                        owned = true;
                        dbus_message_unref(reply);
                    }
                    dbus_message_unref(ping);
                }
                if (dbus_error_is_set(&error)) {
                    dbus_error_free(&error);
                }
            }
            if (!owned) {
                struct timespec pause = {0, 50 * 1000 * 1000};
                nanosleep(&pause, NULL);
            }
        }
    }
    dbus_error_free(&error);
    if (bus) {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
    }
    if (!owned) {
        printf("# private at-spi2 registry did not come up; live checks will skip\n");
    }
}

static int remove_entry(const char *path, const struct stat *info, int flag,
                        struct FTW *walk) {
    (void)info;
    (void)flag;
    (void)walk;
    return remove(path);
}

/* Returns -1 when isolation is unavailable (caller runs in-process), else the
 * child's exit status. */
static int reexec_private_bus(char *self) {
    char session[PATH_MAX];
    char registry[PATH_MAX];
    if (!find_executable("dbus-run-session", session, sizeof(session)) ||
        !find_registry_binary(registry, sizeof(registry)) ||
        !session_bus_reachable()) {
        return -1;
    }
    if (!self[0] || access(self, X_OK) != 0) {
        return -1;
    }
    char template[] = "/tmp/pt-a11y-private.XXXXXX";
    char *directory = mkdtemp(template);
    if (!directory) {
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setenv("PT_A11Y_PRIVATE_BUS", "1", 1);
        setenv("XDG_RUNTIME_DIR", directory, 1);
        unsetenv("AT_SPI_BUS_ADDRESS");
        execl(session, session, "--", self, (char *)NULL);
        _exit(127);
    }
    int status = 1;
    if (pid > 0) {
        int raw = 0;
        while (waitpid(pid, &raw, 0) < 0 && errno == EINTR) {
        }
        status = WIFEXITED(raw) ? WEXITSTATUS(raw) : 1;
    }
    nftw(directory, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    return status;
}

/* ---------- inline image virtual children ---------- */

#define image_placeholder "\xEF\xBF\xBC"

static pt_icon_image *make_image_icon(void) {
    size_t size = 0;
    uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) {
        pixels[i] = 0x80FF0000u | (uint32_t)i;
    }
    uint8_t *png = pt_png_encode_argb_premultiplied(pixels, 4, 4, &size);
    pt_check(png != NULL && size > 0);
    pt_icon_image *image = pt_icon_image_load_memory(png, size);
    free(png);
    return image;
}

static int a11y_child_count_of(pt_app *app, DBusConnection *client, const char *bus,
                               const char *path) {
    DBusMessage *reply = a11y_get_property(app, client, bus, path,
                                           "org.a11y.atspi.Accessible", "ChildCount");
    int32_t count = -1;
    bool ok = reply_int(reply, &count);
    if (reply) {
        dbus_message_unref(reply);
    }
    pt_check(ok);
    return ok ? (int)count : -1;
}

static bool a11y_char_count(pt_app *app, DBusConnection *client, const char *bus,
                            const char *path, int32_t *out) {
    DBusMessage *reply = a11y_get_property(app, client, bus, path,
                                           "org.a11y.atspi.Text", "CharacterCount");
    bool ok = reply_int(reply, out);
    if (reply) {
        dbus_message_unref(reply);
    }
    return ok;
}

static bool a11y_get_text(pt_app *app, DBusConnection *client, const char *bus,
                          const char *path, int32_t start, int32_t end, char *out,
                          size_t size) {
    DBusMessage *message = dbus_message_new_method_call(
        bus, path, "org.a11y.atspi.Text", "GetText");
    if (!message) {
        return false;
    }
    dbus_int32_t first = start;
    dbus_int32_t second = end;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &first, DBUS_TYPE_INT32, &second,
                             DBUS_TYPE_INVALID);
    DBusMessage *reply = a11y_call(app, client, message);
    bool ok = reply_string(reply, out, size);
    if (reply) {
        dbus_message_unref(reply);
    }
    return ok;
}

static bool path_ends_with(const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    return path_length >= suffix_length &&
           strcmp(path + path_length - suffix_length, suffix) == 0;
}

static void test_bridge_image_children(pt_app *app, pt_widget *image_label,
                                       pt_widget *multi_label, pt_widget *empty_label,
                                       pt_widget *plain_label, pt_widget *image_area) {
    DBusConnection *client = open_a11y_client();
    pt_check(client != NULL);
    if (!client) {
        return;
    }
    signal_watch watch = {0};
    dbus_connection_add_filter(client, watch_signals, &watch, NULL);
    DBusError match_error;
    dbus_error_init(&match_error);
    dbus_bus_add_match(client, "type='signal',interface='org.a11y.atspi.Event.Object'",
                       &match_error);
    dbus_connection_flush(client);
    pt_check(!dbus_error_is_set(&match_error));
    dbus_error_free(&match_error);

    a11y_ref roots[16];
    int root_count = a11y_children(app, client, "org.a11y.atspi.Registry",
                                   "/org/a11y/atspi/accessible/root", roots, 16);
    pt_check(root_count > 0);
    a11y_ref app_root = {0};
    for (int i = 0; i < root_count; ++i) {
        char name[256];
        int role = a11y_role(app, client, roots[i].bus, roots[i].path);
        bool named = a11y_name(app, client, roots[i].bus, roots[i].path, name,
                               sizeof(name));
        if (role == atspi_role_application && named &&
            strcmp(name, "pt.tests.a11y-bridge") == 0) {
            app_root = roots[i];
            break;
        }
    }
    pt_check(app_root.path[0] != '\0');
    if (app_root.path[0] == '\0') {
        dbus_connection_close(client);
        dbus_connection_unref(client);
        return;
    }

    a11y_ref windows[8];
    pt_check(a11y_children(app, client, app_root.bus, app_root.path, windows, 8) == 1);
    a11y_ref root_widget = {0};
    pt_check(a11y_child_ref(app, client, windows[0].bus, windows[0].path, 0,
                            &root_widget));
    pt_check(root_widget.path[0] != '\0');

    a11y_ref image_ref = {0};
    a11y_ref multi_ref = {0};
    a11y_ref empty_ref = {0};
    a11y_ref plain_ref = {0};
    a11y_ref area_ref = {0};
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_label, "Image label", &image_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_label, "Multi label", &multi_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_label, "Empty label", &empty_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_label, "Plain label", &plain_ref));
    pt_check(find_child(app, client, windows[0].bus, root_widget.path,
                        atspi_role_entry, "Image area", &area_ref));
    pt_check(image_ref.path[0] != '\0' && multi_ref.path[0] != '\0' &&
             empty_ref.path[0] != '\0' && plain_ref.path[0] != '\0' &&
             area_ref.path[0] != '\0');

    pt_icon_image *icon = make_image_icon();
    pt_check(icon != NULL);
    if (!icon) {
        dbus_connection_close(client);
        dbus_connection_unref(client);
        return;
    }
    int children_before = watch.children_changed;

    /* Attach the image runs. Every setter emits a structure-changed event. */
    pt_text_image single = {0};
    single.start = 1; /* the U+FFFC in "P<U+FFFC>Q" */
    single.image = icon;
    snprintf(single.alt, sizeof(single.alt), "%s", "photo");
    pt_check(pt_widget_set_text_images(image_label, &single, 1));

    pt_text_image first = {0};
    first.start = 0;
    first.image = icon;
    snprintf(first.alt, sizeof(first.alt), "%s", "one");
    pt_text_image second = {0};
    second.start = 4; /* "<U+FFFC>A<U+FFFC>B" */
    second.image = icon;
    snprintf(second.alt, sizeof(second.alt), "%s", "\xe7\x85\xa7\xe7\x89\x87");
    pt_text_image multi[2] = {first, second};
    pt_check(pt_widget_set_text_images(multi_label, multi, 2));

    pt_text_image empty = {0};
    empty.start = 0;
    empty.image = icon;
    pt_check(pt_widget_set_text_images(empty_label, &empty, 1));

    pt_text_image area = {0};
    area.start = 0;
    area.image = icon;
    snprintf(area.alt, sizeof(area.alt), "%s", "area image");
    pt_check(pt_widget_set_text_images(image_area, &area, 1));

    /* A query flushes the queued structure events to the client. */
    for (int i = 0; i < 100 && watch.children_changed < children_before + 4; ++i) {
        pt_platform_dispatch(app, 5);
        dbus_connection_read_write_dispatch(client, 10);
    }
    pt_check(a11y_child_count_of(app, client, image_ref.bus, image_ref.path) == 1);
    pt_check(watch.children_changed >= children_before + 4);

    /* ---- single image label ---- */
    a11y_ref child = {0};
    pt_check(a11y_child_ref(app, client, image_ref.bus, image_ref.path, 0, &child));
    pt_check(child.path[0] != '\0');
    pt_check(path_ends_with(child.path, "/i0"));
    pt_check(a11y_role(app, client, child.bus, child.path) == atspi_role_image);
    char name[256] = {0};
    pt_check(a11y_name(app, client, child.bus, child.path, name, sizeof(name)));
    pt_check(strcmp(name, "photo") == 0);

    DBusMessage *parent_reply = a11y_get_property(app, client, child.bus, child.path,
                                                  "org.a11y.atspi.Accessible", "Parent");
    a11y_ref parent = {0};
    pt_check(reply_ref(parent_reply, &parent));
    if (parent_reply) {
        dbus_message_unref(parent_reply);
    }
    pt_check(strcmp(parent.path, image_ref.path) == 0);

    int lx = 0;
    int ly = 0;
    int lw = 0;
    int lh = 0;
    pt_check(a11y_extents(app, client, child.bus, child.path, &lx, &ly, &lw, &lh));
    pt_check(lw > 0 && lh > 0);
    a11y_ref label_hit = {0};
    pt_check(a11y_component_at_point(app, client, image_ref.bus, image_ref.path,
                                     lx + lw / 2, ly + lh / 2, &label_hit));
    pt_check(path_ends_with(label_hit.path, "/i0"));

    DBusMessage *index_reply = a11y_method(app, client, child.bus, child.path,
                                           "org.a11y.atspi.Accessible", "GetIndexInParent");
    int32_t index = -1;
    pt_check(reply_int(index_reply, &index));
    if (index_reply) {
        dbus_message_unref(index_reply);
    }
    pt_check(index == 0);

    a11y_ref none = {0};
    pt_check(a11y_child_ref(app, client, image_ref.bus, image_ref.path, 1, &none));
    pt_check(strcmp(none.path, "/org/a11y/atspi/null") == 0);
    pt_check(a11y_child_ref(app, client, image_ref.bus, image_ref.path, -1, &none));
    pt_check(strcmp(none.path, "/org/a11y/atspi/null") == 0);

    a11y_ref kids[4];
    pt_check(a11y_children(app, client, image_ref.bus, image_ref.path, kids, 4) == 1);
    pt_check(strcmp(kids[0].path, child.path) == 0);

    /* Text interface keeps the raw placeholder and the character offsets. */
    int32_t chars = 0;
    pt_check(a11y_char_count(app, client, image_ref.bus, image_ref.path, &chars));
    pt_check(chars == 3);
    char text[64] = {0};
    pt_check(a11y_get_text(app, client, image_ref.bus, image_ref.path, 0, 3, text,
                           sizeof(text)));
    pt_check(strcmp(text, "P" image_placeholder "Q") == 0);

    /* ---- multiple images ---- */
    pt_check(a11y_child_count_of(app, client, multi_ref.bus, multi_ref.path) == 2);
    a11y_ref m0 = {0};
    a11y_ref m1 = {0};
    pt_check(a11y_child_ref(app, client, multi_ref.bus, multi_ref.path, 0, &m0));
    pt_check(a11y_child_ref(app, client, multi_ref.bus, multi_ref.path, 1, &m1));
    pt_check(path_ends_with(m0.path, "/i0"));
    pt_check(path_ends_with(m1.path, "/i1"));
    pt_check(a11y_role(app, client, m1.bus, m1.path) == atspi_role_image);
    pt_check(a11y_name(app, client, m0.bus, m0.path, name, sizeof(name)));
    pt_check(strcmp(name, "one") == 0);
    pt_check(a11y_name(app, client, m1.bus, m1.path, name, sizeof(name)));
    pt_check(strcmp(name, "\xe7\x85\xa7\xe7\x89\x87") == 0);
    DBusMessage *index1_reply = a11y_method(app, client, m1.bus, m1.path,
                                            "org.a11y.atspi.Accessible", "GetIndexInParent");
    int32_t index1 = -1;
    pt_check(reply_int(index1_reply, &index1));
    if (index1_reply) {
        dbus_message_unref(index1_reply);
    }
    pt_check(index1 == 1);
    a11y_ref m2 = {0};
    pt_check(a11y_child_ref(app, client, multi_ref.bus, multi_ref.path, 2, &m2));
    pt_check(strcmp(m2.path, "/org/a11y/atspi/null") == 0);

    /* ---- empty alt is unnamed ---- */
    pt_check(a11y_child_count_of(app, client, empty_ref.bus, empty_ref.path) == 1);
    a11y_ref e0 = {0};
    pt_check(a11y_child_ref(app, client, empty_ref.bus, empty_ref.path, 0, &e0));
    pt_check(a11y_role(app, client, e0.bus, e0.path) == atspi_role_image);
    pt_check(a11y_name(app, client, e0.bus, e0.path, name, sizeof(name)));
    pt_check(strcmp(name, "") == 0);

    /* ---- text area image ---- */
    pt_check(a11y_child_count_of(app, client, area_ref.bus, area_ref.path) == 1);
    a11y_ref a0 = {0};
    pt_check(a11y_child_ref(app, client, area_ref.bus, area_ref.path, 0, &a0));
    pt_check(a11y_role(app, client, a0.bus, a0.path) == atspi_role_image);
    pt_check(a11y_name(app, client, a0.bus, a0.path, name, sizeof(name)));
    pt_check(strcmp(name, "area image") == 0);

    /* ---- Text copy replaces a contained U+FFFC with the alt text ---- */
    DBusMessage *copy_reply = a11y_method_int2(
        app, client, area_ref.bus, area_ref.path, "org.a11y.atspi.EditableText",
        "CopyText", 0, 1);
    bool copied = false;
    pt_check(reply_bool(copy_reply, &copied) && copied);
    if (copy_reply) {
        dbus_message_unref(copy_reply);
    }
    char *clipboard = pt_platform_clipboard_get(pt_widget_window(image_area));
    pt_check(clipboard != NULL && strcmp(clipboard, "area image") == 0);
    free(clipboard);

    /* A range that does not contain the object copies ordinary bytes. */
    copy_reply = a11y_method_int2(app, client, area_ref.bus, area_ref.path,
                                  "org.a11y.atspi.EditableText", "CopyText", 1, 5);
    copied = false;
    pt_check(reply_bool(copy_reply, &copied) && copied);
    if (copy_reply) {
        dbus_message_unref(copy_reply);
    }
    clipboard = pt_platform_clipboard_get(pt_widget_window(image_area));
    pt_check(clipboard != NULL && strcmp(clipboard, "Area") == 0);
    free(clipboard);

    /* The full range substitutes once and keeps the surrounding text. */
    copy_reply = a11y_method_int2(app, client, area_ref.bus, area_ref.path,
                                  "org.a11y.atspi.EditableText", "CopyText", 0, 5);
    copied = false;
    pt_check(reply_bool(copy_reply, &copied) && copied);
    if (copy_reply) {
        dbus_message_unref(copy_reply);
    }
    clipboard = pt_platform_clipboard_get(pt_widget_window(image_area));
    pt_check(clipboard != NULL && strcmp(clipboard, "area imageArea") == 0);
    free(clipboard);

    /* GetText still exposes the raw placeholder and its character offsets. */
    char area_text[64] = {0};
    pt_check(a11y_get_text(app, client, area_ref.bus, area_ref.path, 0, 5, area_text,
                           sizeof(area_text)));
    pt_check(strcmp(area_text, image_placeholder "Area") == 0);

    /* An empty alt keeps U+FFFC in the copied text. */
    pt_text_image empty_area = {0};
    empty_area.start = 0;
    empty_area.image = icon;
    pt_check(pt_widget_set_text_images(image_area, &empty_area, 1));
    pt_check(a11y_child_count_of(app, client, area_ref.bus, area_ref.path) == 1);
    a11y_ref ea = {0};
    pt_check(a11y_child_ref(app, client, area_ref.bus, area_ref.path, 0, &ea));
    pt_check(a11y_name(app, client, ea.bus, ea.path, name, sizeof(name)));
    pt_check(strcmp(name, "") == 0);
    copy_reply = a11y_method_int2(app, client, area_ref.bus, area_ref.path,
                                  "org.a11y.atspi.EditableText", "CopyText", 0, 1);
    copied = false;
    pt_check(reply_bool(copy_reply, &copied) && copied);
    if (copy_reply) {
        dbus_message_unref(copy_reply);
    }
    clipboard = pt_platform_clipboard_get(pt_widget_window(image_area));
    pt_check(clipboard != NULL && strcmp(clipboard, image_placeholder) == 0);
    free(clipboard);

    /* ---- a widget without images keeps an unchanged tree ---- */
    pt_check(pt_widget_get_text_image_count(plain_label) == 0);
    pt_check(a11y_child_count_of(app, client, plain_ref.bus, plain_ref.path) == 0);
    pt_check(a11y_children(app, client, plain_ref.bus, plain_ref.path, kids, 4) == 0);
    a11y_ref plain_none = {0};
    pt_check(a11y_child_ref(app, client, plain_ref.bus, plain_ref.path, 0, &plain_none));
    pt_check(strcmp(plain_none.path, "/org/a11y/atspi/null") == 0);

    /* ---- structure refresh on replace, clear and set_text ---- */
    pt_text_image replaced = {0};
    replaced.start = 1;
    replaced.image = icon;
    snprintf(replaced.alt, sizeof(replaced.alt), "%s", "replaced");
    pt_check(pt_widget_set_text_images(image_label, &replaced, 1));
    pt_check(a11y_child_count_of(app, client, image_ref.bus, image_ref.path) == 1);
    a11y_ref r0 = {0};
    pt_check(a11y_child_ref(app, client, image_ref.bus, image_ref.path, 0, &r0));
    pt_check(a11y_name(app, client, r0.bus, r0.path, name, sizeof(name)));
    pt_check(strcmp(name, "replaced") == 0);

    pt_widget_clear_text_images(image_label);
    pt_check(a11y_child_count_of(app, client, image_ref.bus, image_ref.path) == 0);
    pt_check(a11y_children(app, client, image_ref.bus, image_ref.path, kids, 4) == 0);

    pt_widget_set_text(multi_label, "plain");
    pt_check(a11y_child_count_of(app, client, multi_ref.bus, multi_ref.path) == 0);

    /* ---- CutText/DeleteText use the regular editing delete path ---- */
    pt_widget_set_text(image_area, "A" image_placeholder "B" image_placeholder "C");
    pt_text_image del_first = {0};
    del_first.start = 1;
    del_first.image = icon;
    snprintf(del_first.alt, sizeof(del_first.alt), "%s", "first");
    pt_text_image del_second = {0};
    del_second.start = 5;
    del_second.image = icon;
    snprintf(del_second.alt, sizeof(del_second.alt), "%s", "second");
    pt_text_image del_pair[2] = {del_first, del_second};
    pt_check(pt_widget_set_text_images(image_area, del_pair, 2));
    pt_check(pt_widget_get_text_image_count(image_area) == 2);

    /* ---- virtual image Component geometry and hit testing ---- */
    a11y_ref geo0 = {0};
    a11y_ref geo1 = {0};
    pt_check(a11y_child_ref(app, client, area_ref.bus, area_ref.path, 0, &geo0));
    pt_check(a11y_child_ref(app, client, area_ref.bus, area_ref.path, 1, &geo1));
    pt_check(path_ends_with(geo0.path, "/i0"));
    pt_check(path_ends_with(geo1.path, "/i1"));
    int ax = 0;
    int ay = 0;
    int aw = 0;
    int ah = 0;
    pt_check(a11y_extents(app, client, area_ref.bus, area_ref.path, &ax, &ay, &aw, &ah));
    int x0 = 0;
    int y0 = 0;
    int w0 = 0;
    int h0 = 0;
    int x1 = 0;
    int y1 = 0;
    int w1 = 0;
    int h1 = 0;
    pt_check(a11y_extents(app, client, geo0.bus, geo0.path, &x0, &y0, &w0, &h0));
    pt_check(a11y_extents(app, client, geo1.bus, geo1.path, &x1, &y1, &w1, &h1));
    pt_check(aw > 0 && ah > 0);
    pt_check(w0 > 0 && h0 > 0 && w1 > 0 && h1 > 0);
    pt_check(x0 >= ax && y0 >= ay && x0 + w0 <= ax + aw && y0 + h0 <= ay + ah);
    pt_check(x1 >= ax && y1 >= ay && x1 + w1 <= ax + aw && y1 + h1 <= ay + ah);
    pt_check(x1 > x0 && y1 == y0);
    a11y_ref point_hit = {0};
    pt_check(a11y_component_at_point(app, client, area_ref.bus, area_ref.path,
                                     x1 + w1 / 2, y1 + h1 / 2, &point_hit));
    pt_check(path_ends_with(point_hit.path, "/i1"));
    pt_check(a11y_component_at_point(app, client, area_ref.bus, area_ref.path,
                                     x0 + w0 / 2, y0 + h0 / 2, &point_hit));
    pt_check(path_ends_with(point_hit.path, "/i0"));

    DBusMessage *cut_reply = a11y_method_int2(
        app, client, area_ref.bus, area_ref.path, "org.a11y.atspi.EditableText",
        "CutText", 1, 2);
    bool cut = false;
    pt_check(reply_bool(cut_reply, &cut) && cut);
    if (cut_reply) {
        dbus_message_unref(cut_reply);
    }
    pt_check(strcmp(pt_widget_get_text(image_area), "AB" image_placeholder "C") == 0);
    pt_check(pt_widget_get_text_image_count(image_area) == 1);
    pt_text_image kept = {0};
    pt_check(pt_widget_get_text_image(image_area, 0, &kept));
    pt_check(kept.start == 2);
    pt_check(strcmp(kept.alt, "second") == 0);
    clipboard = pt_platform_clipboard_get(pt_widget_window(image_area));
    pt_check(clipboard != NULL && strcmp(clipboard, "first") == 0);
    free(clipboard);

    /* Undo restores the text and both descriptors at their old offsets. */
    pt_check(pt_widget_text_undo(image_area));
    pt_check(strcmp(pt_widget_get_text(image_area),
                    "A" image_placeholder "B" image_placeholder "C") == 0);
    pt_check(pt_widget_get_text_image_count(image_area) == 2);
    pt_text_image undone = {0};
    pt_check(pt_widget_get_text_image(image_area, 0, &undone));
    pt_check(undone.start == 1 && strcmp(undone.alt, "first") == 0);
    pt_check(pt_widget_get_text_image(image_area, 1, &undone));
    pt_check(undone.start == 5 && strcmp(undone.alt, "second") == 0);

    /* DeleteText removes the leading text plus the first object only. */
    DBusMessage *delete_reply = a11y_method_int2(
        app, client, area_ref.bus, area_ref.path, "org.a11y.atspi.EditableText",
        "DeleteText", 0, 2);
    bool deleted = false;
    pt_check(reply_bool(delete_reply, &deleted) && deleted);
    if (delete_reply) {
        dbus_message_unref(delete_reply);
    }
    pt_check(strcmp(pt_widget_get_text(image_area), "B" image_placeholder "C") == 0);
    pt_check(pt_widget_get_text_image_count(image_area) == 1);
    pt_check(pt_widget_get_text_image(image_area, 0, &kept));
    pt_check(kept.start == 1 && strcmp(kept.alt, "second") == 0);
    pt_check(pt_widget_text_undo(image_area));
    pt_check(strcmp(pt_widget_get_text(image_area),
                    "A" image_placeholder "B" image_placeholder "C") == 0);
    pt_check(pt_widget_get_text_image_count(image_area) == 2);

    pt_icon_image_destroy(icon);
    dbus_connection_close(client);
    dbus_connection_unref(client);
}
#endif /* PT_HAVE_DBUS */

int main(int argc, char **argv) {
    (void)argc;
#ifdef PT_HAVE_DBUS
    if (!getenv("PT_A11Y_PRIVATE_BUS")) {
        char self[PATH_MAX];
        ssize_t resolved = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (resolved > 0) {
            self[resolved] = '\0';
        } else {
            snprintf(self, sizeof(self), "%s", argv[0] ? argv[0] : "");
        }
        int isolated = reexec_private_bus(self);
        if (isolated >= 0) {
            return isolated;
        }
    } else {
        start_private_a11y_registry();
    }
#endif
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.a11y-bridge");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("a11y_bridge");
    }
    pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_disabled);

    pt_window *window = pt_window_create(app, "Bridge window", 520, 480);
    pt_widget *root = pt_window_root(window);
    pt_label_create(root, "Form");
    pt_widget *save = pt_button_create(root, "Save");
    pt_widget *search = pt_entry_create(root, "Search");
    pt_widget *password_entry = pt_entry_create(root, "Password");
    pt_widget *notes = pt_text_area_create(root, "Notes");
    pt_widget *hebrew = pt_label_create(root, "");
    pt_widget *arabic = pt_label_create(root, "");
    pt_widget *rtl_field = pt_entry_create(root, "RTL field");
    pt_widget_set_text(hebrew, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d");
    pt_widget_set_text(arabic, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7");
    pt_widget_set_text(rtl_field, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d");
    pt_widget_set_ui_direction(hebrew, pt_ui_direction_rtl);
    pt_widget_set_ui_direction(arabic, pt_ui_direction_rtl);
    pt_widget_set_ui_direction(rtl_field, pt_ui_direction_rtl);
    pt_widget_set_a11y_name(rtl_field, "RTL field");
    pt_widget *image_label = pt_label_create(root, "P\xEF\xBF\xBCQ");
    pt_widget_set_a11y_name(image_label, "Image label");
    pt_widget *multi_label = pt_label_create(root, "\xEF\xBF\xBC" "A" "\xEF\xBF\xBC" "B");
    pt_widget_set_a11y_name(multi_label, "Multi label");
    pt_widget *empty_label = pt_label_create(root, "\xEF\xBF\xBCX");
    pt_widget_set_a11y_name(empty_label, "Empty label");
    pt_widget *plain_label = pt_label_create(root, "No image");
    pt_widget_set_a11y_name(plain_label, "Plain label");
    pt_widget *image_area = pt_text_area_create(root, "Area");
    pt_widget_set_text(image_area, "\xEF\xBF\xBC" "Area");
    pt_widget_set_a11y_name(image_area, "Image area");
    pt_widget *volume = pt_slider_create(root);
    pt_widget_set_text(search, "hello");
    pt_widget_set_text(password_entry, "secret");
    pt_entry_set_password(password_entry, true);
    pt_widget_set_ui_direction(password_entry, pt_ui_direction_rtl);
    pt_widget_set_a11y_name(password_entry, "Password");
    pt_widget_set_text(notes, "caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c");
    pt_widget_set_a11y_name(notes, "Notes");
    pt_widget_set_height(notes, 64.0f);
    pt_slider_set_value(volume, 0.25f);
    pt_button_set_on_click(save, on_button_click, NULL);

    pt_widget *table = pt_table_create(root);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Modified");
    pt_table_set_column_title(table, 2, "Size");
    pt_table_set_on_bind_cell(table, bind_cell, NULL);
    pt_table_set_row_count(table, 5);
    uint32_t sort_columns[2] = {2, 1};
    pt_sort_direction sort_directions[2] = {pt_sort_ascending, pt_sort_descending};
    pt_table_set_sort_columns(table, 2, sort_columns, sort_directions);
    pt_table_set_selected(table, 0);
    pt_widget_set_height(table, 120.0f);

    pt_widget *tree = pt_tree_create(root);
    pt_tree_node *tree_root_node = pt_tree_add_node(tree, NULL, "Root");
    pt_tree_node *tree_child_node = pt_tree_add_node(tree, tree_root_node, "Child");
    pt_check(tree_root_node != NULL && tree_child_node != NULL);
    pt_tree_node_set_expanded(tree_root_node, true);
    pt_tree_set_selected(tree, tree_child_node);
    pt_widget_set_height(tree, 96.0f);

    pt_widget *tabs = pt_tabs_create(root);
    pt_widget *tab_one = pt_tabs_add_page(tabs, "One");
    pt_widget *tab_two = pt_tabs_add_page(tabs, "Two");
    pt_check(tab_one != NULL && tab_two != NULL);
    pt_tabs_select(tabs, 1, false);

    pt_widget *sidebar = pt_sidebar_create(root);
    pt_widget *side_one = pt_sidebar_add_item(sidebar, pt_icon_folder, "Inbox");
    pt_widget *side_two = pt_sidebar_add_item(sidebar, pt_icon_folder, "Sent");
    pt_check(side_one != NULL && side_two != NULL);
    pt_sidebar_select(sidebar, 1, false);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(window->width, window->height, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_canvas_destroy(canvas);

    setenv("AT_SPI_BUS_ADDRESS", "unix:path=/nonexistent/pt-a11y-test", 1);
    pt_check(!pt_app_enable_a11y(app));
    pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_unavailable);
    pt_app_disable_a11y(app);
    pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_disabled);
    unsetenv("AT_SPI_BUS_ADDRESS");

    bool live = pt_app_enable_a11y(app);
#ifdef PT_HAVE_DBUS
    if (live) {
        pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_active);
        test_bridge_live(app, search, password_entry, volume, save, table, tree,
                         tree_root_node, tree_child_node, tabs, sidebar, notes);
        test_bridge_direction(app, rtl_field);
        test_bridge_image_children(app, image_label, multi_label, empty_label, plain_label,
                                   image_area);
        pt_app_disable_a11y(app);
        pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_disabled);
        pt_check(pt_app_enable_a11y(app));
        pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_active);
    }
#else
    (void)live;
#endif
    if (!live) {
        pt_check(pt_app_get_a11y_status(app) == pt_a11y_status_unavailable);
        printf("# accessibility bus unavailable; live checks skipped\n");
    }

    pt_app_destroy(app);
    return pt_test_finish("a11y_bridge");
}

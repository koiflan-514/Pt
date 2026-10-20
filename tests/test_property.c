/*
 * Deterministic property and fuzz checks: UTF-8 editing, URI lists,
 * data-view index clamping and damage arithmetic.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct rng {
    uint64_t state;
} rng;

static uint32_t next_u32(rng *random) {
    uint64_t x = random->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    random->state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

static uint32_t below(rng *random, uint32_t limit) {
    return limit ? next_u32(random) % limit : 0;
}

static size_t encode_utf8(uint32_t codepoint, char *out) {
    if (codepoint < 0x80u) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800u) {
        out[0] = (char)(0xC0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2;
    }
    if (codepoint < 0x10000u) {
        out[0] = (char)(0xE0u | (codepoint >> 12));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (codepoint & 0x3Fu));
    return 4;
}

static bool utf8_valid(const char *text) {
    const unsigned char *bytes = (const unsigned char *)text;
    while (*bytes) {
        size_t need;
        uint32_t codepoint;
        uint32_t minimum;
        if (bytes[0] < 0x80u) {
            ++bytes;
            continue;
        }
        if ((bytes[0] & 0xE0u) == 0xC0u) {
            need = 1;
            codepoint = bytes[0] & 0x1Fu;
            minimum = 0x80u;
        } else if ((bytes[0] & 0xF0u) == 0xE0u) {
            need = 2;
            codepoint = bytes[0] & 0x0Fu;
            minimum = 0x800u;
        } else if ((bytes[0] & 0xF8u) == 0xF0u) {
            need = 3;
            codepoint = bytes[0] & 0x07u;
            minimum = 0x10000u;
        } else {
            return false;
        }
        for (size_t i = 1; i <= need; ++i) {
            if ((bytes[i] & 0xC0u) != 0x80u) {
                return false;
            }
            codepoint = (codepoint << 6) | (bytes[i] & 0x3Fu);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            return false;
        }
        bytes += need + 1;
    }
    return true;
}

static bool utf8_boundary(const char *text, size_t position) {
    size_t length = strlen(text);
    if (position > length) {
        return false;
    }
    if (position == 0 || position == length) {
        return true;
    }
    return ((unsigned char)text[position] & 0xC0u) != 0x80u;
}

static const uint32_t utf8_pool[] = {
    0x41u, 0x62u, 0x30u, 0x20u, 0xE9u, 0x3A9u, 0x4E2Du,
    0x20ACu, 0x1F600u, 0x301u, 0x200Du,
};

static size_t random_utf8(rng *random, char *out, size_t capacity, size_t max_codepoints) {
    size_t used = 0;
    uint32_t count = below(random, (uint32_t)max_codepoints + 1);
    for (uint32_t i = 0; i < count && used + 4 < capacity; ++i) {
        used += encode_utf8(utf8_pool[below(random, (uint32_t)(sizeof(utf8_pool) /
                                                                sizeof(utf8_pool[0])))],
                            out + used);
    }
    out[used] = '\0';
    return used;
}

static void check_text_invariants(pt_widget *widget) {
    const char *text = NULL;
    uint32_t caret = 0;
    uint32_t start = 0;
    uint32_t end = 0;
    pt_check(pt_widget_get_a11y_text(widget, &text, &caret, &start, &end));
    pt_check(text != NULL);
    if (!text) {
        return;
    }
    size_t length = strlen(text);
    pt_check(utf8_valid(text));
    pt_check(strlen(pt_widget_get_text(widget)) == length);
    pt_check(caret <= length);
    pt_check(start <= end && end <= length);
    pt_check(utf8_boundary(text, caret));
    pt_check(utf8_boundary(text, start));
    pt_check(utf8_boundary(text, end));
    pt_check(widget->cursor <= length);
    pt_check(widget->selection_anchor <= length);
}

static void edit_entry(pt_widget *entry, rng *random) {
    char buffer[64];
    for (int step = 0; step < 200; ++step) {
        if (step > 0 && step % 25 == 0) {
            random_utf8(random, buffer, sizeof(buffer), 12);
            pt_widget_set_text(entry, buffer);
        }
        switch (below(random, 8)) {
        case 0:
        case 1:
        case 2:
            random_utf8(random, buffer, sizeof(buffer), 8);
            (void)pt_widget_text_input(entry, buffer);
            break;
        case 3: {
            uint32_t presses = below(random, 4);
            for (uint32_t i = 0; i < presses; ++i) {
                (void)pt_widget_key(entry, XKB_KEY_BackSpace, 0, NULL);
            }
            break;
        }
        case 4: {
            uint32_t presses = below(random, 6);
            uint32_t key = below(random, 2) ? XKB_KEY_Left : XKB_KEY_Right;
            for (uint32_t i = 0; i < presses; ++i) {
                (void)pt_widget_key(entry, key, 0, NULL);
            }
            break;
        }
        case 5:
            pt_entry_select_all(entry);
            if (below(random, 2)) {
                (void)pt_widget_key(entry, XKB_KEY_Delete, 0, NULL);
            } else {
                random_utf8(random, buffer, sizeof(buffer), 4);
                (void)pt_widget_text_input(entry, buffer);
            }
            break;
        case 6:
            (void)pt_entry_undo(entry);
            break;
        default:
            (void)pt_entry_redo(entry);
            break;
        }
        check_text_invariants(entry);
    }
}

static void edit_text_area(pt_widget *area, rng *random) {
    char buffer[96];
    for (int step = 0; step < 120; ++step) {
        if (step > 0 && step % 20 == 0) {
            random_utf8(random, buffer, sizeof(buffer), 10);
            pt_widget_set_text(area, buffer);
        }
        switch (below(random, 7)) {
        case 0:
        case 1:
        case 2:
            random_utf8(random, buffer, sizeof(buffer), 6);
            (void)pt_widget_text_input(area, buffer);
            break;
        case 3:
            (void)pt_widget_key(area, XKB_KEY_Return, 0, "\n");
            break;
        case 4: {
            uint32_t presses = below(random, 4);
            for (uint32_t i = 0; i < presses; ++i) {
                (void)pt_widget_key(area, XKB_KEY_BackSpace, 0, NULL);
            }
            break;
        }
        case 5:
            (void)pt_text_area_undo(area);
            break;
        default:
            (void)pt_text_area_redo(area);
            break;
        }
        check_text_invariants(area);
    }
}

static void test_utf8_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "utf8", 640, 420);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget *entry = pt_entry_create(root, "type");
    pt_widget *area = pt_text_area_create(root, "notes");
    pt_check(entry != NULL);
    pt_check(area != NULL);

    rng random = {0x9E3779B97F4A7C15ull};
    edit_entry(entry, &random);
    edit_text_area(area, &random);

    pt_widget_set_text(entry, "start");
    char snapshots[10][128];
    size_t snapshot_count = 0;
    snprintf(snapshots[snapshot_count++], sizeof(snapshots[0]), "%s",
             pt_widget_get_text(entry));
    const char *pieces[] = {"H\xC3\xA9", "\xF0\x9F\x98\x80", "\xE4\xB8\xAD",
                            "\xC3\x9F", "\xE2\x82\xAC", "ok", "z"};
    for (size_t i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
        (void)pt_widget_text_input(entry, pieces[i]);
        snprintf(snapshots[snapshot_count++], sizeof(snapshots[0]), "%s",
                 pt_widget_get_text(entry));
    }
    for (size_t i = snapshot_count - 1; i > 0; --i) {
        pt_check(pt_entry_undo(entry));
        pt_check(strcmp(pt_widget_get_text(entry), snapshots[i - 1]) == 0);
        check_text_invariants(entry);
    }
    for (size_t i = 1; i < snapshot_count; ++i) {
        pt_check(pt_entry_redo(entry));
        pt_check(strcmp(pt_widget_get_text(entry), snapshots[i]) == 0);
        check_text_invariants(entry);
    }

    pt_window_free(window);
}

static void test_utf8_helpers(void) {
    const char *text = "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80";
    size_t length = strlen(text);
    size_t position = 0;
    uint32_t count = 0;
    while (position < length) {
        size_t next = 0;
        (void)pt_utf8_decode(text, length, position, &next);
        pt_check(next > position && next <= length);
        position = next;
        ++count;
    }
    pt_check(position == length);
    pt_check(count == 4);
    pt_check(pt_utf8_count(text, length) == 4);
    pt_check(utf8_boundary(text, 0));
    pt_check(utf8_boundary(text, length));
    pt_check(utf8_boundary(text, 1));
    pt_check(utf8_boundary(text, 3));
    pt_check(!utf8_boundary(text, 2));
}

static size_t append(char *buffer, size_t used, size_t capacity, const char *text) {
    size_t length = strlen(text);
    if (used + length >= capacity) {
        return used;
    }
    memcpy(buffer + used, text, length);
    return used + length;
}

static void test_uri_list(void) {
    const char *sample = "# generated by a file manager\r\n"
                         "\r\n"
                         "file:///home/user/a.txt\r\n"
                         "file:///home/user/b%20c.txt\n"
                         "# trailing comment\n"
                         "file:///tmp/d.txt";
    pt_check(pt_uri_list_count(sample) == 3);
    char *uri = pt_uri_list_get(sample, 0);
    pt_check(uri && strcmp(uri, "file:///home/user/a.txt") == 0);
    free(uri);
    uri = pt_uri_list_get(sample, 1);
    pt_check(uri && strcmp(uri, "file:///home/user/b%20c.txt") == 0);
    free(uri);
    uri = pt_uri_list_get(sample, 2);
    pt_check(uri && strcmp(uri, "file:///tmp/d.txt") == 0);
    free(uri);
    pt_check(pt_uri_list_get(sample, 3) == NULL);
    pt_check(pt_uri_list_get(sample, UINT32_MAX) == NULL);
    pt_check(pt_uri_list_count("") == 0);
    pt_check(pt_uri_list_count(NULL) == 0);
    pt_check(pt_uri_list_get(NULL, 0) == NULL);

    rng random = {0xD1B54A32D192ED03ull};
    for (int run = 0; run < 120; ++run) {
        char payload[4096];
        char expected[64][96];
        size_t used = 0;
        uint32_t count = 0;
        payload[0] = '\0';
        uint32_t lines = below(&random, 20);
        for (uint32_t i = 0; i < lines; ++i) {
            if (below(&random, 5) == 0) {
                used = append(payload, used, sizeof(payload) - 1,
                              below(&random, 2) ? "\n" : "\r\n");
                continue;
            }
            char line[96];
            size_t at = 0;
            if (below(&random, 4) == 0) {
                line[at++] = '#';
            }
            static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789_-./%:";
            uint32_t length = below(&random, 30);
            for (uint32_t j = 0; j < length; ++j) {
                line[at++] = alphabet[below(&random, (uint32_t)strlen(alphabet))];
            }
            line[at] = '\0';
            used = append(payload, used, sizeof(payload) - 1, line);
            used = append(payload, used, sizeof(payload) - 1,
                          below(&random, 2) ? "\n" : "\r\n");
            if (line[0] != '#' && at > 0 && count < 64) {
                snprintf(expected[count], sizeof(expected[count]), "%s", line);
                ++count;
            }
        }
        payload[used] = '\0';
        pt_check(pt_uri_list_count(payload) == count);
        for (uint32_t i = 0; i < count; ++i) {
            char *item = pt_uri_list_get(payload, i);
            pt_check(item && strcmp(item, expected[i]) == 0);
            free(item);
        }
        pt_check(pt_uri_list_get(payload, count) == NULL);
    }
}

static uint64_t property_row_key(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    (void)data;
    return (uint64_t)row * 7u + 11u;
}

static void check_table_invariants(pt_widget *table) {
    uint32_t rows = pt_table_get_row_count(table);
    uint32_t selected = pt_table_get_selected(table);
    pt_check(selected == pt_list_index_none || selected < rows);
    uint64_t key = pt_table_get_selected_key(table);
    pt_check(key == pt_row_key_none || selected != pt_list_index_none);

    uint32_t columns = pt_table_get_column_count(table);
    for (uint32_t column = 0; column < columns; ++column) {
        pt_check(pt_table_get_column_width(table, column) >= 48.0f);
    }
    pt_check(pt_table_get_column_width(table, columns) == 0.0f);
    pt_check(!pt_table_get_column_hidden(table, columns));

    uint32_t sort_columns[8];
    pt_sort_direction sort_directions[8];
    uint32_t sort_count = pt_table_get_sort_columns(table, sort_columns, sort_directions);
    pt_check(sort_count <= 4);
    for (uint32_t i = 0; i < sort_count; ++i) {
        pt_check(sort_columns[i] < columns);
        pt_check(sort_directions[i] == pt_sort_ascending ||
                 sort_directions[i] == pt_sort_descending);
        for (uint32_t j = i + 1; j < sort_count; ++j) {
            pt_check(sort_columns[i] != sort_columns[j]);
        }
    }
    uint32_t primary_column = 0;
    pt_sort_direction primary = pt_table_get_sort(table, &primary_column);
    if (sort_count == 0) {
        pt_check(primary == pt_sort_none);
        pt_check(primary_column == pt_table_column_none);
    } else {
        pt_check(primary == sort_directions[0]);
        pt_check(primary_column == sort_columns[0]);
    }
    if (rows > 0 && selected != pt_list_index_none) {
        pt_check(key == property_row_key(table, selected, NULL));
    }
}

static void test_table_indices(pt_app *app) {
    pt_window *window = pt_window_create(app, "table", 800, 500);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *table = pt_table_create(pt_window_root(window));
    pt_check(table != NULL);
    if (!table) {
        pt_window_free(window);
        return;
    }
    pt_table_set_row_key_callback(table, property_row_key, NULL);

    rng random = {0xA0761D6478BD642Full};
    for (int step = 0; step < 220; ++step) {
        switch (below(&random, 11)) {
        case 0: {
            uint32_t count = below(&random, 1500);
            pt_table_set_row_count(table, count);
            break;
        }
        case 1: {
            uint32_t columns = 1 + below(&random, 6);
            pt_table_set_column_count(table, columns);
            break;
        }
        case 2: {
            uint32_t columns = pt_table_get_column_count(table);
            uint32_t column = below(&random, columns + 2);
            pt_table_set_column_width(table, column, (float)below(&random, 6000) - 1000.0f);
            break;
        }
        case 3: {
            uint32_t columns = pt_table_get_column_count(table);
            uint32_t column = below(&random, columns + 2);
            bool hidden = below(&random, 2) != 0;
            pt_table_set_column_hidden(table, column, hidden);
            if (column < columns) {
                pt_check(pt_table_get_column_hidden(table, column) == hidden);
            }
            break;
        }
        case 4: {
            uint32_t rows = pt_table_get_row_count(table);
            uint32_t row = below(&random, 3) == 0 ? pt_list_index_none
                                                 : below(&random, rows + 20);
            pt_table_set_selected(table, row);
            uint32_t expected = row < rows ? row : pt_list_index_none;
            pt_check(pt_table_get_selected(table) == expected);
            break;
        }
        case 5: {
            uint64_t row = below(&random, pt_table_get_row_count(table) + 20);
            uint64_t key = (uint64_t)row * 7u + 11u;
            bool found = pt_table_set_selected_key(table, key);
            if ((uint64_t)row < pt_table_get_row_count(table)) {
                pt_check(found);
                pt_check(pt_table_get_selected_key(table) == key);
                pt_check(pt_table_get_selected(table) == (uint32_t)row);
            } else {
                pt_check(!found);
            }
            break;
        }
        case 6: {
            uint32_t columns = pt_table_get_column_count(table);
            uint32_t column = below(&random, columns + 2);
            pt_sort_direction direction =
                below(&random, 3) == 0 ? pt_sort_none : (pt_sort_direction)below(&random, 3);
            pt_table_set_sort(table, column, direction);
            break;
        }
        case 7: {
            uint32_t columns = pt_table_get_column_count(table);
            uint32_t sort_columns[6];
            pt_sort_direction sort_directions[6];
            uint32_t count = below(&random, 7);
            for (uint32_t i = 0; i < count; ++i) {
                sort_columns[i] = below(&random, columns + 2);
                sort_directions[i] = (pt_sort_direction)below(&random, 3);
            }
            pt_table_set_sort_columns(table, count, sort_columns, sort_directions);
            break;
        }
        case 8: {
            uint32_t first = below(&random, 2000);
            uint32_t count = below(&random, 25);
            pt_table_refresh_rows(table, first, count);
            break;
        }
        case 9:
            pt_table_refresh(table);
            break;
        default:
            pt_table_scroll_to_row(table, below(&random, 2000));
            pt_table_scroll_to_column(table, below(&random, 10));
            break;
        }
        check_table_invariants(table);
    }

    uint32_t pixels = 800u * 500u;
    uint32_t *buffer = calloc(pixels, sizeof(*buffer));
    pt_check(buffer != NULL);
    if (buffer) {
        pt_canvas canvas;
        pt_canvas_init(&canvas, buffer, 800, 500, 800, 1.0f);
        pt_check(pt_window_paint(window, &canvas));
        free(buffer);
    }
    pt_window_free(window);
}

typedef struct tree_model {
    pt_tree_node *nodes[140];
    uint32_t parents[140];
    bool expanded[140];
    uint32_t count;
} tree_model;

static uint32_t flatten_tree(const tree_model *model, uint32_t parent, uint32_t *out,
                             uint32_t used, uint32_t capacity) {
    for (uint32_t i = 0; i < model->count; ++i) {
        if (model->parents[i] != parent) {
            continue;
        }
        if (used < capacity) {
            out[used++] = i;
        }
        if (model->expanded[i]) {
            used = flatten_tree(model, i, out, used, capacity);
        }
    }
    return used;
}

static void check_tree(const pt_widget *tree, tree_model *model) {
    uint32_t roots = 0;
    for (uint32_t i = 0; i < model->count; ++i) {
        if (model->parents[i] == UINT32_MAX) {
            ++roots;
        }
    }
    pt_check(pt_tree_get_root_count(tree) == roots);
    for (uint32_t i = 0; i < model->count; ++i) {
        pt_tree_node *node = model->nodes[i];
        pt_check(strcmp(pt_tree_node_get_text(node), "") != 0);
        uint32_t expected_parent = model->parents[i];
        pt_tree_node *parent = pt_tree_node_parent(node);
        if (expected_parent == UINT32_MAX) {
            pt_check(parent == NULL);
            pt_check(pt_tree_node_get_depth(node) == 0);
        } else {
            pt_check(parent == model->nodes[expected_parent]);
            pt_check(pt_tree_node_get_depth(node) ==
                     pt_tree_node_get_depth(model->nodes[expected_parent]) + 1);
        }
        uint32_t children = 0;
        for (uint32_t j = 0; j < model->count; ++j) {
            if (model->parents[j] == i) {
                pt_check(pt_tree_node_get_child(node, children) == model->nodes[j]);
                ++children;
            }
        }
        pt_check(pt_tree_node_get_child_count(node) == children);
        pt_check(pt_tree_node_get_child(node, children) == NULL);
        pt_check(pt_tree_node_has_children(node) == (children > 0));
        if (pt_tree_node_get_expanded(node) != model->expanded[i]) {
            fprintf(stderr, "expand mismatch node=%s actual=%d model=%d\n",
                    pt_tree_node_get_text(node), pt_tree_node_get_expanded(node),
                    model->expanded[i]);
        }
        pt_check(pt_tree_node_get_expanded(node) == model->expanded[i]);
    }

    uint32_t expected[140];
    uint32_t visible = flatten_tree(model, UINT32_MAX, expected, 0, 140);
    pt_check(pt_tree_get_visible_count(tree) == visible);
    for (uint32_t row = 0; row <= visible; ++row) {
        pt_tree_node *node = pt_tree_get_visible_node(tree, row);
        if (row < visible) {
            if (node != model->nodes[expected[row]]) {
                fprintf(stderr, "tree mismatch row=%u expected=%s actual=%s count=%u visible=%u\n",
                        row, pt_tree_node_get_text(model->nodes[expected[row]]),
                        node ? pt_tree_node_get_text(node) : "(null)", model->count, visible);
            }
            pt_check(node == model->nodes[expected[row]]);
        } else {
            pt_check(node == NULL);
        }
    }
}

static void expand_ancestors(tree_model *model, uint32_t node) {
    uint32_t parent = model->parents[node];
    while (parent != UINT32_MAX) {
        model->expanded[parent] = true;
        parent = model->parents[parent];
    }
}

static void test_tree_indices(pt_app *app) {
    pt_window *window = pt_window_create(app, "tree", 700, 500);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget *tree = pt_tree_create(root);
    pt_widget *other = pt_tree_create(root);
    pt_check(tree != NULL);
    pt_check(other != NULL);
    if (!tree || !other) {
        pt_window_free(window);
        return;
    }
    pt_tree_node *foreign = pt_tree_add_node(other, NULL, "foreign");
    pt_check(foreign != NULL);

    rng random = {0xE7037ED1A0B428DBull};
    for (int run = 0; run < 4; ++run) {
        pt_tree_clear(tree);
        tree_model model;
        memset(&model, 0, sizeof(model));
        model.count = 0;
        pt_tree_node *selected = NULL;
        while (model.count < 100) {
            char label[32];
            uint32_t operation = model.count == 0 ? 0 : below(&random, 10);
            if (operation < 4) {
                uint32_t parent = UINT32_MAX;
                if (model.count > 0 && below(&random, 4) != 0) {
                    parent = below(&random, model.count);
                }
                snprintf(label, sizeof(label), "n%u", model.count);
                pt_tree_node *node =
                    pt_tree_add_node(tree, parent == UINT32_MAX ? NULL : model.nodes[parent],
                                     label);
                pt_check(node != NULL);
                if (!node) {
                    break;
                }
                model.nodes[model.count] = node;
                model.parents[model.count] = parent;
                model.expanded[model.count] = false;
                ++model.count;
            } else if (operation < 6) {
                uint32_t index = below(&random, model.count);
                bool expanded = below(&random, 2) != 0;
                pt_tree_node_set_expanded(model.nodes[index], expanded);
                model.expanded[index] = expanded;
            } else if (operation < 8) {
                uint32_t index = below(&random, model.count);
                pt_tree_set_selected(tree, model.nodes[index]);
                selected = model.nodes[index];
                expand_ancestors(&model, index);
            } else if (operation == 8) {
                pt_tree_set_selected(tree, NULL);
                selected = NULL;
            } else {
                pt_tree_refresh(tree);
                uint32_t index = below(&random, model.count);
                pt_tree_scroll_to_node(tree, model.nodes[index]);
                expand_ancestors(&model, index);
            }
            pt_check(pt_tree_get_selected(tree) == selected);
            check_tree(tree, &model);
        }

        pt_tree_set_selected(tree, model.nodes[0]);
        pt_tree_set_selected(tree, foreign);
        pt_check(pt_tree_get_selected(tree) == model.nodes[0]);
        uint32_t previous = pt_tree_get_visible_count(tree);
        pt_tree_clear(tree);
        pt_check(pt_tree_get_visible_count(tree) == 0);
        pt_check(pt_tree_get_root_count(tree) == 0);
        pt_check(previous > 0);
        pt_check(pt_tree_get_selected(tree) == NULL);
    }
    pt_window_free(window);
}

static void test_damage_arithmetic(pt_app *app) {
    rng random = {0x8EBC6AF09C88C6E3ull};
    for (int i = 0; i < 400; ++i) {
        pt_rect a = {(float)below(&random, 800), (float)below(&random, 600),
                     (float)below(&random, 300), (float)below(&random, 300)};
        pt_rect b = {(float)below(&random, 800), (float)below(&random, 600),
                     (float)below(&random, 300), (float)below(&random, 300)};
        pt_rect intersection = pt_rect_intersect(a, b);
        pt_rect reverse = pt_rect_intersect(b, a);
        pt_check_near(intersection.x, reverse.x, 0.001);
        pt_check_near(intersection.y, reverse.y, 0.001);
        pt_check_near(intersection.w, reverse.w, 0.001);
        pt_check_near(intersection.h, reverse.h, 0.001);
        pt_check(pt_rect_intersects(a, b) ==
                 (intersection.w > 0.0f && intersection.h > 0.0f));
        if (intersection.w > 0.0f && intersection.h > 0.0f) {
            pt_check(pt_rect_contains(a, intersection.x, intersection.y));
            pt_check(pt_rect_contains(b, intersection.x, intersection.y));
            pt_check(intersection.x + intersection.w <= a.x + a.w + 0.001f);
            pt_check(intersection.x + intersection.w <= b.x + b.w + 0.001f);
        }
    }

    pt_window *window = pt_window_create(app, "damage", 640, 480);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *card = pt_card_create(pt_window_root(window));
    pt_check(card != NULL);
    pt_window_show(window);
    window->damage_valid = false;
    window->damage_full = false;

    bool expected_valid = false;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    for (int i = 0; i < 120; ++i) {
        pt_rect rect = {(float)below(&random, 2000) - 500.0f,
                        (float)below(&random, 2000) - 500.0f,
                        (float)below(&random, 1200),
                        (float)below(&random, 1200)};
        pt_window_damage_rect(window, rect);
        float left = rect.x < 0.0f ? 0.0f : rect.x;
        float top = rect.y < 0.0f ? 0.0f : rect.y;
        float right = rect.x + rect.w > 640.0f ? 640.0f : rect.x + rect.w;
        float bottom = rect.y + rect.h > 480.0f ? 480.0f : rect.y + rect.h;
        if (right <= left || bottom <= top) {
            continue;
        }
        if (!expected_valid) {
            x0 = left;
            y0 = top;
            x1 = right;
            y1 = bottom;
            expected_valid = true;
        } else {
            x0 = x0 < left ? x0 : left;
            y0 = y0 < top ? y0 : top;
            x1 = x1 > right ? x1 : right;
            y1 = y1 > bottom ? y1 : bottom;
        }
        pt_check(window->damage_valid == expected_valid);
        if (expected_valid) {
            pt_check_near(window->damage.x, x0, 0.01);
            pt_check_near(window->damage.y, y0, 0.01);
            pt_check_near(window->damage.w, x1 - x0, 0.01);
            pt_check_near(window->damage.h, y1 - y0, 0.01);
            pt_check(window->damage.x >= 0.0f && window->damage.y >= 0.0f);
            pt_check(window->damage.x + window->damage.w <= 640.0f + 0.01f);
            pt_check(window->damage.y + window->damage.h <= 480.0f + 0.01f);
        }
    }

    uint32_t *pixels = calloc(640u * 480u, sizeof(*pixels));
    pt_check(pixels != NULL);
    if (pixels) {
        pt_canvas canvas;
        pt_canvas_init(&canvas, pixels, 640, 480, 640, 1.0f);
        pt_check(pt_window_paint(window, &canvas));
        pt_canvas_clear(&canvas, 0xFF00FFFFu);
        pt_window_damage_rect(window, (pt_rect){100.0f, 100.0f, 40.0f, 30.0f});
        pt_check(pt_window_paint_damage(window, &canvas));
        pt_check(pt_canvas_get_pixel(&canvas, 4, 4) == 0xFF00FFFFu);
        pt_check(pt_canvas_get_pixel(&canvas, 635, 475) == 0xFF00FFFFu);
        free(pixels);
    }
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.property");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("property");
    }
    test_utf8_helpers();
    test_utf8_editing(app);
    test_uri_list();
    test_table_indices(app);
    test_tree_indices(app);
    test_damage_arithmetic(app);
    pt_app_destroy(app);
    return pt_test_finish("property");
}

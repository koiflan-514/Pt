/*
 * Versioned column-layout persistence (1.2 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Covers the round trip (width, order, visibility and multi-column sort), the
 * keyed and positional matching policies, version/malformed/truncated input
 * rejection, atomic failure, the blob and column limits, duplicate keys and
 * NULL arguments. restore() is also hammered with random garbage because it
 * is the parser boundary.
 */

#include "../src/internal.h"
#include "harness.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define layout_max_columns PT_TABLE_MAX_COLUMNS

typedef struct layout_snapshot {
    uint32_t count;
    struct {
        char key[PT_TABLE_COLUMN_KEY_MAX + 1];
        float width;
        bool hidden;
    } columns[layout_max_columns];
    uint32_t sort_count;
    uint32_t sort_columns[PT_TABLE_MAX_SORT_COLUMNS];
    pt_sort_direction sort_directions[PT_TABLE_MAX_SORT_COLUMNS];
} layout_snapshot;

static layout_snapshot snapshot;

static void capture(pt_widget *table) {
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.count = pt_table_get_column_count(table);
    for (uint32_t i = 0; i < snapshot.count; ++i) {
        snprintf(snapshot.columns[i].key, sizeof(snapshot.columns[i].key), "%s",
                 pt_table_get_column_key(table, i));
        snapshot.columns[i].width = pt_table_get_column_width(table, i);
        snapshot.columns[i].hidden = pt_table_get_column_hidden(table, i);
    }
    snapshot.sort_count = pt_table_get_sort_columns(
        table, snapshot.sort_columns, snapshot.sort_directions);
}

static bool same_columns(pt_widget *table) {
    if (pt_table_get_column_count(table) != snapshot.count) {
        return false;
    }
    for (uint32_t i = 0; i < snapshot.count; ++i) {
        if (strcmp(pt_table_get_column_key(table, i), snapshot.columns[i].key) != 0) {
            return false;
        }
        if (fabs((double)pt_table_get_column_width(table, i) -
                 (double)snapshot.columns[i].width) > 0.001) {
            return false;
        }
        if (pt_table_get_column_hidden(table, i) != snapshot.columns[i].hidden) {
            return false;
        }
    }
    uint32_t columns[PT_TABLE_MAX_SORT_COLUMNS] = {0};
    pt_sort_direction directions[PT_TABLE_MAX_SORT_COLUMNS] = {0};
    uint32_t sort_count = pt_table_get_sort_columns(table, columns, directions);
    if (sort_count != snapshot.sort_count) {
        return false;
    }
    for (uint32_t i = 0; i < sort_count; ++i) {
        if (columns[i] != snapshot.sort_columns[i] ||
            directions[i] != snapshot.sort_directions[i]) {
            return false;
        }
    }
    return true;
}

static pt_widget *new_table(pt_widget *root, uint32_t count, const char *const *keys) {
    pt_widget *table = pt_table_create(root);
    pt_check(table != NULL);
    if (!table) {
        return NULL;
    }
    pt_table_set_column_count(table, count);
    for (uint32_t i = 0; i < count; ++i) {
        char title[64];
        snprintf(title, sizeof(title), "%s%u", keys && keys[i] ? keys[i] : "col", i);
        pt_table_set_column_title(table, i, title);
        if (keys && keys[i]) {
            pt_table_set_column_key(table, i, keys[i]);
        }
    }
    return table;
}

static pt_widget *header_cell(pt_widget *table, uint32_t column) {
    pt_table_state *state = table ? table->extra : NULL;
    if (!state || !state->header) {
        return NULL;
    }
    pt_widget *cell = state->header->first_child;
    while (cell && column > 0) {
        cell = cell->next_sibling;
        --column;
    }
    return cell && cell->kind == pt_widget_table_header_cell ? cell : NULL;
}

static void destroy_table(pt_widget *table) {
    if (table) {
        pt_widget_destroy(table);
    }
}

static void test_round_trip(pt_widget *root) {
    static const char *const keys[] = {"id", "name", "size", "modified"};
    pt_widget *source = new_table(root, 4, keys);
    pt_check(source != NULL);
    if (!source) {
        return;
    }
    pt_table_set_column_title(source, 0, "ID");
    pt_table_set_column_title(source, 1, "Name");
    pt_table_set_column_title(source, 2, "Size");
    pt_table_set_column_title(source, 3, "Modified");
    pt_table_set_column_width(source, 0, 90.0f);
    pt_table_set_column_width(source, 1, 220.5f);
    pt_table_set_column_width(source, 2, 77.25f);
    pt_table_set_column_width(source, 3, 180.0f);
    pt_table_set_column_hidden(source, 2, true);
    pt_table_set_column_align(source, 3, pt_text_align_end);
    pt_table_set_column_resizable(source, 2, false);
    uint32_t sort_columns[2] = {1, 3};
    pt_sort_direction sort_directions[2] = {pt_sort_descending, pt_sort_ascending};
    pt_table_set_sort_columns(source, 2, sort_columns, sort_directions);
    /* name, size, modified, id */
    pt_check(pt_table_move_column(source, 0, 3));

    char *blob = pt_table_columns_save(source);
    pt_check(blob != NULL);
    if (!blob) {
        destroy_table(source);
        return;
    }
    pt_check(strncmp(blob, "PTCOLS 1\nC 4\n", 13) == 0);
    pt_check(strlen(blob) < PT_TABLE_COLUMN_LAYOUT_MAX);

    pt_widget *target = new_table(root, 4, keys);
    pt_check(target != NULL);
    if (target) {
        pt_table_set_column_title(target, 0, "ID");
        pt_table_set_column_title(target, 1, "Name");
        pt_table_set_column_title(target, 2, "Size");
        pt_table_set_column_title(target, 3, "Modified");
        /* Model configuration stays with the target column of that key. */
        pt_table_set_column_align(target, 3, pt_text_align_end);
        pt_table_set_column_resizable(target, 2, false);
        pt_check(pt_table_columns_restore(target, blob));
        pt_check(pt_table_get_column_count(target) == 4);
        pt_check(strcmp(pt_table_get_column_key(target, 0), "name") == 0);
        pt_check(strcmp(pt_table_get_column_key(target, 1), "size") == 0);
        pt_check(strcmp(pt_table_get_column_key(target, 2), "modified") == 0);
        pt_check(strcmp(pt_table_get_column_key(target, 3), "id") == 0);
        pt_check_near(pt_table_get_column_width(target, 0), 220.5, 0.001);
        pt_check_near(pt_table_get_column_width(target, 1), 77.25, 0.001);
        pt_check_near(pt_table_get_column_width(target, 2), 180.0, 0.001);
        pt_check_near(pt_table_get_column_width(target, 3), 90.0, 0.001);
        pt_check(!pt_table_get_column_hidden(target, 0));
        pt_check(pt_table_get_column_hidden(target, 1));
        pt_check(!pt_table_get_column_hidden(target, 2));
        pt_check(!pt_table_get_column_hidden(target, 3));

        /* Titles, alignment and resizability travel with the keyed column. */
        pt_check(strcmp(pt_widget_get_text(header_cell(target, 0)), "Name") == 0);
        pt_check(strcmp(pt_widget_get_text(header_cell(target, 2)), "Modified") == 0);
        pt_widget *modified_head = header_cell(target, 2);
        pt_widget *size_head = header_cell(target, 1);
        pt_check(modified_head && modified_head->text_align == pt_text_align_end);
        pt_check(size_head && !size_head->visible);
        pt_table_state *state = target->extra;
        pt_check(state && !state->column_resizable[1]);

        /* Sort survives the permutation and still points at the same keys. */
        uint32_t columns[PT_TABLE_MAX_SORT_COLUMNS] = {0};
        pt_sort_direction directions[PT_TABLE_MAX_SORT_COLUMNS] = {0};
        pt_check(pt_table_get_sort_columns(target, columns, directions) == 2);
        pt_check(strcmp(pt_table_get_column_key(target, columns[0]), "name") == 0);
        pt_check(directions[0] == pt_sort_descending);
        pt_check(strcmp(pt_table_get_column_key(target, columns[1]), "modified") == 0);
        pt_check(directions[1] == pt_sort_ascending);

        /* Saving the restored table is byte-identical. */
        char *again = pt_table_columns_save(target);
        pt_check(again != NULL);
        if (again) {
            pt_check(strcmp(again, blob) == 0);
            pt_free(again);
        }
        /* Restoring the same blob is idempotent. */
        pt_check(pt_table_columns_restore(target, blob));
        pt_check(strcmp(pt_table_get_column_key(target, 0), "name") == 0);
        pt_check(pt_table_get_column_hidden(target, 1));
    }
    pt_free(blob);
    destroy_table(target);
    destroy_table(source);
}

static void test_positional(pt_widget *root) {
    pt_widget *source = new_table(root, 3, NULL);
    pt_check(source != NULL);
    if (!source) {
        return;
    }
    pt_table_set_column_width(source, 0, 111.0f);
    pt_table_set_column_width(source, 1, 122.0f);
    pt_table_set_column_width(source, 2, 133.0f);
    pt_table_set_column_hidden(source, 1, true);
    char *blob = pt_table_columns_save(source);
    pt_check(blob != NULL);
    if (!blob) {
        destroy_table(source);
        return;
    }
    pt_check(strncmp(blob, "PTCOLS 1\nC 3\n- ", 15) == 0);

    pt_widget *target = new_table(root, 3, NULL);
    if (target) {
        pt_check(pt_table_columns_restore(target, blob));
        pt_check_near(pt_table_get_column_width(target, 0), 111.0, 0.001);
        pt_check_near(pt_table_get_column_width(target, 1), 122.0, 0.001);
        pt_check_near(pt_table_get_column_width(target, 2), 133.0, 0.001);
        pt_check(pt_table_get_column_hidden(target, 1));
        pt_check(strcmp(pt_table_get_column_key(target, 0), "") == 0);
    }
    pt_free(blob);
    destroy_table(target);
    destroy_table(source);
}

static void test_empty_and_single(pt_widget *root) {
    pt_widget *empty = new_table(root, 0, NULL);
    pt_check(empty != NULL);
    if (empty) {
        char *blob = pt_table_columns_save(empty);
        pt_check(blob != NULL);
        if (blob) {
            pt_check(strcmp(blob, "PTCOLS 1\nC 0\nS 0\n") == 0);
            pt_check(pt_table_columns_restore(empty, blob));
            pt_check(pt_table_get_column_count(empty) == 0);
            pt_free(blob);
        }
        destroy_table(empty);
    }

    static const char *const keys[] = {"only"};
    pt_widget *single = new_table(root, 1, keys);
    if (single) {
        pt_table_set_column_width(single, 0, 200.0f);
        pt_table_set_column_hidden(single, 0, true);
        uint32_t sort_column = 0;
        pt_table_set_sort(single, 0, pt_sort_descending);
        char *blob = pt_table_columns_save(single);
        pt_check(blob != NULL);
        if (blob) {
            pt_widget *target = new_table(root, 1, keys);
            if (target) {
                pt_check(pt_table_columns_restore(target, blob));
                pt_check(pt_table_get_column_count(target) == 1);
                pt_check_near(pt_table_get_column_width(target, 0), 200.0, 0.001);
                pt_check(pt_table_get_column_hidden(target, 0));
                pt_check(pt_table_get_sort(target, &sort_column) == pt_sort_descending);
                pt_check(sort_column == 0);
            }
            destroy_table(target);
            pt_free(blob);
        }
        destroy_table(single);
    }
}

static void set_indexed_keys(pt_widget *table) {
    char key[PT_TABLE_COLUMN_KEY_MAX + 1];
    for (uint32_t i = 0; i < PT_TABLE_MAX_COLUMNS; ++i) {
        snprintf(key, sizeof(key), "column-%04u-%015u-abcdefghijklmnopqrstuvwxyz",
                 i, i);
        pt_table_set_column_key(table, i, key);
    }
}

static void test_max_columns(pt_widget *root) {
    pt_widget *source = new_table(root, PT_TABLE_MAX_COLUMNS, NULL);
    pt_check(source != NULL);
    if (!source) {
        return;
    }
    set_indexed_keys(source);
    for (uint32_t i = 0; i < PT_TABLE_MAX_COLUMNS; ++i) {
        pt_table_set_column_width(source, i, 48.0f + (float)(i % 100));
        pt_table_set_column_hidden(source, i, (i % 7u) == 0);
    }
    char *blob = pt_table_columns_save(source);
    pt_check(blob != NULL);
    if (blob) {
        size_t length = strlen(blob);
        pt_check(length > 0);
        pt_check(length < PT_TABLE_COLUMN_LAYOUT_MAX);
        /* The keyed limit uses the full key budget, so this is the worst case. */
        pt_check(length > 100000);

        pt_widget *target = new_table(root, PT_TABLE_MAX_COLUMNS, NULL);
        if (target) {
            set_indexed_keys(target);
            pt_check(pt_table_columns_restore(target, blob));
            pt_check(pt_table_get_column_count(target) == PT_TABLE_MAX_COLUMNS);
            pt_check(strcmp(pt_table_get_column_key(target, 0),
                            pt_table_get_column_key(source, 0)) == 0);
            pt_check(strcmp(pt_table_get_column_key(target, PT_TABLE_MAX_COLUMNS - 1),
                            pt_table_get_column_key(source, PT_TABLE_MAX_COLUMNS - 1)) == 0);
            pt_check_near(pt_table_get_column_width(target, 0), 48.0, 0.001);
            pt_check_near(pt_table_get_column_width(target, PT_TABLE_MAX_COLUMNS - 1),
                          48.0 + (float)((PT_TABLE_MAX_COLUMNS - 1) % 100), 0.001);
            pt_check(pt_table_get_column_hidden(target, 0));
        }
        destroy_table(target);
        pt_free(blob);
    }
    destroy_table(source);
}

static void test_version_and_truncation(pt_widget *root) {
    static const char *const keys[] = {"a", "b", "c"};
    pt_widget *table = new_table(root, 3, keys);
    pt_check(table != NULL);
    if (!table) {
        return;
    }
    pt_table_set_column_width(table, 0, 120.0f);
    pt_table_set_column_width(table, 1, 130.0f);
    pt_table_set_column_width(table, 2, 140.0f);
    pt_table_set_column_hidden(table, 2, true);
    capture(table);

    char *blob = pt_table_columns_save(table);
    pt_check(blob != NULL);
    if (!blob) {
        destroy_table(table);
        return;
    }
    size_t length = strlen(blob);

    char *bad = pt_strdup(blob);
    bad[7] = '2'; /* PTCOLS 1 -> PTCOLS 2 */
    pt_check(!pt_table_columns_restore(table, bad));
    pt_check(same_columns(table));
    bad[7] = '1';
    bad[0] = 'X';
    pt_check(!pt_table_columns_restore(table, bad));
    pt_check(same_columns(table));
    pt_free(bad);

    pt_check(!pt_table_columns_restore(table, "PTCOLS 10\nC 3\n- 120000 0\n"
                                              "- 130000 0\n- 140000 1\nS 0\n"));
    pt_check(same_columns(table));
    pt_check(!pt_table_columns_restore(table, "PTCOLS\nC 3\n"));
    pt_check(same_columns(table));

    /* Every prefix except the final newline is truncated and rejected. */
    char *prefix = pt_alloc(length + 1);
    for (size_t i = 0; i + 1 < length; ++i) {
        memcpy(prefix, blob, i);
        prefix[i] = '\0';
        pt_check(!pt_table_columns_restore(table, prefix));
    }
    memcpy(prefix, blob, length - 1);
    prefix[length - 1] = '\0';
    pt_check(pt_table_columns_restore(table, prefix));
    pt_check(same_columns(table));
    pt_free(prefix);

    destroy_table(table);
    pt_free(blob);
}

static void expect_reject(pt_widget *table, const char *blob) {
    pt_check(!pt_table_columns_restore(table, blob));
    pt_check(same_columns(table));
}

static void test_malformed(pt_widget *root) {
    pt_widget *table = new_table(root, 2, NULL);
    pt_check(table != NULL);
    if (!table) {
        return;
    }
    pt_table_set_column_width(table, 0, 160.0f);
    pt_table_set_column_width(table, 1, 160.0f);
    capture(table);

    const char *cases[] = {
        "",
        "\n",
        "PTCOLS 2\nC 2\n- 160000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\n",
        "PTCOLS 1\nC\nS 0\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 0\nextra\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 0\n\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\n",
        "PTCOLS 1\nC 3\n- 160000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- 0 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- -1 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- nan 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- inf 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- 2000000000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- 160000 2\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000\nS 0\n",
        "PTCOLS 1\nC 2\nzz 160000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n6161 160000 0\n6161 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n616263 160000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n6100 160000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 5\n0 1\n0 2\n0 1\n0 2\n0 1\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 1\n0 0\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 1\n0 3\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 1\n2 1\n",
        "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 2\n0 1\n0 2\n",
        "PTCOLS 1\nC 1025\n- 160000 0\nS 0\n",
        "PTCOLS\t1\nC 2\n- 160000 0\n- 160000 0\nS 0\n",
        "PTCOLS 1\r\nC 2\r\n- 160000 0\r\n- 160000 0\r\nS 0\r\n",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        expect_reject(table, cases[i]);
    }

    /* A well-formed baseline is still accepted, with or without the final LF. */
    pt_check(pt_table_columns_restore(
        table, "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 0\n"));
    pt_check(same_columns(table));
    pt_check(pt_table_columns_restore(
        table, "PTCOLS 1\nC 2\n- 160000 0\n- 160000 0\nS 0"));
    pt_check(same_columns(table));

    destroy_table(table);
}

static void test_mismatch(pt_widget *root) {
    static const char *const keys[] = {"a", "b", "c"};
    pt_widget *source = new_table(root, 3, keys);
    pt_check(source != NULL);
    if (!source) {
        return;
    }
    char *blob = pt_table_columns_save(source);
    pt_check(blob != NULL);
    if (!blob) {
        destroy_table(source);
        return;
    }

    static const char *const extra_keys[] = {"a", "b", "c", "d"};
    pt_widget *more = new_table(root, 4, extra_keys);
    if (more) {
        capture(more);
        expect_reject(more, blob);
    }
    destroy_table(more);

    static const char *const other_keys[] = {"a", "b", "d"};
    pt_widget *other = new_table(root, 3, other_keys);
    if (other) {
        capture(other);
        expect_reject(other, blob);
    }
    destroy_table(other);

    /* Positional layouts need the same number of columns. */
    pt_widget *positional = new_table(root, 3, NULL);
    if (positional) {
        char *positional_blob = pt_table_columns_save(positional);
        pt_widget *two = new_table(root, 2, NULL);
        if (two && positional_blob) {
            capture(two);
            expect_reject(two, positional_blob);
        }
        destroy_table(two);
        pt_free(positional_blob);
    }
    destroy_table(positional);

    /* The matching keyed table accepts it, even in a different base order. */
    static const char *const shuffled[] = {"c", "a", "b"};
    pt_widget *matching = new_table(root, 3, shuffled);
    if (matching) {
        pt_check(pt_table_columns_restore(matching, blob));
        pt_check(strcmp(pt_table_get_column_key(matching, 0), "a") == 0);
        pt_check(strcmp(pt_table_get_column_key(matching, 1), "b") == 0);
        pt_check(strcmp(pt_table_get_column_key(matching, 2), "c") == 0);
    }
    destroy_table(matching);

    /* A hand-written blob with duplicate keys is rejected. */
    pt_widget *keys_table = new_table(root, 2, NULL);
    if (keys_table) {
        pt_table_set_column_key(keys_table, 0, "aa");
        pt_table_set_column_key(keys_table, 1, "bb");
        capture(keys_table);
        expect_reject(keys_table, "PTCOLS 1\nC 2\n6161 160000 0\n6161 160000 0\nS 0\n");
    }
    destroy_table(keys_table);

    pt_free(blob);
    destroy_table(source);
}

static void test_keys(pt_widget *root) {
    pt_widget *table = new_table(root, 3, NULL);
    pt_check(table != NULL);
    if (!table) {
        return;
    }
    pt_table_set_column_key(table, 0, "alpha");
    pt_check(strcmp(pt_table_get_column_key(table, 0), "alpha") == 0);
    pt_table_set_column_key(table, 1, "beta");
    /* Duplicate keys are rejected, leaving the old key in place. */
    pt_table_set_column_key(table, 1, "alpha");
    pt_check(strcmp(pt_table_get_column_key(table, 1), "beta") == 0);
    /* Empty and NULL keys clear the identity. */
    pt_table_set_column_key(table, 0, "");
    pt_check(strcmp(pt_table_get_column_key(table, 0), "") == 0);
    pt_table_set_column_key(table, 0, NULL);
    pt_check(strcmp(pt_table_get_column_key(table, 0), "") == 0);
    /* Over-long keys are truncated to the documented limit. */
    char long_key[PT_TABLE_COLUMN_KEY_MAX + 40];
    memset(long_key, 'k', sizeof(long_key));
    long_key[sizeof(long_key) - 1] = '\0';
    pt_table_set_column_key(table, 2, long_key);
    pt_check(strlen(pt_table_get_column_key(table, 2)) == PT_TABLE_COLUMN_KEY_MAX);
    /* Bad indices and a NULL table are safe. */
    pt_table_set_column_key(table, 99, "nope");
    pt_check(strcmp(pt_table_get_column_key(table, 99), "") == 0);
    pt_check(strcmp(pt_table_get_column_key(NULL, 0), "") == 0);
    destroy_table(table);
}

static void test_limits_and_null(pt_widget *root) {
    pt_widget *table = new_table(root, 2, NULL);
    pt_check(table != NULL);
    if (!table) {
        return;
    }
    capture(table);

    pt_check(pt_table_columns_save(NULL) == NULL);
    pt_check(!pt_table_columns_restore(NULL, "PTCOLS 1\nC 0\nS 0\n"));
    pt_check(!pt_table_columns_restore(table, NULL));
    pt_check(same_columns(table));

    /* A width that cannot be represented makes save fail cleanly. */
    pt_table_set_column_width(table, 0, INFINITY);
    pt_check(pt_table_columns_save(table) == NULL);
    pt_table_set_column_width(table, 0, NAN);
    pt_check(pt_table_columns_save(table) == NULL);
    pt_table_set_column_width(table, 0, 160.0f);
    char *blob = pt_table_columns_save(table);
    pt_check(blob != NULL);
    pt_free(blob);

    /* One byte over the hard cap is rejected before parsing. */
    char *huge = pt_alloc(PT_TABLE_COLUMN_LAYOUT_MAX + 2u);
    memset(huge, 'A', PT_TABLE_COLUMN_LAYOUT_MAX + 1u);
    huge[PT_TABLE_COLUMN_LAYOUT_MAX + 1u] = '\0';
    pt_check(!pt_table_columns_restore(table, huge));
    pt_check(same_columns(table));
    /* Exactly at the cap is parsed (and rejected as malformed). */
    huge[PT_TABLE_COLUMN_LAYOUT_MAX] = '\0';
    pt_check(!pt_table_columns_restore(table, huge));
    pt_check(same_columns(table));
    pt_free(huge);

    destroy_table(table);
}

static uint32_t random_state = 0x9e3779b9u;

static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_random_garbage(pt_widget *root) {
    static const char *const keys[] = {"x", "y", "z"};
    pt_widget *table = new_table(root, 3, keys);
    pt_check(table != NULL);
    if (!table) {
        return;
    }
    pt_table_set_column_width(table, 0, 101.0f);
    pt_table_set_column_width(table, 1, 102.0f);
    pt_table_set_column_width(table, 2, 103.0f);
    capture(table);

    static const char alphabet[] =
        "PTCOLS0123456789 \n-abcdefABCDEFxyz[]{}<>!@#$%^&*()_+=\\/|;:'\",.?";
    char buffer[256];
    for (uint32_t iteration = 0; iteration < 500; ++iteration) {
        size_t length = (size_t)(next_random() % (sizeof(buffer) - 1u));
        for (size_t i = 0; i < length; ++i) {
            buffer[i] = alphabet[next_random() % (sizeof(alphabet) - 1u)];
        }
        buffer[length] = '\0';
        bool accepted = pt_table_columns_restore(table, buffer);
        pt_check(!accepted);
        pt_check(same_columns(table));
    }

    /* Flip bytes of a real blob: some flips stay valid (a width digit), but
     * none may crash or leave a partially applied layout when rejected. */
    char *blob = pt_table_columns_save(table);
    pt_check(blob != NULL);
    if (blob) {
        size_t length = strlen(blob);
        char *mutated = pt_alloc(length + 1);
        for (size_t i = 0; i < length; ++i) {
            memcpy(mutated, blob, length + 1);
            mutated[i] = (char)((unsigned char)mutated[i] ^ 0x01u);
            if (!pt_table_columns_restore(table, mutated)) {
                pt_check(same_columns(table));
            }
            pt_check(pt_table_columns_restore(table, blob));
            pt_check(same_columns(table));
        }
        pt_free(mutated);
        pt_free(blob);
    }
    destroy_table(table);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.column_persist");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("column_persist");
    }
    pt_window *window = pt_window_create(app, "columns", 800, 600);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    test_round_trip(root);
    test_positional(root);
    test_empty_and_single(root);
    test_max_columns(root);
    test_version_and_truncation(root);
    test_malformed(root);
    test_mismatch(root);
    test_keys(root);
    test_limits_and_null(root);
    test_random_garbage(root);

    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("column_persist");
}

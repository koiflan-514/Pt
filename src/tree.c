/*
 * Pt virtualized hierarchical tree view.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

static pt_tree_state *tree_state(pt_widget *tree) {
    if (!tree || tree->kind != pt_widget_tree) {
        return NULL;
    }
    return tree->extra;
}

static const pt_tree_state *tree_state_const(const pt_widget *tree) {
    if (!tree || tree->kind != pt_widget_tree) {
        return NULL;
    }
    return tree->extra;
}

static void tree_free_node(pt_tree_node *node) {
    while (node) {
        pt_tree_node *next = node->next_sibling;
        if (node->first_child) {
            tree_free_node(node->first_child);
        }
        free(node->text);
        free(node);
        node = next;
    }
}

static void tree_append_visible(pt_tree_state *state, pt_tree_node *node,
                                uint32_t *capacity) {
    if (state->visible_count == *capacity) {
        uint32_t next = *capacity ? *capacity * 2u : 32u;
        state->visible = pt_realloc_mem(state->visible, (size_t)next * sizeof(*state->visible));
        *capacity = next;
    }
    state->visible[state->visible_count++] = node;
    if (node->expanded) {
        for (pt_tree_node *child = node->first_child; child; child = child->next_sibling) {
            tree_append_visible(state, child, capacity);
        }
    }
}

void pt_tree_rebuild(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !state->visible_dirty) {
        return;
    }
    uint32_t capacity = state->visible_capacity;
    state->visible_count = 0;
    for (pt_tree_node *root = state->first_root; root; root = root->next_sibling) {
        tree_append_visible(state, root, &capacity);
    }
    state->visible_capacity = capacity;
    state->visible_dirty = false;
}

static void tree_visible_mark_dirty(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    state->visible_dirty = true;
    pt_widget_mark_dirty(tree, true);
}

pt_widget *pt_tree_create(pt_widget *parent) {
    pt_widget *tree = pt_widget_new(parent, pt_widget_tree);
    pt_tree_state *state = pt_alloc_zeroed(1, sizeof(*state));
    tree->extra = state;
    state->indent = 22.0f;
    state->visible_dirty = true;
    tree->row_height = 32.0f;
    return tree;
}

pt_tree_node *pt_tree_add_node(pt_widget *tree, pt_tree_node *parent_node,
                               const char *text) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return NULL;
    }
    if (parent_node && parent_node->tree != tree) {
        return NULL;
    }
    pt_tree_node *node = pt_alloc_zeroed(1, sizeof(*node));
    node->tree = tree;
    node->parent = parent_node;
    node->text = pt_strdup(text ? text : "");
    node->depth = parent_node ? parent_node->depth + 1u : 0u;
    if (parent_node) {
        if (parent_node->last_child) {
            parent_node->last_child->next_sibling = node;
        } else {
            parent_node->first_child = node;
        }
        parent_node->last_child = node;
    } else {
        if (state->last_root) {
            state->last_root->next_sibling = node;
        } else {
            state->first_root = node;
        }
        state->last_root = node;
    }
    tree_visible_mark_dirty(tree);
    pt_widget_a11y_emit(tree, pt_a11y_event_structure_changed);
    return node;
}

void pt_tree_clear(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    tree_free_node(state->first_root);
    state->first_root = NULL;
    state->last_root = NULL;
    state->selected = NULL;
    state->selection_anchor = NULL;
    state->selection_count = 0;
    pt_marquee_end(&state->marquee);
    state->visible_count = 0;
    state->visible_dirty = true;
    pt_widget_clear_children(tree);
    pt_widget_mark_dirty(tree, true);
    pt_widget_a11y_emit(tree, pt_a11y_event_structure_changed);
}

uint32_t pt_tree_get_root_count(const pt_widget *tree) {
    const pt_tree_state *state = tree_state_const(tree);
    uint32_t count = 0;
    if (!state) {
        return 0;
    }
    for (const pt_tree_node *root = state->first_root; root; root = root->next_sibling) {
        ++count;
    }
    return count;
}

pt_tree_node *pt_tree_get_root_node(const pt_widget *tree, uint32_t index) {
    const pt_tree_state *state = tree_state_const(tree);
    if (!state) {
        return NULL;
    }
    pt_tree_node *root = state->first_root;
    while (root && index > 0) {
        root = root->next_sibling;
        --index;
    }
    return root;
}

const char *pt_tree_node_get_text(const pt_tree_node *node) {
    return node && node->text ? node->text : "";
}

void pt_tree_node_set_text(pt_tree_node *node, const char *text) {
    if (!node) {
        return;
    }
    free(node->text);
    node->text = pt_strdup(text ? text : "");
    pt_widget_mark_dirty(node->tree, false);
    pt_widget_a11y_emit(node->tree, pt_a11y_event_name_changed);
}

bool pt_tree_node_get_expanded(const pt_tree_node *node) {
    return node ? node->expanded : false;
}

void pt_tree_node_set_expanded(pt_tree_node *node, bool expanded) {
    if (!node || node->expanded == expanded) {
        return;
    }
    node->expanded = expanded;
    tree_visible_mark_dirty(node->tree);
    pt_widget_a11y_emit(node->tree, pt_a11y_event_state_changed);
}

bool pt_tree_node_has_children(const pt_tree_node *node) {
    return node && node->first_child != NULL;
}

uint32_t pt_tree_node_get_depth(const pt_tree_node *node) {
    return node ? node->depth : 0;
}

pt_tree_node *pt_tree_node_parent(const pt_tree_node *node) {
    return node ? node->parent : NULL;
}

uint32_t pt_tree_node_get_child_count(const pt_tree_node *node) {
    uint32_t count = 0;
    for (pt_tree_node *child = node ? node->first_child : NULL; child;
         child = child->next_sibling) {
        ++count;
    }
    return count;
}

pt_tree_node *pt_tree_node_get_child(const pt_tree_node *node, uint32_t index) {
    pt_tree_node *child = node ? node->first_child : NULL;
    while (child && index > 0) {
        child = child->next_sibling;
        --index;
    }
    return child;
}

void *pt_tree_node_get_data(const pt_tree_node *node) {
    return node ? node->data : NULL;
}

void pt_tree_node_set_data(pt_tree_node *node, void *data) {
    if (node) {
        node->data = data;
    }
}

uint32_t pt_tree_get_visible_count(const pt_widget *tree) {
    pt_tree_rebuild((pt_widget *)tree);
    const pt_tree_state *state = tree_state_const(tree);
    return state ? state->visible_count : 0;
}

pt_tree_node *pt_tree_get_visible_node(const pt_widget *tree, uint32_t row) {
    pt_tree_rebuild((pt_widget *)tree);
    const pt_tree_state *state = tree_state_const(tree);
    if (!state || row >= state->visible_count) {
        return NULL;
    }
    return state->visible[row];
}

pt_tree_node *pt_tree_get_selected(const pt_widget *tree) {
    const pt_tree_state *state = tree_state_const(tree);
    return state ? state->selected : NULL;
}

uint32_t pt_tree_index_of(const pt_widget *tree, const pt_tree_node *node) {
    pt_tree_rebuild((pt_widget *)tree);
    const pt_tree_state *state = tree_state_const(tree);
    if (!state || !node) {
        return pt_list_index_none;
    }
    for (uint32_t i = 0; i < state->visible_count; ++i) {
        if (state->visible[i] == node) {
            return i;
        }
    }
    return pt_list_index_none;
}

void pt_tree_reveal(pt_widget *tree, pt_tree_node *node) {
    uint32_t index = pt_tree_index_of(tree, node);
    if (index == pt_list_index_none) {
        return;
    }
    float row_height = pt_max_float(20.0f, tree->row_height);
    float viewport = pt_max_float(1.0f, tree->height - tree->padding_top -
                                           tree->padding_bottom);
    float top = (float)index * row_height;
    float bottom = top + row_height;
    if (top < tree->scroll_y) {
        tree->scroll_y = top;
    } else if (bottom > tree->scroll_y + viewport) {
        tree->scroll_y = bottom - viewport;
    }
    float content = (float)pt_tree_get_visible_count(tree) * row_height;
    tree->scroll_y = pt_clamp_float(tree->scroll_y, 0.0f,
                                    pt_max_float(0.0f, content - viewport));
    pt_widget_mark_dirty(tree, true);
}

static void tree_clear_node_selection(pt_tree_node *node) {
    while (node) {
        if (node->selected) {
            node->selected = false;
        }
        tree_clear_node_selection(node->first_child);
        node = node->next_sibling;
    }
}

static pt_tree_node *tree_first_selected(pt_tree_node *node) {
    while (node) {
        if (node->selected) {
            return node;
        }
        pt_tree_node *found = tree_first_selected(node->first_child);
        if (found) {
            return found;
        }
        node = node->next_sibling;
    }
    return NULL;
}

static uint32_t tree_collect_selection(pt_tree_node *node, pt_tree_node **nodes,
                                       uint32_t capacity, uint32_t written) {
    while (node && written < capacity) {
        if (node->selected) {
            nodes[written++] = node;
        }
        written = tree_collect_selection(node->first_child, nodes, capacity, written);
        node = node->next_sibling;
    }
    return written;
}

/* Selection lives on the nodes, so a collapsed branch keeps its rows selected.
 * That means clearing has to walk the whole tree rather than the visible rows,
 * and the count only stays honest if every mutation goes through these two. */
static void tree_reset_selection(pt_tree_state *state) {
    tree_clear_node_selection(state->first_root);
    state->selection_count = 0;
}

static void tree_apply_selection(pt_widget *tree) {
    for (pt_widget *child = tree->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_tree_row && child->list_bound) {
            bool selected = child->tree_node && child->tree_node->selected;
            if (child->row_selected != selected) {
                child->row_selected = selected;
            }
        }
    }
}

/* The public set helpers stay silent so an application can rebuild a selection
 * without echoing, but every gesture that moves the primary row reports it, the
 * way a plain click already does. pt_tree_select notifies on its own, so this is
 * only for the paths that change the primary row behind it. */
static void tree_notify_primary(pt_widget *tree, pt_tree_node *previous) {
    pt_tree_state *state = tree_state(tree);
    if (state && state->on_select && state->selected && state->selected != previous) {
        state->on_select(tree, state->selected, state->select_data);
    }
}

void pt_tree_select(pt_widget *tree, pt_tree_node *node, bool notify) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    if (node && node->tree != tree) {
        return;
    }
    bool changed = state->selected != node;
    tree_reset_selection(state);
    state->selected = node;
    state->selection_anchor = node;
    if (node) {
        node->selected = true;
        state->selection_count = 1;
        for (pt_tree_node *ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
            if (!ancestor->expanded) {
                ancestor->expanded = true;
            }
        }
        state->visible_dirty = true;
        pt_tree_rebuild(tree);
        pt_tree_reveal(tree, node);
    }
    tree_apply_selection(tree);
    pt_widget_mark_dirty(tree, true);
    if (changed) {
        pt_widget_a11y_emit(tree, pt_a11y_event_selection_changed);
        if (notify && node && state->on_select) {
            state->on_select(tree, node, state->select_data);
        }
    }
}

uint32_t pt_tree_get_selection_count(const pt_widget *tree) {
    const pt_tree_state *state = tree_state_const(tree);
    return state ? state->selection_count : 0;
}

bool pt_tree_is_selected(const pt_widget *tree, const pt_tree_node *node) {
    return node && node->tree == tree && node->selected;
}

uint32_t pt_tree_get_selection(const pt_widget *tree, pt_tree_node **nodes,
                               uint32_t capacity) {
    const pt_tree_state *state = tree_state_const(tree);
    if (!state || !nodes || capacity == 0) {
        return 0;
    }
    uint32_t written = 0;
    /* Document order, hidden rows included: a collapsed branch is still part of
     * the selection the application owns. */
    return tree_collect_selection(state->first_root, nodes, capacity, written);
}

void pt_tree_select_range(pt_widget *tree, uint32_t first_row, uint32_t last_row) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    pt_tree_rebuild(tree);
    if (state->visible_count == 0) {
        return;
    }
    if (first_row > last_row) {
        uint32_t swap = first_row;
        first_row = last_row;
        last_row = swap;
    }
    if (first_row >= state->visible_count) {
        first_row = state->visible_count - 1;
    }
    if (last_row >= state->visible_count) {
        last_row = state->visible_count - 1;
    }
    tree_reset_selection(state);
    for (uint32_t row = first_row; row <= last_row; ++row) {
        pt_tree_node *node = state->visible[row];
        if (!node->selected) {
            node->selected = true;
            ++state->selection_count;
        }
    }
    state->selected = state->visible[first_row];
    state->selection_anchor = state->visible[first_row];
    tree_apply_selection(tree);
    pt_tree_reveal(tree, state->selected);
    pt_widget_mark_dirty(tree, true);
    pt_widget_a11y_emit(tree, pt_a11y_event_selection_changed);
}

void pt_tree_select_toggle(pt_widget *tree, pt_tree_node *node) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !node || node->tree != tree) {
        return;
    }
    if (node->selected) {
        node->selected = false;
        if (state->selection_count > 0) {
            --state->selection_count;
        }
        if (state->selected == node) {
            state->selected = tree_first_selected(state->first_root);
        }
    } else {
        node->selected = true;
        ++state->selection_count;
        state->selected = node;
        pt_tree_reveal(tree, node);
    }
    state->selection_anchor = node;
    tree_apply_selection(tree);
    pt_widget_mark_dirty(tree, true);
    pt_widget_a11y_emit(tree, pt_a11y_event_selection_changed);
}

void pt_tree_clear_selection(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    tree_reset_selection(state);
    state->selected = NULL;
    state->selection_anchor = NULL;
    tree_apply_selection(tree);
    pt_widget_mark_dirty(tree, true);
    pt_widget_a11y_emit(tree, pt_a11y_event_selection_changed);
}

/* Extend from the node the previous gesture anchored on. The anchor survives the
 * range call so repeated Shift+ arrows sweep the same span instead of chasing the
 * caret. */
static void tree_extend_selection(pt_widget *tree, pt_tree_state *state,
                                  uint32_t index) {
    uint32_t anchor = pt_tree_index_of(tree, state->selection_anchor);
    if (anchor == pt_list_index_none) {
        anchor = index;
    }
    pt_tree_select_range(tree, anchor, index);
    pt_tree_rebuild(tree);
    if (index < state->visible_count) {
        state->selected = state->visible[index];
        state->selection_anchor = anchor < state->visible_count
                                    ? state->visible[anchor]
                                    : state->selected;
        tree_apply_selection(tree);
        pt_tree_reveal(tree, state->selected);
        pt_widget_mark_dirty(tree, true);
    }
}

void pt_tree_set_selected(pt_widget *tree, pt_tree_node *node) {
    pt_tree_select(tree, node, false);
}

void pt_tree_set_row_height(pt_widget *tree, float height) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    tree->row_height = pt_max_float(20.0f, height);
    pt_widget_mark_dirty(tree, true);
}

void pt_tree_set_indent(pt_widget *tree, float indent) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    state->indent = pt_max_float(8.0f, indent);
    pt_widget_mark_dirty(tree, true);
}

void pt_tree_set_on_select(pt_widget *tree, pt_tree_node_callback callback, void *user_data) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    state->on_select = callback;
    state->select_data = user_data;
}

void pt_tree_set_on_activate(pt_widget *tree, pt_tree_node_callback callback,
                             void *user_data) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    state->on_activate = callback;
    state->activate_data = user_data;
}

/* ---------- inline rename ---------- */

void pt_tree_set_renamable(pt_widget *tree, bool renamable) {
    pt_tree_state *state = tree_state(tree);
    if (!state || state->renamable == renamable) {
        return;
    }
    state->renamable = renamable;
    if (!renamable) {
        pt_tree_cancel_rename(tree);
    }
}

bool pt_tree_get_renamable(const pt_widget *tree) {
    const pt_tree_state *state = tree_state_const(tree);
    return state && state->renamable;
}

static void tree_rename_detach(pt_widget *tree, pt_tree_state *state) {
    pt_widget *editor = state->rename_editor;
    if (!editor) {
        return;
    }
    state->rename_editor = NULL;
    state->rename_node = NULL;
    editor->editor_owner = NULL;
    pt_window *window = pt_widget_window(tree);
    if (window && window->focused == editor) {
        editor->focused = false;
        window->focused = NULL;
        pt_platform_ime_set_enabled(window, false);
    }
    pt_widget_destroy(editor);
}

bool pt_tree_begin_rename(pt_widget *tree, pt_tree_node *node) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !state->renamable || !node || node->tree != tree) {
        return false;
    }
    pt_tree_cancel_rename(tree);
    pt_tree_select(tree, node, false);
    pt_tree_reveal(tree, node);
    pt_widget *editor = pt_widget_new(tree, pt_widget_entry);
    editor->editor_owner = tree;
    editor->text = pt_strdup(node->text ? node->text : "");
    editor->requested_width = pt_auto;
    editor->requested_height = pt_auto;
    state->rename_editor = editor;
    state->rename_node = node;
    pt_widget_focus(editor);
    pt_widget_text_select_all(editor);
    pt_widget_mark_dirty(tree, true);
    pt_widget_a11y_emit(tree, pt_a11y_event_state_changed);
    return true;
}

bool pt_tree_commit_rename(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !state->rename_editor || !state->rename_node) {
        return false;
    }
    pt_widget *editor = state->rename_editor;
    pt_tree_node *node = state->rename_node;
    char *text = pt_strdup(editor->text ? editor->text : "");
    tree_rename_detach(tree, state);
    free(node->text);
    node->text = pt_strdup(text);
    pt_widget_mark_dirty(tree, true);
    if (state->on_rename) {
        state->on_rename(tree, node, text, state->rename_data);
    }
    pt_widget_a11y_emit(tree, pt_a11y_event_name_changed);
    free(text);
    return true;
}

void pt_tree_cancel_rename(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !state->rename_editor) {
        return;
    }
    tree_rename_detach(tree, state);
    pt_widget_mark_dirty(tree, true);
    pt_widget_a11y_emit(tree, pt_a11y_event_state_changed);
}

bool pt_tree_is_renaming(const pt_widget *tree) {
    const pt_tree_state *state = tree_state_const(tree);
    return state && state->rename_editor != NULL;
}

void pt_tree_set_on_rename(pt_widget *tree, pt_tree_rename_callback callback,
                           void *user_data) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    state->on_rename = callback;
    state->rename_data = user_data;
}

void pt_tree_refresh(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    state->visible_dirty = true;
    pt_widget_mark_dirty(tree, true);
}

void pt_tree_scroll_to_node(pt_widget *tree, pt_tree_node *node) {
    if (!tree || tree->kind != pt_widget_tree || !node || node->tree != tree) {
        return;
    }
    for (pt_tree_node *ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
        ancestor->expanded = true;
    }
    pt_tree_state *state = tree_state(tree);
    if (state) {
        state->visible_dirty = true;
    }
    pt_tree_reveal(tree, node);
}

void pt_tree_select_index(pt_widget *tree, uint32_t index, bool notify) {
    pt_tree_rebuild(tree);
    pt_tree_state *state = tree_state(tree);
    if (!state || state->visible_count == 0) {
        return;
    }
    if (index >= state->visible_count) {
        index = state->visible_count - 1;
    }
    pt_tree_select(tree, state->visible[index], notify);
}

bool pt_tree_activate_selected(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !state->selected) {
        return false;
    }
    if (state->on_activate) {
        state->on_activate(tree, state->selected, state->activate_data);
        return true;
    }
    return false;
}

bool pt_tree_key(pt_widget *tree, uint32_t keysym, uint32_t modifiers) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return false;
    }
    pt_tree_rebuild(tree);
    if (state->visible_count == 0) {
        return false;
    }
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    if ((modifiers & (uint32_t)pt_key_modifier_ctrl) != 0 &&
        (keysym == XKB_KEY_a || keysym == XKB_KEY_A)) {
        pt_tree_node *previous = state->selected;
        pt_tree_select_range(tree, 0, state->visible_count - 1);
        tree_notify_primary(tree, previous);
        return true;
    }
    uint32_t current = pt_tree_index_of(tree, state->selected);
    uint32_t next = current;
    float row_height = pt_max_float(20.0f, tree->row_height);
    float viewport = pt_max_float(1.0f, tree->height - tree->padding_top -
                                           tree->padding_bottom);
    uint32_t page = (uint32_t)pt_max_float(1.0f, viewport / row_height);
    if (current == pt_list_index_none) {
        if (keysym == XKB_KEY_Down || keysym == XKB_KEY_Page_Down ||
            keysym == XKB_KEY_Home) {
            next = 0;
        } else if (keysym == XKB_KEY_Up || keysym == XKB_KEY_Page_Up ||
                   keysym == XKB_KEY_End) {
            next = state->visible_count - 1;
        } else {
            return false;
        }
    } else {
        pt_tree_node *node = state->visible[current];
        switch (keysym) {
        case XKB_KEY_Down:
            next = current + 1 < state->visible_count ? current + 1 : current;
            break;
        case XKB_KEY_Up:
            next = current > 0 ? current - 1 : 0;
            break;
        case XKB_KEY_Page_Down:
            next = current + page < state->visible_count ? current + page
                                                         : state->visible_count - 1;
            break;
        case XKB_KEY_Page_Up:
            next = current > page ? current - page : 0;
            break;
        case XKB_KEY_Home:
            next = 0;
            break;
        case XKB_KEY_End:
            next = state->visible_count - 1;
            break;
        case XKB_KEY_Right:
            if (node->first_child) {
                if (!node->expanded) {
                    pt_tree_node_set_expanded(node, true);
                } else {
                    pt_tree_select(tree, node->first_child, true);
                }
            }
            return true;
        case XKB_KEY_Left:
            if (node->expanded && node->first_child) {
                pt_tree_node_set_expanded(node, false);
            } else if (node->parent) {
                pt_tree_select(tree, node->parent, true);
            }
            return true;
        case XKB_KEY_F2:
            return pt_tree_begin_rename(tree, node);
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            return pt_tree_activate_selected(tree);
        default:
            return false;
        }
    }
    if (next == current) {
        return true;
    }
    if (shift && current != pt_list_index_none) {
        pt_tree_node *previous = state->selected;
        tree_extend_selection(tree, state, next);
        tree_notify_primary(tree, previous);
    } else {
        pt_tree_select(tree, state->visible[next], true);
    }
    return true;
}

static void tree_apply_marquee(pt_widget *tree, pt_tree_state *state) {
    pt_tree_node *previous = state->selected;
    pt_window *window = pt_widget_window(tree);
    bool additive = window && (window->modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    if (!additive) {
        tree_reset_selection(state);
    }
    uint32_t before = state->selection_count;
    pt_tree_node *top_hit = NULL;
    float top_y = 0.0f;
    for (pt_widget *child = tree->first_child; child; child = child->next_sibling) {
        if (child->kind != pt_widget_tree_row || !child->list_bound || !child->tree_node) {
            continue;
        }
        if (!pt_marquee_hit(&state->marquee,
                           (pt_rect){child->x, child->y, child->width, child->height})) {
            continue;
        }
        if (!child->tree_node->selected) {
            child->tree_node->selected = true;
            ++state->selection_count;
        }
        if (!top_hit || child->y < top_y) {
            top_hit = child->tree_node;
            top_y = child->y;
        }
    }
    if (top_hit) {
        state->selected = top_hit;
        pt_tree_reveal(tree, top_hit);
    }
    tree_apply_selection(tree);
    pt_widget_mark_dirty(tree, true);
    if (state->selection_count != before) {
        pt_widget_a11y_emit(tree, pt_a11y_event_selection_changed);
    }
    tree_notify_primary(tree, previous);
}

void pt_tree_row_down(pt_widget *tree, pt_widget *row, float x, float y) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !row || row->kind != pt_widget_tree_row || !row->tree_node) {
        return;
    }
    pt_tree_node *node = row->tree_node;
    if (node->first_child) {
        float indent = pt_max_float(8.0f, state->indent);
        float left = row->x + (float)node->depth * indent + 4.0f;
        if (x >= left && x <= left + 24.0f && y >= row->y && y <= row->y + row->height) {
            pt_tree_node_set_expanded(node, !node->expanded);
            return;
        }
    }
    uint32_t index = pt_tree_index_of(tree, node);
    if (index == pt_list_index_none) {
        return;
    }
    pt_window *window = pt_widget_window(tree);
    uint32_t modifiers = window ? window->modifiers : 0;
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool ctrl = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    pt_tree_node *previous = state->selected;
    state->shift_select = shift;
    state->toggle_select = ctrl;
    if (shift) {
        tree_extend_selection(tree, state, index);
    } else if (ctrl) {
        pt_tree_select_toggle(tree, node);
    } else {
        state->selection_anchor = node;
    }
    pt_marquee_begin(&state->marquee, x, y);
    pt_widget_mark_dirty(tree, false);
    tree_notify_primary(tree, previous);
}

void pt_tree_row_drag(pt_widget *tree, float x, float y) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    if (pt_marquee_move(&state->marquee, x, y)) {
        tree_apply_marquee(tree, state);
    }
}

bool pt_tree_row_up(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return false;
    }
    bool dragged = pt_marquee_end(&state->marquee);
    bool consumed = dragged || state->shift_select || state->toggle_select;
    state->shift_select = false;
    state->toggle_select = false;
    pt_widget_mark_dirty(tree, false);
    return consumed;
}

/* The tree has the same empty-viewport rule as the list: a press that misses
 * every visible row starts the band and clears the selection unless Ctrl keeps
 * the existing set as the base. */
static bool tree_view_background(const pt_widget *tree, float x, float y) {
    if (!tree || x < tree->x + tree->padding_left ||
        x > tree->x + tree->width - tree->padding_right ||
        y < tree->y + tree->padding_top ||
        y > tree->y + tree->height - tree->padding_bottom) {
        return false;
    }
    for (pt_widget *child = tree->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        if (x >= child->x && x <= child->x + child->width && y >= child->y &&
            y <= child->y + child->height) {
            return false;
        }
    }
    return true;
}

void pt_tree_view_down(pt_widget *tree, float x, float y) {
    pt_tree_state *state = tree_state(tree);
    if (!state || !tree->enabled || !tree->visible || !tree_view_background(tree, x, y)) {
        return;
    }
    pt_window *window = pt_widget_window(tree);
    uint32_t modifiers = window ? window->modifiers : 0;
    bool additive = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    if (!additive) {
        pt_tree_clear_selection(tree);
    }
    state->shift_select = false;
    state->toggle_select = false;
    pt_marquee_begin(&state->marquee, x, y);
    pt_widget_mark_dirty(tree, false);
}

void pt_tree_view_drag(pt_widget *tree, float x, float y) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    if (pt_marquee_move(&state->marquee, x, y)) {
        tree_apply_marquee(tree, state);
    }
    pt_widget_mark_dirty(tree, false);
}

bool pt_tree_view_up(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return false;
    }
    bool dragged = pt_marquee_end(&state->marquee);
    state->shift_select = false;
    state->toggle_select = false;
    pt_widget_mark_dirty(tree, false);
    return dragged;
}

void pt_tree_free_extra(pt_widget *tree) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    tree_free_node(state->first_root);
    free(state->visible);
    free(state);
    tree->extra = NULL;
}

/* ---------- layout ---------- */

void pt_tree_preferred(pt_widget *tree, const pt_layout_ctx *ctx, float *width,
                       float *height) {
    pt_unused(ctx);
    *width = tree->requested_width >= 0.0f ? tree->requested_width : 300.0f;
    *height = tree->requested_height >= 0.0f ? tree->requested_height : 320.0f;
}

void pt_tree_layout(pt_widget *tree, const pt_layout_ctx *ctx, pt_rect inner) {
    pt_tree_state *state = tree_state(tree);
    if (!state) {
        return;
    }
    pt_tree_rebuild(tree);
    pt_rect inherited = tree->clip;
    float row_height = pt_max_float(20.0f, tree->row_height);
    float content_h = (float)state->visible_count * row_height;
    tree->scroll_content_height = content_h;
    float max_scroll = pt_max_float(0.0f, content_h - inner.h);
    tree->scroll_y = pt_clamp_float(tree->scroll_y, 0.0f, max_scroll);
    uint32_t first = row_height > 0.0f ? (uint32_t)(tree->scroll_y / row_height) : 0;
    if (first > state->visible_count) {
        first = state->visible_count;
    }
    uint32_t needed = 0;
    if (first < state->visible_count && inner.h > 0.0f) {
        float visible = (tree->scroll_y + inner.h) / row_height - (float)first;
        needed = (uint32_t)ceilf(pt_max_float(0.0f, visible)) + 1u;
    }
    if (needed > state->visible_count - first) {
        needed = state->visible_count - first;
    }
    uint32_t existing = 0;
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_tree_row) {
            ++existing;
        }
    }
    while (existing < needed) {
        pt_widget_new(tree, pt_widget_tree_row);
        ++existing;
    }
    tree->clip = pt_rect_intersect(inherited, inner);
    uint32_t pool = 0;
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind != pt_widget_tree_row) {
            continue;
        }
        if (pool >= needed) {
            row->visible = false;
            row->list_bound = false;
            row->list_index = pt_list_index_none;
            row->tree_node = NULL;
            ++pool;
            continue;
        }
        uint32_t row_index = first + pool;
        pt_tree_node *node = state->visible[row_index];
        row->visible = true;
        row->list_bound = true;
        row->list_index = row_index;
        row->tree_node = node;
        row->row_selected = node->selected;
        float row_y = inner.y - tree->scroll_y + (float)row_index * row_height;
        pt_widget_layout_child(row, inner.x, row_y, inner.w, row_height, ctx);
        ++pool;
    }
    if (state->rename_editor) {
        pt_widget *target = NULL;
        for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
            if (row->kind == pt_widget_tree_row && row->visible && row->list_bound &&
                row->tree_node == state->rename_node) {
                target = row;
                break;
            }
        }
        if (!target) {
            state->rename_editor->visible = false;
        } else {
            float offset = (float)state->rename_node->depth * state->indent;
            state->rename_editor->visible = true;
            pt_widget_layout_child(state->rename_editor, target->x + offset + 28.0f,
                                   target->y + 2.0f,
                                   pt_max_float(48.0f, target->width - offset - 36.0f),
                                   pt_max_float(20.0f, target->height - 4.0f), ctx);
            state->rename_editor->clip = tree->clip;
        }
    }
}

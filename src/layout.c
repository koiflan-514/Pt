/*
 * Pt flex-style layout engine.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

static bool layout_container(const pt_widget *widget) {
    return widget->kind == pt_widget_box || widget->kind == pt_widget_card ||
           widget->kind == pt_widget_scroll || widget->kind == pt_widget_wrap ||
           widget->kind == pt_widget_grid || widget->kind == pt_widget_list ||
           widget->kind == pt_widget_list_row || widget->kind == pt_widget_table ||
           widget->kind == pt_widget_table_header || widget->kind == pt_widget_table_row ||
           widget->kind == pt_widget_tree || widget->kind == pt_widget_tabs ||
           widget->kind == pt_widget_tab_page || widget->kind == pt_widget_sidebar ||
           widget->kind == pt_widget_combobox;
}

static float content_width(const pt_widget *widget) {
    return pt_max_float(0.0f, widget->width - widget->padding_left - widget->padding_right);
}

static float content_height(const pt_widget *widget) {
    return pt_max_float(0.0f, widget->height - widget->padding_top - widget->padding_bottom);
}

static pt_rect content_rect(const pt_widget *widget) {
    return (pt_rect){widget->x + widget->padding_left, widget->y + widget->padding_top,
                     content_width(widget), content_height(widget)};
}

static float clamp_width(const pt_widget *widget, float width) {
    width = pt_max_float(width, widget->min_width);
    if (widget->max_width < FLT_MAX) {
        width = pt_min_float(width, widget->max_width);
    }
    return pt_max_float(0.0f, width);
}

static float clamp_height(const pt_widget *widget, float height) {
    height = pt_max_float(height, widget->min_height);
    if (widget->max_height < FLT_MAX) {
        height = pt_min_float(height, widget->max_height);
    }
    return pt_max_float(0.0f, height);
}

pt_layout_ctx pt_layout_context_for(const pt_widget *widget) {
    pt_layout_ctx ctx = {1.0f, "sans-serif"};
    pt_window *window = pt_widget_window(widget);
    if (window) {
        if (window->render_scale > 0.0f) {
            ctx.scale = window->render_scale;
        }
        if (window->app) {
            ctx.font_family = window->app->theme.font_family;
        }
    }
    return ctx;
}

static void preferred_size(pt_widget *widget, const pt_layout_ctx *ctx, float avail_width,
                           float avail_height, float *out_width, float *out_height);

static void wrap_preferred(pt_widget *wrap, const pt_layout_ctx *ctx, float avail_width,
                           float *out_width, float *out_height) {
    float inner_width = pt_max_float(0.0f, avail_width - wrap->padding_left -
                                            wrap->padding_right);
    float line_width = 0.0f;
    float line_height = 0.0f;
    float max_width = 0.0f;
    float height = 0.0f;
    int in_line = 0;
    for (pt_widget *child = wrap->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        float child_w = child->requested_width;
        float child_h = child->requested_height;
        if (child_w < 0.0f || child_h < 0.0f) {
            float measured_w = 0.0f;
            float measured_h = 0.0f;
            preferred_size(child, ctx, inner_width, INFINITY, &measured_w, &measured_h);
            if (child_w < 0.0f) {
                child_w = measured_w;
            }
            if (child_h < 0.0f) {
                child_h = measured_h;
            }
        }
        child_w = pt_max_float(0.0f, child_w) + child->margin_left + child->margin_right;
        child_h = pt_max_float(0.0f, child_h) + child->margin_top + child->margin_bottom;
        if (in_line > 0 && line_width + wrap->gap + child_w > inner_width) {
            max_width = pt_max_float(max_width, line_width);
            height += line_height + wrap->row_gap;
            line_width = 0.0f;
            line_height = 0.0f;
            in_line = 0;
        }
        if (in_line > 0) {
            line_width += wrap->gap;
        }
        line_width += child_w;
        line_height = pt_max_float(line_height, child_h);
        ++in_line;
    }
    if (in_line > 0) {
        max_width = pt_max_float(max_width, line_width);
        height += line_height;
    }
    *out_width = max_width + wrap->padding_left + wrap->padding_right;
    *out_height = height + wrap->padding_top + wrap->padding_bottom;
}

static void grid_preferred(pt_widget *grid, const pt_layout_ctx *ctx, float avail_width,
                           float *out_width, float *out_height) {
    uint32_t columns = grid->grid_columns > 0 ? grid->grid_columns : 1;
    float *column_widths = pt_alloc_zeroed(columns, sizeof(*column_widths));
    float inner_width = pt_max_float(0.0f, avail_width - grid->padding_left -
                                            grid->padding_right);
    float cell_width = (inner_width - grid->gap * (float)(columns - 1)) / (float)columns;
    if (cell_width < 0.0f) {
        cell_width = 0.0f;
    }
    float *row_heights = NULL;
    size_t row_count = 0;
    size_t row_capacity = 0;
    uint32_t column = 0;
    float row_height = 0.0f;
    float widest = 0.0f;
    for (pt_widget *child = grid->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        uint32_t span = child->grid_span > 0 ? child->grid_span : 1;
        if (span > columns) {
            span = columns;
        }
        if (column > 0 && column + span > columns) {
            row_count++;
            if (row_count > row_capacity) {
                row_capacity = row_capacity ? row_capacity * 2 : 4;
                row_heights = pt_realloc_mem(row_heights,
                                             row_capacity * sizeof(*row_heights));
            }
            row_heights[row_count - 1] = row_height;
            row_height = 0.0f;
            column = 0;
        }
        float measured_w = 0.0f;
        float measured_h = 0.0f;
        preferred_size(child, ctx, cell_width, INFINITY, &measured_w, &measured_h);
        float child_w = child->requested_width >= 0.0f ? child->requested_width : measured_w;
        float child_h = child->requested_height >= 0.0f ? child->requested_height : measured_h;
        if (span == 1) {
            column_widths[column] = pt_max_float(column_widths[column], child_w);
        }
        row_height = pt_max_float(row_height, child_h + child->margin_top + child->margin_bottom);
        widest = pt_max_float(widest, child_w);
        column += span;
        if (column >= columns) {
            column = 0;
            row_count++;
            if (row_count > row_capacity) {
                row_capacity = row_capacity ? row_capacity * 2 : 4;
                row_heights = pt_realloc_mem(row_heights,
                                             row_capacity * sizeof(*row_heights));
            }
            row_heights[row_count - 1] = row_height;
            row_height = 0.0f;
        }
    }
    if (column > 0) {
        row_count++;
        row_heights = pt_realloc_mem(row_heights, row_count * sizeof(*row_heights));
        row_heights[row_count - 1] = row_height;
    }
    float width = 0.0f;
    for (uint32_t i = 0; i < columns; ++i) {
        if (column_widths[i] > 0.0f) {
            width += column_widths[i];
        } else {
            width += widest;
        }
    }
    width += grid->gap * (float)(columns - 1);
    float height = 0.0f;
    for (size_t i = 0; i < row_count; ++i) {
        height += row_heights[i];
        if (i + 1 < row_count) {
            height += grid->row_gap;
        }
    }
    free(column_widths);
    free(row_heights);
    *out_width = width + grid->padding_left + grid->padding_right;
    *out_height = height + grid->padding_top + grid->padding_bottom;
}

static void preferred_size(pt_widget *widget, const pt_layout_ctx *ctx, float avail_width,
                           float avail_height, float *out_width, float *out_height) {
    if (!widget->visible) {
        *out_width = 0.0f;
        *out_height = 0.0f;
        return;
    }
    float padding_w = widget->padding_left + widget->padding_right;
    float padding_h = widget->padding_top + widget->padding_bottom;
    float width = 0.0f;
    float height = 0.0f;
    pt_font *font = NULL;
    const char *text = widget->text ? widget->text : "";
    size_t text_len = strlen(text);
    float scale = ctx->scale > 0.0f ? ctx->scale : 1.0f;
    float logical_avail = pt_max_float(0.0f, avail_width - padding_w);
    if (widget->requested_width >= 0.0f) {
        logical_avail = pt_max_float(0.0f, widget->requested_width - padding_w);
    }

    switch (widget->kind) {
    case pt_widget_label: {
        if (widget->span_count > 0) {
            pt_text_runs runs = {0};
            if (pt_text_runs_build(widget, ctx, &runs)) {
                float natural = pt_text_runs_natural_width(&runs);
                if (widget->wrap && logical_avail > 0.0f) {
                    width = pt_min_float(natural, logical_avail);
                    pt_text_line *lines = NULL;
                    int line_count =
                        pt_text_runs_wrap(&runs, pt_max_float(1.0f, width), &lines);
                    height = (float)line_count * runs.line_height;
                    free(lines);
                } else {
                    width = natural;
                    height = runs.line_height;
                }
                pt_text_runs_free(&runs);
                width += padding_w;
                height += padding_h;
                break;
            }
        }
        font = pt_widget_font_for(widget, ctx);
        if (!font) {
            break;
        }
        float line_height = font->line_height / scale;
        float natural = pt_font_measure(font, text, text_len) / scale;
        if (widget->wrap && logical_avail > 0.0f) {
            width = pt_min_float(natural, logical_avail);
            height = pt_font_wrapped_height(font, text, width * scale) / scale;
        } else {
            width = natural;
            height = line_height;
        }
        width += padding_w;
        height += padding_h;
        break;
    }
    case pt_widget_button: {
        font = pt_widget_font_for(widget, ctx);
        float line_height = font ? font->line_height / scale : pt_default_font_size * 1.3f;
        width = (font ? pt_font_measure(font, text, text_len) / scale : 0.0f) + padding_w;
        width = pt_max_float(width, 64.0f);
        height = line_height + padding_h;
        break;
    }
    case pt_widget_entry: {
        font = pt_widget_font_for(widget, ctx);
        float line_height = font ? font->line_height / scale : pt_default_font_size * 1.3f;
        width = 220.0f + padding_w;
        height = line_height + padding_h;
        break;
    }
    case pt_widget_toggle:
    case pt_widget_slider:
    case pt_widget_divider:
    case pt_widget_progress:
    case pt_widget_spinner:
    case pt_widget_spacer:
        width = widget->requested_width >= 0.0f ? widget->requested_width : 1.0f;
        height = widget->requested_height >= 0.0f ? widget->requested_height : 1.0f;
        break;
    case pt_widget_icon:
        width = widget->requested_width >= 0.0f ? widget->requested_width : widget->icon_size;
        height = widget->requested_height >= 0.0f ? widget->requested_height : widget->icon_size;
        width += padding_w;
        height += padding_h;
        break;
    case pt_widget_checkbox:
    case pt_widget_radio: {
        font = pt_widget_font_for(widget, ctx);
        float line_height = font ? font->line_height / scale : pt_default_font_size * 1.3f;
        float natural = font ? pt_font_measure(font, text, text_len) / scale : 0.0f;
        width = 18.0f + 10.0f + natural + padding_w;
        height = pt_max_float(18.0f, line_height) + padding_h;
        break;
    }
    case pt_widget_segmented: {
        if (widget->requested_width >= 0.0f) {
            width = widget->requested_width;
        } else {
            font = pt_widget_font_for(widget, ctx);
            width = 6.0f;
            for (uint32_t i = 0; i < widget->segment_count; ++i) {
                const char *label = widget->segment_labels && widget->segment_labels[i]
                                        ? widget->segment_labels[i]
                                        : "";
                float natural = font ? pt_font_measure(font, label, strlen(label)) / scale
                                     : 0.0f;
                width += pt_max_float(natural + 28.0f, 56.0f);
            }
        }
        height = widget->requested_height >= 0.0f ? widget->requested_height : 34.0f;
        width += padding_w;
        height += padding_h;
        break;
    }
    case pt_widget_table:
        pt_table_preferred(widget, ctx, &width, &height);
        width += padding_w;
        height += padding_h;
        break;
    case pt_widget_tree:
        pt_tree_preferred(widget, ctx, &width, &height);
        width += padding_w;
        height += padding_h;
        break;
    case pt_widget_tabs:
        pt_tabs_preferred(widget, ctx, &width, &height);
        width += padding_w;
        height += padding_h;
        break;
    case pt_widget_sidebar:
        pt_sidebar_preferred(widget, ctx, &width, &height);
        width += padding_w;
        height += padding_h;
        break;
    case pt_widget_scroll:
        width = (widget->requested_width >= 0.0f ? widget->requested_width : 240.0f) + padding_w;
        height = (widget->requested_height >= 0.0f ? widget->requested_height : 200.0f) + padding_h;
        break;
    case pt_widget_text_area: {
        float area_width = widget->requested_width >= 0.0f ? widget->requested_width : 320.0f;
        width = area_width + padding_w;
        if (widget->grow_to_content) {
            height = pt_text_area_measure_height(widget, ctx, area_width) + padding_h;
        } else {
            height = (widget->requested_height >= 0.0f ? widget->requested_height : 160.0f) +
                     padding_h;
        }
        break;
    }
    case pt_widget_wrap:
        wrap_preferred(widget, ctx, avail_width, &width, &height);
        break;
    case pt_widget_grid:
        grid_preferred(widget, ctx, avail_width, &width, &height);
        break;
    case pt_widget_list:
        width = (widget->requested_width >= 0.0f ? widget->requested_width : 320.0f) + padding_w;
        height = (widget->requested_height >= 0.0f ? widget->requested_height : 260.0f) +
                 padding_h;
        break;
    case pt_widget_table_cell:
    case pt_widget_table_header_cell: {
        font = pt_widget_font_for(widget, ctx);
        float line_height = font ? font->line_height / scale : pt_default_font_size * 1.3f;
        width = widget->requested_width >= 0.0f ? widget->requested_width
                                                : 120.0f + padding_w;
        height = line_height + padding_h;
        break;
    }
    case pt_widget_tree_row:
    case pt_widget_sidebar_item: {
        font = pt_widget_font_for(widget, ctx);
        float line_height = font ? font->line_height / scale : pt_default_font_size * 1.3f;
        float natural = font ? pt_font_measure(font, text, text_len) / scale : 0.0f;
        width = (widget->kind == pt_widget_tree_row ? 240.0f : natural + 60.0f) + padding_w;
        height = pt_max_float(28.0f, line_height) + padding_h;
        break;
    }
    case pt_widget_chip: {
        font = pt_widget_font_for(widget, ctx);
        float line_height = font ? font->line_height / scale : pt_default_font_size * 1.3f;
        float natural = font ? pt_font_measure(font, text, text_len) / scale : 0.0f;
        float extra = widget->icon != pt_icon_none ? 20.0f : 0.0f;
        float remove = widget->chip_removable ? 26.0f : 0.0f;
        width = natural + extra + remove + padding_w;
        height = pt_max_float(line_height + 8.0f,
                              pt_widget_theme_metric_value(widget, pt_theme_metric_chip_height)) +
                 padding_h;
        break;
    }
    case pt_widget_skeleton:
        width = widget->requested_width >= 0.0f ? widget->requested_width : 120.0f;
        height = widget->requested_height >= 0.0f ? widget->requested_height : 12.0f;
        break;
    case pt_widget_combobox:
        pt_combobox_preferred(widget, ctx, &width, &height);
        break;
    case pt_widget_box:
    case pt_widget_card:
    case pt_widget_list_row:
    case pt_widget_table_header:
    case pt_widget_table_row:
    case pt_widget_tab_page: {
        float inner_avail_w = pt_max_float(0.0f, avail_width - padding_w);
        float inner_avail_h = pt_max_float(0.0f, avail_height - padding_h);
        float main = 0.0f;
        float cross = 0.0f;
        int visible_count = 0;
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            if (!child->visible) {
                continue;
            }
            float child_w = 0.0f;
            float child_h = 0.0f;
            preferred_size(child, ctx, inner_avail_w, inner_avail_h, &child_w, &child_h);
            if (widget->direction == pt_direction_row) {
                main += child_w + child->margin_left + child->margin_right;
                cross = pt_max_float(cross, child_h + child->margin_top + child->margin_bottom);
            } else {
                main += child_h + child->margin_top + child->margin_bottom;
                cross = pt_max_float(cross, child_w + child->margin_left + child->margin_right);
            }
            ++visible_count;
        }
        if (visible_count > 1) {
            main += widget->gap * (float)(visible_count - 1);
        }
        if (widget->direction == pt_direction_row) {
            width = main + padding_w;
            height = cross + padding_h;
        } else {
            width = cross + padding_w;
            height = main + padding_h;
        }
        break;
    }
    }

    if (widget->requested_width >= 0.0f) {
        width = widget->requested_width;
    }
    /* Content-sized text areas ignore an explicit height; the wrapped text wins. */
    bool grows_in_height = widget->kind == pt_widget_text_area && widget->grow_to_content;
    if (widget->requested_height >= 0.0f && !grows_in_height) {
        height = widget->requested_height;
    }
    *out_width = clamp_width(widget, width);
    *out_height = clamp_height(widget, height);
}

static pt_align effective_align(const pt_widget *parent, const pt_widget *child) {
    pt_align align = child->align_self;
    if (align == pt_align_auto) {
        align = parent->align_items;
    }
    if (align == pt_align_auto) {
        align = pt_align_stretch;
    }
    return align;
}

static float child_width_for(const pt_widget *parent, pt_widget *child, const pt_layout_ctx *ctx,
                             float inner_width, float inner_height) {
    if (child->requested_width >= 0.0f) {
        return child->requested_width;
    }
    pt_align align = effective_align(parent, child);
    float available = pt_max_float(0.0f, inner_width - child->margin_left - child->margin_right);
    if (align == pt_align_stretch && parent->direction == pt_direction_column) {
        return available;
    }
    float width = 0.0f;
    float height = 0.0f;
    preferred_size(child, ctx, available, inner_height, &width, &height);
    return pt_min_float(width, available);
}

static float child_height_for(const pt_widget *parent, pt_widget *child, const pt_layout_ctx *ctx,
                              float inner_width, float inner_height, float assigned_width) {
    bool grows_in_height = child->kind == pt_widget_text_area && child->grow_to_content;
    if (child->requested_height >= 0.0f && !grows_in_height) {
        return child->requested_height;
    }
    pt_align align = effective_align(parent, child);
    if (align == pt_align_stretch && parent->direction == pt_direction_row) {
        return pt_max_float(0.0f,
                            inner_height - child->margin_top - child->margin_bottom);
    }
    float width = 0.0f;
    float height = 0.0f;
    float measure_width = assigned_width > 0.0f ? assigned_width : inner_width;
    preferred_size(child, ctx, measure_width, inner_height, &width, &height);
    return height;
}

static void layout_widget_internal(pt_widget *widget, float x, float y, float width,
                                   float height, const pt_layout_ctx *ctx);

static void layout_flex_children(pt_widget *parent, const pt_layout_ctx *ctx, pt_rect inner) {
    bool row = parent->direction == pt_direction_row;
    int count = 0;
    float total_gap = 0.0f;
    float total_margin_main = 0.0f;
    float total_margin_cross = 0.0f;
    float total_grow = 0.0f;

    for (pt_widget *child = parent->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        ++count;
        if (row) {
            total_margin_main += child->margin_left + child->margin_right;
            total_margin_cross = pt_max_float(total_margin_cross,
                                              child->margin_top + child->margin_bottom);
        } else {
            total_margin_main += child->margin_top + child->margin_bottom;
            total_margin_cross = pt_max_float(total_margin_cross,
                                              child->margin_left + child->margin_right);
        }
        total_grow += child->grow;
    }
    if (count == 0) {
        return;
    }
    total_gap = parent->gap * (float)(count - 1);
    float available_main = (row ? inner.w : inner.h) - total_margin_main - total_gap;

    float *widths = pt_alloc_zeroed((size_t)count, sizeof(*widths));
    float *heights = pt_alloc_zeroed((size_t)count, sizeof(*heights));
    pt_widget **children = pt_alloc_zeroed((size_t)count, sizeof(*children));
    float *base_main = pt_alloc_zeroed((size_t)count, sizeof(*base_main));

    int index = 0;
    float sum_main = 0.0f;
    for (pt_widget *child = parent->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        children[index] = child;
        float child_w = child_width_for(parent, child, ctx, inner.w, inner.h);
        float child_h = child_height_for(parent, child, ctx, inner.w, inner.h, child_w);
        base_main[index] = row ? child_w : child_h;
        widths[index] = child_w;
        heights[index] = child_h;
        sum_main += base_main[index];
        ++index;
    }

    float extra = available_main - sum_main;
    if (extra > 0.0f && total_grow > 0.0f) {
        for (int i = 0; i < count; ++i) {
            float share = extra * (children[i]->grow / total_grow);
            if (row) {
                widths[i] += share;
            } else {
                heights[i] += share;
            }
            base_main[i] += share;
        }
        extra = 0.0f;
    }

    for (int i = 0; i < count; ++i) {
        pt_widget *child = children[i];
        widths[i] = clamp_width(child, widths[i]);
        heights[i] = clamp_height(child, heights[i]);
        if (row) {
            float max_height = pt_max_float(0.0f, inner.h - child->margin_top -
                                                       child->margin_bottom);
            if (heights[i] > max_height) {
                heights[i] = max_height;
            }
        } else {
            float max_width = pt_max_float(0.0f, inner.w - child->margin_left -
                                                      child->margin_right);
            if (widths[i] > max_width) {
                widths[i] = max_width;
            }
        }
    }

    float used = total_margin_main + total_gap;
    for (int i = 0; i < count; ++i) {
        used += row ? widths[i] : heights[i];
    }
    float free_space = (row ? inner.w : inner.h) - used;
    if (free_space < 0.0f) {
        free_space = 0.0f;
    }
    float offset = 0.0f;
    float between = parent->gap;
    switch (parent->justify) {
    case pt_justify_center:
        offset = free_space * 0.5f;
        break;
    case pt_justify_end:
        offset = free_space;
        break;
    case pt_justify_space_between:
        if (count > 1) {
            between += free_space / (float)(count - 1);
        }
        break;
    case pt_justify_space_around:
        if (count > 0) {
            float unit = free_space / (float)count;
            offset = unit * 0.5f;
            between += unit;
        }
        break;
    case pt_justify_space_evenly:
        if (count > 0) {
            float unit = free_space / (float)(count + 1);
            offset = unit;
            between += unit;
        }
        break;
    case pt_justify_start:
    default:
        break;
    }

    float cursor = (row ? inner.x : inner.y) + offset;
    for (int i = 0; i < count; ++i) {
        pt_widget *child = children[i];
        float child_w = widths[i];
        float child_h = heights[i];
        float child_x;
        float child_y;
        pt_align align = effective_align(parent, child);
        if (row) {
            child_x = cursor + child->margin_left;
            float start = inner.y + child->margin_top;
            float end = inner.y + inner.h - child->margin_bottom - child_h;
            if (align == pt_align_center) {
                child_y = inner.y + (inner.h - child_h) * 0.5f;
            } else if (align == pt_align_end) {
                child_y = end;
            } else {
                child_y = start;
            }
            if (align == pt_align_stretch) {
                child_h = pt_max_float(0.0f, inner.h - child->margin_top -
                                                 child->margin_bottom);
            }
            cursor += child->margin_left + child_w + child->margin_right + between;
        } else {
            child_y = cursor + child->margin_top;
            float start = inner.x + child->margin_left;
            float end = inner.x + inner.w - child->margin_right - child_w;
            if (align == pt_align_center) {
                child_x = inner.x + (inner.w - child_w) * 0.5f;
            } else if (align == pt_align_end) {
                child_x = end;
            } else {
                child_x = start;
            }
            if (align == pt_align_stretch) {
                child_w = pt_max_float(0.0f, inner.w - child->margin_left -
                                                     child->margin_right);
                child_x = start;
            }
            cursor += child->margin_top + child_h + child->margin_bottom + between;
        }
        layout_widget_internal(child, child_x, child_y, child_w, child_h, ctx);
    }

    free(children);
    free(widths);
    free(heights);
    free(base_main);
}

static float justify_offset(pt_justify justify, float free_space, int count,
                            float *out_between, float gap) {
    float offset = 0.0f;
    float between = gap;
    switch (justify) {
    case pt_justify_center:
        offset = free_space * 0.5f;
        break;
    case pt_justify_end:
        offset = free_space;
        break;
    case pt_justify_space_between:
        if (count > 1) {
            between += free_space / (float)(count - 1);
        }
        break;
    case pt_justify_space_around:
        if (count > 0) {
            float unit = free_space / (float)count;
            offset = unit * 0.5f;
            between += unit;
        }
        break;
    case pt_justify_space_evenly:
        if (count > 0) {
            float unit = free_space / (float)(count + 1);
            offset = unit;
            between += unit;
        }
        break;
    case pt_justify_start:
    default:
        break;
    }
    if (out_between) {
        *out_between = between;
    }
    return offset;
}

static void layout_wrap_children(pt_widget *wrap, const pt_layout_ctx *ctx, pt_rect inner) {
    int count = 0;
    for (pt_widget *child = wrap->first_child; child; child = child->next_sibling) {
        if (child->visible) {
            ++count;
        }
    }
    if (count == 0) {
        wrap->scroll_content_height = 0.0f;
        return;
    }
    pt_widget **children = pt_alloc_zeroed((size_t)count, sizeof(*children));
    float *widths = pt_alloc_zeroed((size_t)count, sizeof(*widths));
    float *heights = pt_alloc_zeroed((size_t)count, sizeof(*heights));
    int *line_of = pt_alloc_zeroed((size_t)count, sizeof(*line_of));
    int index = 0;
    int line = 0;
    float line_width = 0.0f;
    int in_line = 0;
    for (pt_widget *child = wrap->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        float child_w = 0.0f;
        float child_h = 0.0f;
        preferred_size(child, ctx, inner.w, inner.h, &child_w, &child_h);
        if (child->requested_width >= 0.0f) {
            child_w = child->requested_width;
        }
        if (child->requested_height >= 0.0f) {
            child_h = child->requested_height;
        }
        child_w += child->margin_left + child->margin_right;
        child_h += child->margin_top + child->margin_bottom;
        if (in_line > 0 && line_width + wrap->gap + child_w > inner.w) {
            ++line;
            line_width = 0.0f;
            in_line = 0;
        }
        if (in_line > 0) {
            line_width += wrap->gap;
        }
        children[index] = child;
        widths[index] = child_w;
        heights[index] = child_h;
        line_of[index] = line;
        line_width += child_w;
        ++in_line;
        ++index;
    }

    int start = 0;
    float cursor_y = inner.y;
    while (start < count) {
        int end = start;
        float used = 0.0f;
        float line_height = 0.0f;
        while (end < count && line_of[end] == line_of[start]) {
            used += widths[end];
            line_height = pt_max_float(line_height, heights[end]);
            if (end > start) {
                used += wrap->gap;
            }
            ++end;
        }
        int line_count = end - start;
        float free_space = pt_max_float(0.0f, inner.w - used);
        float between = wrap->gap;
        float offset = justify_offset(wrap->justify, free_space, line_count, &between,
                                      wrap->gap);
        float cursor_x = inner.x + offset;
        for (int i = start; i < end; ++i) {
            pt_widget *child = children[i];
            float child_w = widths[i] - child->margin_left - child->margin_right;
            float child_h = heights[i] - child->margin_top - child->margin_bottom;
            pt_align align = effective_align(wrap, child);
            if (align == pt_align_stretch) {
                child_h = pt_max_float(0.0f, line_height - child->margin_top -
                                                 child->margin_bottom);
            }
            float child_y = cursor_y + child->margin_top;
            if (align == pt_align_center) {
                child_y = cursor_y + (line_height - child_h) * 0.5f;
            } else if (align == pt_align_end) {
                child_y = cursor_y + line_height - child->margin_bottom - child_h;
            }
            pt_layout_ctx local = *ctx;
            layout_widget_internal(child, cursor_x + child->margin_left, child_y, child_w,
                                   child_h, &local);
            cursor_x += widths[i] + between;
        }
        cursor_y += line_height + wrap->row_gap;
        start = end;
    }
    wrap->scroll_content_height = pt_max_float(0.0f, cursor_y - inner.y - wrap->row_gap);
    free(children);
    free(widths);
    free(heights);
    free(line_of);
}

typedef struct grid_cell {
    pt_widget *child;
    uint32_t column;
    uint32_t span;
    uint32_t row;
} grid_cell;

static void layout_grid_children(pt_widget *grid, const pt_layout_ctx *ctx, pt_rect inner) {
    uint32_t columns = grid->grid_columns > 0 ? grid->grid_columns : 1;
    float column_width = (inner.w - grid->gap * (float)(columns - 1)) / (float)columns;
    if (column_width < 0.0f) {
        column_width = 0.0f;
    }
    int count = 0;
    for (pt_widget *child = grid->first_child; child; child = child->next_sibling) {
        if (child->visible) {
            ++count;
        }
    }
    if (count == 0) {
        return;
    }
    grid_cell *cells = pt_alloc_zeroed((size_t)count, sizeof(*cells));
    uint32_t row = 0;
    uint32_t column = 0;
    int index = 0;
    uint32_t row_count = 0;
    for (pt_widget *child = grid->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        uint32_t span = child->grid_span > 0 ? child->grid_span : 1;
        if (span > columns) {
            span = columns;
        }
        if (column > 0 && column + span > columns) {
            ++row;
            column = 0;
        }
        cells[index].child = child;
        cells[index].column = column;
        cells[index].span = span;
        cells[index].row = row;
        ++index;
        column += span;
        if (column >= columns) {
            ++row;
            column = 0;
        }
    }
    row_count = row + (column > 0 ? 1u : 0u);
    float *row_heights = pt_alloc_zeroed(row_count, sizeof(*row_heights));
    for (int i = 0; i < count; ++i) {
        pt_widget *child = cells[i].child;
        float cell_width = column_width * (float)cells[i].span +
                           grid->gap * (float)(cells[i].span - 1);
        float child_w = 0.0f;
        float child_h = 0.0f;
        preferred_size(child, ctx, cell_width, inner.h, &child_w, &child_h);
        if (child->requested_height >= 0.0f) {
            child_h = child->requested_height;
        }
        float needed = child_h + child->margin_top + child->margin_bottom;
        if (needed > row_heights[cells[i].row]) {
            row_heights[cells[i].row] = needed;
        }
    }
    float *row_tops = pt_alloc_zeroed(row_count, sizeof(*row_tops));
    float y = inner.y;
    for (uint32_t i = 0; i < row_count; ++i) {
        row_tops[i] = y;
        y += row_heights[i];
        if (i + 1 < row_count) {
            y += grid->row_gap;
        }
    }
    for (int i = 0; i < count; ++i) {
        pt_widget *child = cells[i].child;
        float cell_width = column_width * (float)cells[i].span +
                           grid->gap * (float)(cells[i].span - 1);
        float child_w = 0.0f;
        float child_h = 0.0f;
        preferred_size(child, ctx, cell_width, inner.h, &child_w, &child_h);
        pt_align align_x = effective_align(grid, child);
        pt_align align_y = align_x;
        if (child->requested_width >= 0.0f) {
            child_w = child->requested_width;
        } else if (align_x == pt_align_stretch) {
            child_w = pt_max_float(0.0f, cell_width - child->margin_left -
                                             child->margin_right);
        }
        if (child->requested_height >= 0.0f) {
            child_h = child->requested_height;
        } else if (align_y == pt_align_stretch) {
            child_h = pt_max_float(0.0f, row_heights[cells[i].row] - child->margin_top -
                                             child->margin_bottom);
        }
        child_w = pt_min_float(child_w, pt_max_float(0.0f, cell_width - child->margin_left -
                                                               child->margin_right));
        child_h = pt_min_float(child_h, pt_max_float(0.0f, row_heights[cells[i].row] -
                                                               child->margin_top -
                                                               child->margin_bottom));
        float cell_x = inner.x + (float)cells[i].column * (column_width + grid->gap);
        float child_x = cell_x + child->margin_left;
        if (align_x == pt_align_center) {
            child_x = cell_x + (cell_width - child_w) * 0.5f;
        } else if (align_x == pt_align_end) {
            child_x = cell_x + cell_width - child->margin_right - child_w;
        }
        float child_y = row_tops[cells[i].row] + child->margin_top;
        if (align_y == pt_align_center) {
            child_y = row_tops[cells[i].row] +
                      (row_heights[cells[i].row] - child_h) * 0.5f;
        } else if (align_y == pt_align_end) {
            child_y = row_tops[cells[i].row] + row_heights[cells[i].row] -
                      child->margin_bottom - child_h;
        }
        pt_layout_ctx local = *ctx;
        layout_widget_internal(child, child_x, child_y, child_w, child_h, &local);
    }
    free(row_tops);
    free(row_heights);
    free(cells);
}

static int list_child_count(const pt_widget *list) {
    int count = 0;
    for (const pt_widget *row = list->first_child; row; row = row->next_sibling) {
        ++count;
    }
    return count;
}

static pt_widget *list_child_at(pt_widget *list, int index) {
    pt_widget *row = list->first_child;
    while (row && index > 0) {
        row = row->next_sibling;
        --index;
    }
    return row;
}

static void layout_list_children(pt_widget *list, const pt_layout_ctx *ctx, pt_rect inner) {
    float row_height = pt_max_float(1.0f, list->row_height);
    float content_height = (float)list->row_count * row_height;
    list->scroll_content_height = content_height;
    float max_scroll = pt_max_float(0.0f, content_height - inner.h);
    list->scroll_y = pt_clamp_float(list->scroll_y, 0.0f, max_scroll);
    uint32_t first = row_height > 0.0f ? (uint32_t)(list->scroll_y / row_height) : 0;
    if (first > list->row_count) {
        first = list->row_count;
    }
    uint32_t needed = 0;
    if (first < list->row_count && inner.h > 0.0f) {
        float visible = (list->scroll_y + inner.h) / row_height - (float)first;
        needed = (uint32_t)ceilf(pt_max_float(0.0f, visible)) + 1u;
    }
    if (needed > list->row_count - first) {
        needed = list->row_count - first;
    }
    int existing = list_child_count(list);
    while ((uint32_t)existing < needed) {
        pt_widget_new(list, pt_widget_list_row);
        ++existing;
    }
    for (int i = 0; i < existing; ++i) {
        pt_widget *row = list_child_at(list, i);
        if (!row) {
            break;
        }
        if ((uint32_t)i >= needed) {
            row->visible = false;
            row->list_bound = false;
            row->list_index = pt_list_index_none;
            continue;
        }
        uint32_t row_index = first + (uint32_t)i;
        row->visible = true;
        if (!row->list_bound || row->list_index != row_index) {
            row->list_index = row_index;
            row->list_bound = true;
            row->row_selected = row_index == list->list_selected;
            if (list->on_bind_row) {
                list->on_bind_row(list, row, row_index, list->on_bind_row_data);
            }
        }
        row->row_selected = row_index == list->list_selected;
        float row_y = inner.y - list->scroll_y + (float)row_index * row_height;
        layout_widget_internal(row, inner.x, row_y, inner.w, row_height, ctx);
    }
}

static void layout_scroll_children(pt_widget *scroll, const pt_layout_ctx *ctx, pt_rect inner) {
    float gutter = 10.0f;
    float content_w = pt_max_float(0.0f, inner.w - gutter);
    float total_height = 0.0f;
    int count = 0;
    for (pt_widget *child = scroll->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        if (count > 0) {
            total_height += scroll->gap;
        }
        float child_w = child_width_for(scroll, child, ctx, content_w, inner.h);
        float child_h = child_height_for(scroll, child, ctx, content_w, inner.h, child_w);
        total_height += child->margin_top + child_h + child->margin_bottom;
        ++count;
    }
    scroll->scroll_content_height = pt_max_float(total_height, inner.h);
    float max_scroll = pt_max_float(0.0f, scroll->scroll_content_height - inner.h);
    scroll->scroll_y = pt_clamp_float(scroll->scroll_y, 0.0f, max_scroll);

    float cursor = inner.y - scroll->scroll_y;
    for (pt_widget *child = scroll->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        float child_w = child_width_for(scroll, child, ctx, content_w, inner.h);
        float child_h = child_height_for(scroll, child, ctx, content_w, inner.h, child_w);
        float child_x = inner.x + child->margin_left;
        pt_align align = effective_align(scroll, child);
        if (align == pt_align_center) {
            child_x = inner.x + (content_w - child_w) * 0.5f;
        } else if (align == pt_align_end) {
            child_x = inner.x + content_w - child_w;
        }
        layout_widget_internal(child, child_x, cursor + child->margin_top, child_w, child_h,
                               ctx);
        cursor += child->margin_top + child_h + child->margin_bottom + scroll->gap;
    }
}

static void layout_widget_internal(pt_widget *widget, float x, float y, float width,
                                   float height, const pt_layout_ctx *ctx) {
    if (!widget->visible) {
        widget->x = x;
        widget->y = y;
        widget->width = 0.0f;
        widget->height = 0.0f;
        widget->clip = (pt_rect){x, y, 0.0f, 0.0f};
        return;
    }
    width = clamp_width(widget, width);
    height = clamp_height(widget, height);
    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    pt_rect inherited = {0.0f, 0.0f, 0.0f, 0.0f};
    if (widget->parent) {
        inherited = widget->parent->clip;
    } else {
        inherited = (pt_rect){x, y, width, height};
    }
    widget->clip = inherited;

    if (widget->kind == pt_widget_text_area) {
        pt_rect inner = content_rect(widget);
        widget->clip = pt_rect_intersect(inherited, inner);
        float content = pt_text_area_measure_height(widget, ctx, inner.w);
        if (widget->grow_to_content) {
            widget->scroll_y = 0.0f;
        } else {
            float max_scroll = pt_max_float(0.0f, content - inner.h);
            widget->scroll_y = pt_clamp_float(widget->scroll_y, 0.0f, max_scroll);
        }
        widget->scroll_content_height = content;
        return;
    }

    if (!layout_container(widget)) {
        return;
    }
    pt_rect inner = content_rect(widget);
    switch (widget->kind) {
    case pt_widget_scroll:
        widget->clip = pt_rect_intersect(inherited, inner);
        layout_scroll_children(widget, ctx, inner);
        break;
    case pt_widget_wrap:
        layout_wrap_children(widget, ctx, inner);
        break;
    case pt_widget_grid:
        layout_grid_children(widget, ctx, inner);
        break;
    case pt_widget_list:
        widget->clip = pt_rect_intersect(inherited, inner);
        layout_list_children(widget, ctx, inner);
        break;
    case pt_widget_table:
        widget->clip = pt_rect_intersect(inherited, inner);
        pt_table_layout(widget, ctx, inner);
        break;
    case pt_widget_tree:
        widget->clip = pt_rect_intersect(inherited, inner);
        pt_tree_layout(widget, ctx, inner);
        break;
    case pt_widget_tabs:
        widget->clip = pt_rect_intersect(inherited, inner);
        pt_tabs_layout(widget, ctx, inner);
        break;
    case pt_widget_sidebar:
        widget->clip = pt_rect_intersect(inherited, inner);
        pt_sidebar_layout(widget, ctx, inner);
        break;
    case pt_widget_combobox:
        pt_combobox_layout(widget, ctx, inner);
        break;
    default:
        layout_flex_children(widget, ctx, inner);
        break;
    }
}

void pt_widget_layout_child(pt_widget *widget, float x, float y, float width, float height,
                            const pt_layout_ctx *ctx) {
    if (!widget) {
        return;
    }
    pt_layout_ctx local = ctx ? *ctx : pt_layout_context_for(widget);
    layout_widget_internal(widget, x, y, width, height, &local);
}

void pt_widget_measure_child(pt_widget *widget, const pt_layout_ctx *ctx, float avail_width,
                             float avail_height, float *out_width, float *out_height) {
    if (!widget) {
        if (out_width) {
            *out_width = 0.0f;
        }
        if (out_height) {
            *out_height = 0.0f;
        }
        return;
    }
    pt_layout_ctx local = ctx ? *ctx : pt_layout_context_for(widget);
    float width = 0.0f;
    float height = 0.0f;
    preferred_size(widget, &local, avail_width, avail_height, &width, &height);
    if (out_width) {
        *out_width = width;
    }
    if (out_height) {
        *out_height = height;
    }
}

void pt_widget_layout(pt_widget *root, float x, float y, float width, float height,
                      const pt_layout_ctx *ctx) {
    if (!root) {
        return;
    }
    pt_layout_ctx local = ctx ? *ctx : pt_layout_context_for(root);
    layout_widget_internal(root, x, y, width, height, &local);
}

void pt_widget_measure(pt_widget *widget, const pt_layout_ctx *ctx, float available_width,
                       float available_height, float *out_width, float *out_height) {
    if (!widget) {
        if (out_width) {
            *out_width = 0.0f;
        }
        if (out_height) {
            *out_height = 0.0f;
        }
        return;
    }
    pt_layout_ctx local = ctx ? *ctx : pt_layout_context_for(widget);
    float width = 0.0f;
    float height = 0.0f;
    preferred_size(widget, &local, available_width, available_height, &width, &height);
    if (out_width) {
        *out_width = width;
    }
    if (out_height) {
        *out_height = height;
    }
}

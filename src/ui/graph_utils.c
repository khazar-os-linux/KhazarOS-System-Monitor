#include "ui/graph_utils.h"
#include <string.h>

void graph_get_theme_colors(GtkWidget *widget,
                            GdkRGBA *bg,
                            GdkRGBA *fg,
                            GdkRGBA *accent,
                            const gchar *accent_fallback) {
    if (!widget || !bg || !fg || !accent) return;

    GtkStyleContext *style = gtk_widget_get_style_context(widget);

    // Defaults
    gdk_rgba_parse(bg, "rgb(24, 25, 26)");
    gdk_rgba_parse(fg, "rgb(238, 238, 236)");
    if (accent_fallback) {
        gdk_rgba_parse(accent, accent_fallback);
    } else {
        gdk_rgba_parse(accent, "rgb(53, 132, 228)");
    }

    // Theme overrides
    gtk_style_context_lookup_color(style, "theme_bg_color", bg);
    gtk_style_context_lookup_color(style, "theme_fg_color", fg);
    gtk_style_context_lookup_color(style, "theme_selected_bg_color", accent);
}

void graph_draw_background(cairo_t *cr, int width, int height, GdkRGBA *bg_color) {
    if (!cr || !bg_color) return;
    gdk_cairo_set_source_rgba(cr, bg_color);
    cairo_paint(cr);
}

void graph_draw_grid(cairo_t *cr, int width, int height, GdkRGBA *fg_color) {
    if (!cr || !fg_color) return;

    GdkRGBA grid_color = *fg_color;
    grid_color.alpha = 0.2;

    gdk_cairo_set_source_rgba(cr, &grid_color);
    cairo_set_line_width(cr, 0.8);

    for (int i = 1; i < 4; i++) {
        double y = height * i / 4.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
    }
    cairo_stroke(cr);
}

void graph_draw_fill(cairo_t *cr, int width, int height,
                     const gdouble *history, gint history_idx,
                     gint num_points, GdkRGBA *accent_color) {
    if (!cr || !history || !accent_color || num_points <= 1) return;

    cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(fill, 0,
                                      accent_color->red, accent_color->green, accent_color->blue, 0.7);
    cairo_pattern_add_color_stop_rgba(fill, 1,
                                      accent_color->red, accent_color->green, accent_color->blue, 0.1);
    cairo_set_source(cr, fill);

    cairo_move_to(cr, 0, height);
    for (int i = 0; i < num_points; i++) {
        int idx = (history_idx + i) % num_points;
        double x = (double)i / (num_points - 1) * width;
        double y = height - (history[idx] / 100.0 * height);
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);
}

void graph_draw_line(cairo_t *cr, int width, int height,
                     const gdouble *history, gint history_idx,
                     gint num_points, GdkRGBA *accent_color, gdouble line_width) {
    if (!cr || !history || !accent_color || num_points <= 1) return;

    cairo_set_source_rgba(cr, accent_color->red, accent_color->green, accent_color->blue, 0.9);
    cairo_set_line_width(cr, line_width);

    for (int i = 0; i < num_points; i++) {
        int idx = (history_idx + i) % num_points;
        double x = (double)i / (num_points - 1) * width;
        double y = height - (history[idx] / 100.0 * height);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

void graph_draw(cairo_t *cr, int width, int height,
                const gdouble *history, gint history_idx, gint num_points,
                GdkRGBA *bg_color, GdkRGBA *fg_color, GdkRGBA *accent_color,
                gdouble line_width) {
    if (!cr || !history || !bg_color || !fg_color || !accent_color || num_points <= 1) return;

    graph_draw_background(cr, width, height, bg_color);
    graph_draw_grid(cr, width, height, fg_color);
    graph_draw_fill(cr, width, height, history, history_idx, num_points, accent_color);
    graph_draw_line(cr, width, height, history, history_idx, num_points, accent_color, line_width);
}

void graph_draw_per_core_graphs(cairo_t *cr, GtkWidget *widget,
                                gint num_cores,
                                GraphGetHistoryFunc get_history,
                                GraphGetHistoryIndexFunc get_history_idx,
                                GraphGetLabelFunc get_label,
                                GdkRGBA *bg_color, GdkRGBA *fg_color, GdkRGBA *accent_color,
                                gint num_points) {
    if (!cr || !widget || num_cores <= 0 || !get_history || !get_history_idx || !get_label) return;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    int rows = (num_cores + 1) / 2;
    int cols = (num_cores > 1) ? 2 : 1;

    int graph_width = width / cols;
    int graph_height = height / rows;

    for (int i = 0; i < num_cores; i++) {
        int row = i / cols;
        int col = i % cols;

        cairo_save(cr);
        cairo_translate(cr, col * graph_width, row * graph_height);

        // Draw grid for each mini graph
        graph_draw_grid(cr, graph_width, graph_height, fg_color);

        const gdouble* history = get_history(i);
        if (history) {
            gint history_idx = get_history_idx();
            graph_draw_fill(cr, graph_width, graph_height, history, history_idx, num_points, accent_color);
            graph_draw_line(cr, graph_width, graph_height, history, history_idx, num_points, accent_color, 2.0);
        }

        // Label
        cairo_set_source_rgba(cr, fg_color->red, fg_color->green, fg_color->blue, 0.9);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);

        gchar label[32];
        get_label(i, label, sizeof(label));
        cairo_move_to(cr, 5, 15);
        cairo_show_text(cr, label);

        cairo_restore(cr);
    }
}
#ifndef GRAPH_UTILS_H
#define GRAPH_UTILS_H

#include <gtk/gtk.h>
#include <cairo.h>

/**
 * graph_get_theme_colors:
 * @widget: The widget to get the style context from
 * @bg: (out): Background color to fill
 * @fg: (out): Foreground/text color to fill  
 * @accent: (out): Accent color to fill
 * @accent_fallback: CSS color string used as fallback for accent if theme lookup fails
 *
 * Retrieves theme colors from the given widget. Falls back to sensible
 * defaults if theme lookup fails.
 */
void graph_get_theme_colors(GtkWidget *widget,
                            GdkRGBA *bg,
                            GdkRGBA *fg,
                            GdkRGBA *accent,
                            const gchar *accent_fallback);

/**
 * graph_draw_background:
 * @cr: Cairo context
 * @width: Widget width
 * @height: Widget height
 * @bg_color: Background color
 *
 * Fills the entire widget area with the given background color.
 */
void graph_draw_background(cairo_t *cr, int width, int height, GdkRGBA *bg_color);

/**
 * graph_draw_grid:
 * @cr: Cairo context
 * @width: Widget width
 * @height: Widget height
 * @fg_color: Foreground color used for grid lines
 *
 * Draws 4 horizontal grid lines (at 25%, 50%, 75% and 100%)
 */
void graph_draw_grid(cairo_t *cr, int width, int height, GdkRGBA *fg_color);

/**
 * graph_draw_fill:
 * @cr: Cairo context
 * @width: Widget width
 * @height: Widget height
 * @history: Array of data points (0-100 range)
 * @history_idx: Current circular index into history
 * @num_points: Total number of points in history
 * @accent_color: Color used for the fill
 *
 * Draws a gradient filled area under the graph curve.
 */
void graph_draw_fill(cairo_t *cr, int width, int height,
                     const gdouble *history, gint history_idx,
                     gint num_points, GdkRGBA *accent_color);

/**
 * graph_draw_line:
 * @cr: Cairo context
 * @width: Widget width
 * @height: Widget height
 * @history: Array of data points (0-100 range)
 * @history_idx: Current circular index into history
 * @num_points: Total number of points in history
 * @accent_color: Color used for the line
 * @line_width: Width of the stroke line
 *
 * Draws the graph line over the filled area.
 */
void graph_draw_line(cairo_t *cr, int width, int height,
                     const gdouble *history, gint history_idx,
                     gint num_points, GdkRGBA *accent_color, gdouble line_width);

/**
 * graph_draw:
 * @cr: Cairo context
 * @width: Widget width
 * @height: Widget height
 * @history: Array of data points (0-100 range)
 * @history_idx: Current circular index into history
 * @num_points: Total number of points in history
 * @bg_color: Background color
 * @fg_color: Foreground color (used for grid)
 * @accent_color: Accent color for data
 * @line_width: Width of the stroke line
 *
 * Convenience function that draws the complete graph:
 * background + grid + fill + line
 */
void graph_draw(cairo_t *cr, int width, int height,
                const gdouble *history, gint history_idx, gint num_points,
                GdkRGBA *bg_color, GdkRGBA *fg_color, GdkRGBA *accent_color,
                gdouble line_width);

/**
 * graph_draw_per_core_graphs:
 * @cr: Cairo context
 * @widget: The drawing widget
 * @num_cores: Number of CPU cores to draw
 * @get_history: Function to retrieve history for a given core index
 * @get_history_idx: Function to retrieve current history index
 * @get_label: Function to generate the label text for each core
 * @bg_color: Background color
 * @fg_color: Foreground/text color
 * @accent_color: Accent color for graphs
 *
 * Draws a grid of per-core CPU usage graphs. This is a specialized
 * helper for the CPU tab's per-core mode.
 */
typedef const gdouble* (*GraphGetHistoryFunc)(gint index);
typedef gint (*GraphGetHistoryIndexFunc)(void);
typedef void (*GraphGetLabelFunc)(gint index, gchar *buf, gsize buf_size);

void graph_draw_per_core_graphs(cairo_t *cr, GtkWidget *widget,
                                gint num_cores,
                                GraphGetHistoryFunc get_history,
                                GraphGetHistoryIndexFunc get_history_idx,
                                GraphGetLabelFunc get_label,
                                GdkRGBA *bg_color, GdkRGBA *fg_color, GdkRGBA *accent_color,
                                gint num_points);

#endif // GRAPH_UTILS_H
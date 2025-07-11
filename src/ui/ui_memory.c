#include "ui/ui_memory.h"
#include "memory/memory_data.h"
#include <cairo.h>
#include <math.h>

// Forward declarations
static gboolean draw_memory_graph(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean draw_swap_graph(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean update_memory_widgets(gpointer user_data);
static gboolean on_memory_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);

// Structure to hold all the widgets that need to be updated
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *swap_drawing_area;
    GtkWidget *memory_usage_label;
    GtkWidget *swap_usage_label;
    GtkWidget *memory_used_value;
    GtkWidget *memory_free_value;
    GtkWidget *memory_available_value;
    GtkWidget *memory_buffers_value;
    GtkWidget *memory_cached_value;
    GtkWidget *swap_used_value;
    GtkWidget *swap_free_value;
    guint update_interval;
    guint timeout_id;
} MemoryUpdateData;

// Cleanup function for the update data
static void cleanup_memory_update_data(gpointer data) {
    if (!data) return;
    
    MemoryUpdateData *update_data = (MemoryUpdateData*)data;
    
    if (update_data->timeout_id > 0) {
        g_source_remove(update_data->timeout_id);
        update_data->timeout_id = 0;
    }
    
    g_free(update_data);
}

// Function to update the memory widgets with current data
static gboolean update_memory_widgets(gpointer user_data) {
    g_print("update_memory_widgets called\n");
    MemoryUpdateData *data = (MemoryUpdateData*)user_data;
    if (!data) {
        g_print("ERROR: data is NULL\n");
        return G_SOURCE_REMOVE;
    }
    
    g_print("Updating memory data\n");
    memory_data_update();

    // Update memory usage percentage
    char memory_str[32];
    snprintf(memory_str, sizeof(memory_str), "%.1f%%", get_current_memory_usage_percent());
    g_print("Setting memory usage label: %s\n", memory_str);
    gtk_label_set_text(GTK_LABEL(data->memory_usage_label), memory_str);
    
    // Update swap usage percentage
    char swap_str[32];
    snprintf(swap_str, sizeof(swap_str), "%.1f%%", get_current_swap_usage_percent());
    g_print("Setting swap usage label: %s\n", swap_str);
    gtk_label_set_text(GTK_LABEL(data->swap_usage_label), swap_str);

    // Update memory values
    char used_str[32], free_str[32], available_str[32], buffers_str[32], cached_str[32];
    char swap_used_str[32], swap_free_str[32];
    
    snprintf(used_str, sizeof(used_str), "%lu MB", get_used_memory());
    snprintf(free_str, sizeof(free_str), "%lu MB", get_free_memory());
    snprintf(available_str, sizeof(available_str), "%lu MB", get_available_memory());
    snprintf(buffers_str, sizeof(buffers_str), "%lu MB", get_buffer_memory());
    snprintf(cached_str, sizeof(cached_str), "%lu MB", get_cached_memory());
    snprintf(swap_used_str, sizeof(swap_used_str), "%lu MB", get_swap_used());
    snprintf(swap_free_str, sizeof(swap_free_str), "%lu MB", get_swap_free());
    
    g_print("Setting memory value labels\n");
    if (data->memory_used_value && GTK_IS_LABEL(data->memory_used_value)) {
        gtk_label_set_text(GTK_LABEL(data->memory_used_value), used_str);
    }
    if (data->memory_free_value && GTK_IS_LABEL(data->memory_free_value)) {
        gtk_label_set_text(GTK_LABEL(data->memory_free_value), free_str);
    }
    if (data->memory_available_value && GTK_IS_LABEL(data->memory_available_value)) {
        gtk_label_set_text(GTK_LABEL(data->memory_available_value), available_str);
    }
    if (data->memory_buffers_value && GTK_IS_LABEL(data->memory_buffers_value)) {
        gtk_label_set_text(GTK_LABEL(data->memory_buffers_value), buffers_str);
    }
    if (data->memory_cached_value && GTK_IS_LABEL(data->memory_cached_value)) {
        gtk_label_set_text(GTK_LABEL(data->memory_cached_value), cached_str);
    }
    if (data->swap_used_value && GTK_IS_LABEL(data->swap_used_value)) {
        gtk_label_set_text(GTK_LABEL(data->swap_used_value), swap_used_str);
    }
    if (data->swap_free_value && GTK_IS_LABEL(data->swap_free_value)) {
        gtk_label_set_text(GTK_LABEL(data->swap_free_value), swap_free_str);
    }

    g_print("Queueing redraw of drawing areas\n");
    gtk_widget_queue_draw(data->drawing_area);
    gtk_widget_queue_draw(data->swap_drawing_area);
    g_print("update_memory_widgets complete\n");
    return G_SOURCE_CONTINUE;
}

// Function to draw the memory usage graph
static gboolean draw_memory_graph(GtkWidget *widget, cairo_t *cr, gpointer data) {
    g_print("draw_memory_graph called\n");
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    g_print("Graph dimensions: %d x %d\n", width, height);
    
    GtkStyleContext *style_context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color, fg_color, accent_color;

    // Provide sensible fallbacks
    gdk_rgba_parse(&bg_color, "rgb(24, 25, 26)");
    gdk_rgba_parse(&fg_color, "rgb(238, 238, 236)");
    gdk_rgba_parse(&accent_color, "rgb(76, 175, 80)"); // Green for memory

    // Try to get theme colors
    gtk_style_context_lookup_color(style_context, "theme_bg_color", &bg_color);
    gtk_style_context_lookup_color(style_context, "theme_fg_color", &fg_color);

    g_print("Drawing background\n");
    gdk_cairo_set_source_rgba(cr, &bg_color);
    cairo_paint(cr);

    g_print("Drawing grid lines\n");
    GdkRGBA grid_color = fg_color;
    grid_color.alpha = 0.2;
    gdk_cairo_set_source_rgba(cr, &grid_color);
    cairo_set_line_width(cr, 0.8);

    // Draw grid lines
    for (int i = 1; i < 4; i++) {
        double y = height * i / 4.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
    }
    cairo_stroke(cr);
    
    // Draw memory usage graph
    g_print("Getting memory usage history\n");
    const gdouble* history = get_memory_usage_history();
    gint history_idx = get_memory_usage_history_index();
    
    // Graph fill
    g_print("Creating gradient fill\n");
    cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(fill, 0, accent_color.red, accent_color.green, accent_color.blue, 0.7);
    cairo_pattern_add_color_stop_rgba(fill, 1, accent_color.red, accent_color.green, accent_color.blue, 0.1);
    cairo_set_source(cr, fill);

    g_print("Drawing graph fill\n");
    cairo_move_to(cr, 0, height);
    for (int i = 0; i < MAX_POINTS; i++) {
        int idx = (history_idx + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (history[idx] / 100.0 * height);
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);

    // Graph line
    g_print("Drawing graph line\n");
    cairo_set_source_rgba(cr, accent_color.red, accent_color.green, accent_color.blue, 0.9);
    cairo_set_line_width(cr, 2.5);
    for (int i = 0; i < MAX_POINTS; i++) {
        int idx = (history_idx + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (history[idx] / 100.0 * height);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    g_print("draw_memory_graph complete\n");
    return FALSE;
}

// Function to draw the swap usage graph
static gboolean draw_swap_graph(GtkWidget *widget, cairo_t *cr, gpointer data) {
    g_print("draw_swap_graph called\n");
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    g_print("Graph dimensions: %d x %d\n", width, height);
    
    GtkStyleContext *style_context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color, fg_color, accent_color;

    // Provide sensible fallbacks
    gdk_rgba_parse(&bg_color, "rgb(24, 25, 26)");
    gdk_rgba_parse(&fg_color, "rgb(238, 238, 236)");
    gdk_rgba_parse(&accent_color, "rgb(66, 135, 245)"); // Blue for swap

    // Try to get theme colors
    gtk_style_context_lookup_color(style_context, "theme_bg_color", &bg_color);
    gtk_style_context_lookup_color(style_context, "theme_fg_color", &fg_color);

    g_print("Drawing background\n");
    gdk_cairo_set_source_rgba(cr, &bg_color);
    cairo_paint(cr);

    g_print("Drawing grid lines\n");
    GdkRGBA grid_color = fg_color;
    grid_color.alpha = 0.2;
    gdk_cairo_set_source_rgba(cr, &grid_color);
    cairo_set_line_width(cr, 0.8);

    // Draw grid lines
    for (int i = 1; i < 4; i++) {
        double y = height * i / 4.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
    }
    cairo_stroke(cr);
    
    // Draw swap usage graph
    g_print("Getting swap usage history\n");
    const gdouble* swap_history = get_swap_usage_history();
    gint swap_history_idx = get_swap_usage_history_index();
    
    // Graph fill
    g_print("Creating gradient fill\n");
    cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(fill, 0, accent_color.red, accent_color.green, accent_color.blue, 0.7);
    cairo_pattern_add_color_stop_rgba(fill, 1, accent_color.red, accent_color.green, accent_color.blue, 0.1);
    cairo_set_source(cr, fill);

    g_print("Drawing graph fill\n");
    cairo_move_to(cr, 0, height);
    for (int i = 0; i < MAX_POINTS; i++) {
        int idx = (swap_history_idx + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (swap_history[idx] / 100.0 * height);
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);

    // Graph line
    g_print("Drawing graph line\n");
    cairo_set_source_rgba(cr, accent_color.red, accent_color.green, accent_color.blue, 0.9);
    cairo_set_line_width(cr, 2.5);
    for (int i = 0; i < MAX_POINTS; i++) {
        int idx = (swap_history_idx + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (swap_history[idx] / 100.0 * height);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    g_print("draw_swap_graph complete\n");
    return FALSE;
}

// Handle right-click on memory tab
static gboolean on_memory_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->button == 3) { // Right click
        GtkWidget *menu = GTK_WIDGET(user_data);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

// Dialog for changing refresh period
static void show_refresh_dialog(GtkWidget *parent, MemoryUpdateData *data) {
    if (!data) return;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Refresh Period",
                                                  GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Apply", GTK_RESPONSE_APPLY,
                                                  "Cancel", GTK_RESPONSE_CANCEL,
                                                  NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *label = gtk_label_new("Update interval (ms):");
    GtkWidget *spin = gtk_spin_button_new_with_range(100, 5000, 100);
    
    // Set the current interval if available, otherwise use default
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), data->update_interval);
    
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);
    
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_APPLY) {
        guint interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        g_print("Setting memory refresh interval to %d ms\n", interval);
        
        // Update the interval
        data->update_interval = interval;
        
        // Remove the old timeout and add a new one with the updated interval
        if (data->timeout_id > 0) {
            g_source_remove(data->timeout_id);
        }
        data->timeout_id = g_timeout_add(data->update_interval, (GSourceFunc)update_memory_widgets, data);
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for the refresh menu item
static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(drawing_area)); // graph_frame -> main_grid
    
    // Get the MemoryUpdateData from the parent's data
    MemoryUpdateData *update_data = g_object_get_data(G_OBJECT(parent), "update_data");
    
    show_refresh_dialog(drawing_area, update_data);
}

// Create the memory tab
GtkWidget* create_memory_tab(void) {
    g_print("Creating memory tab\n");
    GtkWidget *main_grid = gtk_grid_new();
    gtk_widget_set_hexpand(main_grid, TRUE);
    gtk_widget_set_vexpand(main_grid, TRUE);
    gtk_widget_set_margin_start(main_grid, 10);
    gtk_widget_set_margin_end(main_grid, 10);
    gtk_widget_set_margin_top(main_grid, 10);
    gtk_widget_set_margin_bottom(main_grid, 10);
    gtk_grid_set_column_spacing(GTK_GRID(main_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(main_grid), 10);

    g_print("Creating RAM drawing area\n");
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 150);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_memory_graph), NULL);

    g_print("Creating swap drawing area\n");
    GtkWidget *swap_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(swap_drawing_area, -1, 100);
    gtk_widget_set_hexpand(swap_drawing_area, TRUE);
    gtk_widget_set_vexpand(swap_drawing_area, TRUE);
    g_signal_connect(G_OBJECT(swap_drawing_area), "draw", G_CALLBACK(draw_swap_graph), NULL);

    // Create the right-click menu
    g_print("Creating right-click menu\n");
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Refresh Period");
    g_signal_connect(refresh_item, "activate", G_CALLBACK(on_refresh_activate), drawing_area);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);
    gtk_widget_show_all(menu);
    
    // Connect the right-click event to the drawing areas
    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_memory_tab_button_press), menu);
    gtk_widget_add_events(swap_drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(swap_drawing_area, "button-press-event", G_CALLBACK(on_memory_tab_button_press), menu);

    g_print("Creating graph frames\n");
    GtkWidget *graph_frame = gtk_frame_new("RAM Usage");
    gtk_frame_set_shadow_type(GTK_FRAME(graph_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(graph_frame), drawing_area);
    
    GtkWidget *swap_frame = gtk_frame_new("Swap Usage");
    gtk_frame_set_shadow_type(GTK_FRAME(swap_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(swap_frame), swap_drawing_area);

    g_print("Creating info grid\n");
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(info_box, GTK_ALIGN_START);

    GtkWidget *memory_info_frame = gtk_frame_new("RAM Information");
    gtk_frame_set_shadow_type(GTK_FRAME(memory_info_frame), GTK_SHADOW_ETCHED_IN);
    
    GtkWidget *memory_info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(memory_info_grid), 15);
    gtk_grid_set_row_spacing(GTK_GRID(memory_info_grid), 8);
    gtk_widget_set_margin_start(memory_info_grid, 10);
    gtk_widget_set_margin_end(memory_info_grid, 10);
    gtk_widget_set_margin_top(memory_info_grid, 10);
    gtk_widget_set_margin_bottom(memory_info_grid, 10);
    gtk_container_add(GTK_CONTAINER(memory_info_frame), memory_info_grid);
    
    GtkWidget *swap_info_frame = gtk_frame_new("Swap Information");
    gtk_frame_set_shadow_type(GTK_FRAME(swap_info_frame), GTK_SHADOW_ETCHED_IN);
    
    GtkWidget *swap_info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(swap_info_grid), 15);
    gtk_grid_set_row_spacing(GTK_GRID(swap_info_grid), 8);
    gtk_widget_set_margin_start(swap_info_grid, 10);
    gtk_widget_set_margin_end(swap_info_grid, 10);
    gtk_widget_set_margin_top(swap_info_grid, 10);
    gtk_widget_set_margin_bottom(swap_info_grid, 10);
    gtk_container_add(GTK_CONTAINER(swap_info_frame), swap_info_grid);
    
    // Pack frames into the info box
    gtk_box_pack_start(GTK_BOX(info_box), memory_info_frame, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), swap_info_frame, FALSE, FALSE, 0);

    g_print("Creating usage boxes\n");
    // RAM usage box
    GtkWidget *ram_usage_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *memory_label_title = gtk_label_new("RAM Usage:");
    GtkWidget *memory_label_value = gtk_label_new("0.0%");

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "label.memory-usage { font-weight: bold; font-size: 14px; }", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(memory_label_value);
    gtk_style_context_add_class(context, "memory-usage");
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_box_pack_start(GTK_BOX(ram_usage_box), memory_label_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ram_usage_box), memory_label_value, FALSE, FALSE, 0);
    
    // Swap usage box
    GtkWidget *swap_usage_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *swap_label_title = gtk_label_new("Swap Usage:");
    GtkWidget *swap_label_value = gtk_label_new("0.0%");

    context = gtk_widget_get_style_context(swap_label_value);
    gtk_style_context_add_class(context, "memory-usage");
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);

    gtk_box_pack_start(GTK_BOX(swap_usage_box), swap_label_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(swap_usage_box), swap_label_value, FALSE, FALSE, 0);

    g_print("Attaching widgets to main grid\n");
    gtk_grid_attach(GTK_GRID(main_grid), ram_usage_box, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), graph_frame, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), info_box, 1, 0, 1, 2);
    
    gtk_grid_attach(GTK_GRID(main_grid), swap_usage_box, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), swap_frame, 0, 3, 1, 1);

    g_print("Creating update data\n");
    MemoryUpdateData *update_data = g_new0(MemoryUpdateData, 1);
    update_data->drawing_area = drawing_area;
    update_data->swap_drawing_area = swap_drawing_area;
    update_data->memory_usage_label = memory_label_value;
    update_data->swap_usage_label = swap_label_value;
    
    // Create memory info labels
    int row = 0;
    #define ADD_MEMORY_INFO(label_text, variable_name) \
        do { \
            GtkWidget *label = gtk_label_new(label_text); \
            gtk_widget_set_halign(label, GTK_ALIGN_START); \
            GtkWidget *value = gtk_label_new("N/A"); \
            gtk_widget_set_halign(value, GTK_ALIGN_START); \
            gtk_grid_attach(GTK_GRID(memory_info_grid), label, 0, row, 1, 1); \
            gtk_grid_attach(GTK_GRID(memory_info_grid), value, 1, row, 1, 1); \
            update_data->variable_name = value; \
            row++; \
        } while (0)
        
    ADD_MEMORY_INFO("Used:", memory_used_value);
    ADD_MEMORY_INFO("Free:", memory_free_value);
    ADD_MEMORY_INFO("Available:", memory_available_value);
    ADD_MEMORY_INFO("Buffers:", memory_buffers_value);
    ADD_MEMORY_INFO("Cached:", memory_cached_value);
    
    // Create swap info labels
    row = 0;
    #define ADD_SWAP_INFO(label_text, variable_name) \
        do { \
            GtkWidget *label = gtk_label_new(label_text); \
            gtk_widget_set_halign(label, GTK_ALIGN_START); \
            GtkWidget *value = gtk_label_new("N/A"); \
            gtk_widget_set_halign(value, GTK_ALIGN_START); \
            gtk_grid_attach(GTK_GRID(swap_info_grid), label, 0, row, 1, 1); \
            gtk_grid_attach(GTK_GRID(swap_info_grid), value, 1, row, 1, 1); \
            update_data->variable_name = value; \
            row++; \
        } while (0)
        
    ADD_SWAP_INFO("Used:", swap_used_value);
    ADD_SWAP_INFO("Free:", swap_free_value);
    
    update_data->update_interval = 1000; // Default to 1 second
    
    // Store the update_data in the main_grid's data
    g_print("Setting data on main grid\n");
    g_object_set_data_full(G_OBJECT(main_grid), "update_data", update_data, cleanup_memory_update_data);
    
    // Set up the timeout with the initial interval
    g_print("Adding timeout\n");
    update_data->timeout_id = g_timeout_add(update_data->update_interval, (GSourceFunc)update_memory_widgets, update_data);
    g_print("Memory tab creation complete\n");

    return main_grid;
} 
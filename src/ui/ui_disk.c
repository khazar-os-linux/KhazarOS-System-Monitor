#include "ui/ui_disk.h"
#include "disk/disk_data.h"
#include <cairo.h>
#include <math.h>

// Forward declarations
static gboolean draw_disk_graph(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean update_disk_widgets(gpointer user_data);
static gboolean on_disk_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void on_disk_combo_changed(GtkComboBox *widget, gpointer user_data);

// Structure to hold all the widgets that need to be updated
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *disk_activity_label;
    GtkWidget *disk_combo;
    GtkWidget *disk_type_value;
    GtkWidget *disk_size_value;
    GtkWidget *disk_mount_value;
    GtkWidget *disk_fs_value;
    GtkWidget *disk_used_value;
    GtkWidget *disk_free_value;
    gint selected_disk_index;
    guint update_interval;
    guint timeout_id;
} DiskUpdateData;

// Cleanup function for the update data
static void cleanup_disk_update_data(gpointer data) {
    if (!data) return;
    
    DiskUpdateData *update_data = (DiskUpdateData*)data;
    
    if (update_data->timeout_id > 0) {
        g_source_remove(update_data->timeout_id);
        update_data->timeout_id = 0;
    }
    
    g_free(update_data);
}

// Function to update the disk widgets with current data
static gboolean update_disk_widgets(gpointer user_data) {
    g_print("update_disk_widgets called\n");
    DiskUpdateData *data = (DiskUpdateData*)user_data;
    if (!data) {
        g_print("ERROR: data is NULL\n");
        return G_SOURCE_REMOVE;
    }
    
    g_print("Updating disk data\n");
    disk_data_update();
    
    // Check if we need to update the combo box
    gint disk_count = get_disk_count();
    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(data->disk_combo)));
    gint model_count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), NULL);
    
    if (disk_count != model_count) {
        // Clear and repopulate the combo box
        gtk_list_store_clear(store);
        for (gint i = 0; i < disk_count; i++) {
            const DiskInfo *disk_info = get_disk_info(i);
            if (disk_info) {
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gchar label[512];
                snprintf(label, sizeof(label), "%s (%s)", disk_info->device_name, disk_info->mount_point);
                gtk_list_store_set(store, &iter, 0, label, 1, i, -1);
            }
        }
        
        // Set active item
        if (disk_count > 0) {
            if (data->selected_disk_index >= disk_count) {
                data->selected_disk_index = 0;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->disk_combo), data->selected_disk_index);
        }
    }
    
    // Update disk information if we have a valid selection
    if (data->selected_disk_index >= 0 && data->selected_disk_index < disk_count) {
        const DiskInfo *disk_info = get_disk_info(data->selected_disk_index);
        
        // Update disk activity percentage instead of usage
    char disk_str[32];
        snprintf(disk_str, sizeof(disk_str), "%.1f%%", get_current_disk_activity_percent(data->selected_disk_index));
        g_print("Setting disk activity label: %s\n", disk_str);
        if (data->disk_activity_label && GTK_IS_LABEL(data->disk_activity_label)) {
            gtk_label_set_text(GTK_LABEL(data->disk_activity_label), disk_str);
    }
    
    // Update disk specifications - check each widget before using it
    if (data->disk_type_value && GTK_IS_LABEL(data->disk_type_value)) {
        gtk_label_set_text(GTK_LABEL(data->disk_type_value), get_disk_type(data->selected_disk_index));
    }
    
    if (data->disk_size_value && GTK_IS_LABEL(data->disk_size_value)) {
        char size_str[32];
        snprintf(size_str, sizeof(size_str), "%lu GB", disk_info->total_space / 1024);
        gtk_label_set_text(GTK_LABEL(data->disk_size_value), size_str);
    }
    
    // Update disk usage information
    if (data->disk_mount_value && GTK_IS_LABEL(data->disk_mount_value)) {
        gtk_label_set_text(GTK_LABEL(data->disk_mount_value), disk_info->mount_point);
    }
    
    if (data->disk_fs_value && GTK_IS_LABEL(data->disk_fs_value)) {
        gtk_label_set_text(GTK_LABEL(data->disk_fs_value), disk_info->fs_type);
    }
    
    if (data->disk_used_value && GTK_IS_LABEL(data->disk_used_value)) {
        char used_str[32];
        snprintf(used_str, sizeof(used_str), "%lu MB", disk_info->used_space);
        gtk_label_set_text(GTK_LABEL(data->disk_used_value), used_str);
    }
    
    if (data->disk_free_value && GTK_IS_LABEL(data->disk_free_value)) {
        char free_str[32];
        snprintf(free_str, sizeof(free_str), "%lu MB", disk_info->free_space);
        gtk_label_set_text(GTK_LABEL(data->disk_free_value), free_str);
    }
    }
    
    g_print("Queueing redraw of drawing area\n");
    gtk_widget_queue_draw(data->drawing_area);
    g_print("update_disk_widgets complete\n");
    return G_SOURCE_CONTINUE;
}

// Function to draw the disk activity graph (instead of usage)
static gboolean draw_disk_graph(GtkWidget *widget, cairo_t *cr, gpointer data) {
    g_print("draw_disk_graph called\n");
    DiskUpdateData *update_data = (DiskUpdateData*)data;
    if (!update_data) {
        g_print("ERROR: update_data is NULL\n");
        return FALSE;
    }
    
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    g_print("Graph dimensions: %d x %d\n", width, height);
    
    GtkStyleContext *style_context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color, fg_color, accent_color;

    // Provide sensible fallbacks
    gdk_rgba_parse(&bg_color, "rgb(24, 25, 26)");
    gdk_rgba_parse(&fg_color, "rgb(238, 238, 236)");
    gdk_rgba_parse(&accent_color, "rgb(230, 97, 0)"); // Orange for disk

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
    
    // Draw disk activity graph if we have a valid selection
    gint disk_count = get_disk_count();
    if (update_data->selected_disk_index >= 0 && update_data->selected_disk_index < disk_count) {
        g_print("Getting disk activity history\n");
        const gdouble* history = get_disk_activity_history(update_data->selected_disk_index);
        gint history_idx = get_disk_activity_history_index(update_data->selected_disk_index);
        
        if (history) {
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
        }
    }

    g_print("draw_disk_graph complete\n");
    return FALSE;
}

// Right-click menu handler
static gboolean on_disk_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->button == 3) { // Right mouse button
        GtkWidget *menu = GTK_WIDGET(user_data);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

// Disk combo box change handler
static void on_disk_combo_changed(GtkComboBox *widget, gpointer user_data) {
    DiskUpdateData *data = (DiskUpdateData*)user_data;
    if (!data) return;
    
    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter(widget, &iter)) {
        GtkTreeModel *model = gtk_combo_box_get_model(widget);
        gint index;
        gtk_tree_model_get(model, &iter, 1, &index, -1);
        data->selected_disk_index = index;
        update_disk_widgets(data);
    }
}

// Dialog for Refresh Period
static void show_refresh_dialog(GtkWidget *parent, DiskUpdateData *data) {
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
    if (data && data->update_interval > 0) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), data->update_interval);
    } else {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1000); // Default 1 second
    }
    
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);
    
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_APPLY && data) {
        guint interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        g_print("Setting refresh interval to %d ms\n", interval);
        
        // Update the interval
        data->update_interval = interval;
        
        // Remove the old timeout and add a new one with the updated interval
        if (data->timeout_id > 0) {
            g_source_remove(data->timeout_id);
        }
        data->timeout_id = g_timeout_add(data->update_interval, (GSourceFunc)update_disk_widgets, data);
    }
    
    gtk_widget_destroy(dialog);
}

static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    // user_data is the drawing_area, but we need the DiskUpdateData
    // Get the parent grid to find the DiskUpdateData
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(drawing_area)); // graph_frame -> main_grid
    
    // Get the DiskUpdateData from the parent's data
    gpointer update_data = g_object_get_data(G_OBJECT(parent), "update_data");
    
    show_refresh_dialog(drawing_area, update_data);
}

GtkWidget* create_disk_tab(void) {
    // Initialize disk data
    disk_data_init();
    
    GtkWidget *main_grid = gtk_grid_new();
    gtk_widget_set_hexpand(main_grid, TRUE);
    gtk_widget_set_vexpand(main_grid, TRUE);
    gtk_widget_set_margin_start(main_grid, 10);
    gtk_widget_set_margin_end(main_grid, 10);
    gtk_widget_set_margin_top(main_grid, 10);
    gtk_widget_set_margin_bottom(main_grid, 10);
    gtk_grid_set_column_spacing(GTK_GRID(main_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(main_grid), 10);

    // Create disk selection combo box
    GtkWidget *combo_label = gtk_label_new("Disk:");
    gtk_widget_set_halign(combo_label, GTK_ALIGN_START);
    
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget *combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer, "text", 0, NULL);
    
    GtkWidget *combo_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(combo_box), combo_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(combo_box), combo, TRUE, TRUE, 0);
    
    // Create the graph area
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 250);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);

    GtkWidget *graph_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(graph_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(graph_frame), drawing_area);

    // Create disk activity label (changed from "Usage:" to "Activity:")
    GtkWidget *activity_label = gtk_label_new("Activity:");
    gtk_widget_set_halign(activity_label, GTK_ALIGN_END);
    
    GtkWidget *activity_value = gtk_label_new("0.0%");
    gtk_widget_set_halign(activity_value, GTK_ALIGN_START);
    
    // Create disk info grid
    GtkWidget *info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(info_grid), 15);
    gtk_grid_set_row_spacing(GTK_GRID(info_grid), 8);
    
    // Add disk info labels
    int row = 0;
    
    // Add type
    GtkWidget *type_label = gtk_label_new("Type:");
    gtk_widget_set_halign(type_label, GTK_ALIGN_START);
    GtkWidget *type_value = gtk_label_new("Unknown");
    gtk_widget_set_halign(type_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), type_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), type_value, 1, row, 1, 1);
    row++;
    
    // Add size
    GtkWidget *size_label = gtk_label_new("Size:");
    gtk_widget_set_halign(size_label, GTK_ALIGN_START);
    GtkWidget *size_value = gtk_label_new("0 GB");
    gtk_widget_set_halign(size_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), size_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), size_value, 1, row, 1, 1);
    row++;
    
    // Add mount point
    GtkWidget *mount_label = gtk_label_new("Mount point:");
    gtk_widget_set_halign(mount_label, GTK_ALIGN_START);
    GtkWidget *mount_value = gtk_label_new("/");
    gtk_widget_set_halign(mount_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), mount_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), mount_value, 1, row, 1, 1);
    row++;
    
    // Add filesystem
    GtkWidget *fs_label = gtk_label_new("Filesystem:");
    gtk_widget_set_halign(fs_label, GTK_ALIGN_START);
    GtkWidget *fs_value = gtk_label_new("unknown");
    gtk_widget_set_halign(fs_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), fs_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), fs_value, 1, row, 1, 1);
    row++;
    
    // Add used space
    GtkWidget *used_label = gtk_label_new("Used space:");
    gtk_widget_set_halign(used_label, GTK_ALIGN_START);
    GtkWidget *used_value = gtk_label_new("0 MB");
    gtk_widget_set_halign(used_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), used_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), used_value, 1, row, 1, 1);
    row++;
    
    // Add free space
    GtkWidget *free_label = gtk_label_new("Free space:");
    gtk_widget_set_halign(free_label, GTK_ALIGN_START);
    GtkWidget *free_value = gtk_label_new("0 MB");
    gtk_widget_set_halign(free_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), free_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), free_value, 1, row, 1, 1);
    row++;
    
    // Create info frame
    GtkWidget *info_frame = gtk_frame_new("Disk Information");
    gtk_frame_set_shadow_type(GTK_FRAME(info_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(info_frame), info_grid);

    // Add widgets to main grid
    gtk_grid_attach(GTK_GRID(main_grid), combo_box, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(main_grid), graph_frame, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(main_grid), activity_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), activity_value, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), info_frame, 0, 3, 2, 1);
    
    // Create update data
    DiskUpdateData *data = g_new0(DiskUpdateData, 1);
    data->drawing_area = drawing_area;
    data->disk_activity_label = activity_value;
    data->disk_combo = combo;
    data->disk_type_value = type_value;
    data->disk_size_value = size_value;
    data->disk_mount_value = mount_value;
    data->disk_fs_value = fs_value;
    data->disk_used_value = used_value;
    data->disk_free_value = free_value;
    data->selected_disk_index = 0;
    data->update_interval = 2000;  // 2 seconds
    
    // Connect signals
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_disk_graph), data);
    g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(on_disk_combo_changed), data);
    g_signal_connect(G_OBJECT(drawing_area), "button-press-event", G_CALLBACK(on_disk_tab_button_press), data);
    
    // Add events
    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    
    // Set up update timer
    data->timeout_id = g_timeout_add(data->update_interval, (GSourceFunc)update_disk_widgets, data);
    
    // Store data in main_grid for cleanup
    g_object_set_data_full(G_OBJECT(main_grid), "update_data", data, cleanup_disk_update_data);
    
    // Initial update
    update_disk_widgets(data);
    
    return main_grid;
} 
#include "ui/ui_network.h"
#include "network/network_data.h"
#include <cairo.h>
#include <math.h>

// Forward declarations
static gboolean draw_network_graph(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean update_network_widgets(gpointer user_data);
static gboolean on_network_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void on_interface_combo_changed(GtkComboBox *widget, gpointer user_data);

// Structure to hold all the widgets that need to be updated
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *download_speed_label;
    GtkWidget *upload_speed_label;
    GtkWidget *interface_combo;
    GtkWidget *interface_type_value;
    GtkWidget *ip_address_value;
    GtkWidget *mac_address_value;
    GtkWidget *mtu_value;
    GtkWidget *link_speed_value;
    GtkWidget *total_download_value;
    GtkWidget *total_upload_value;
    gint selected_interface_index;
    guint update_interval;
    guint timeout_id;
} NetworkUpdateData;

// Cleanup function for the update data
static void cleanup_network_update_data(gpointer data) {
    if (!data) return;
    
    NetworkUpdateData *update_data = (NetworkUpdateData*)data;
    
    if (update_data->timeout_id > 0) {
        g_source_remove(update_data->timeout_id);
        update_data->timeout_id = 0;
    }
    
    g_free(update_data);
}

// Function to update the network widgets with current data
static gboolean update_network_widgets(gpointer user_data) {
    g_print("update_network_widgets called\n");
    NetworkUpdateData *data = (NetworkUpdateData*)user_data;
    if (!data) {
        g_print("ERROR: data is NULL\n");
        return G_SOURCE_REMOVE;
    }
    
    g_print("Updating network data\n");
    network_data_update();
    
    // Check if we need to update the combo box
    gint interface_count = get_interface_count();
    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(data->interface_combo)));
    gint model_count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), NULL);
    
    if (interface_count != model_count) {
        // Clear and repopulate the combo box
        gtk_list_store_clear(store);
        for (gint i = 0; i < interface_count; i++) {
            const NetworkInfo *interface_info = get_interface_info(i);
            if (interface_info) {
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gchar label[128];
                snprintf(label, sizeof(label), "%s (%s)", interface_info->interface_name, interface_info->interface_type);
                gtk_list_store_set(store, &iter, 0, label, 1, i, -1);
            }
        }
        
        // Set active item
        if (interface_count > 0) {
            if (data->selected_interface_index >= interface_count) {
                data->selected_interface_index = 0;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->interface_combo), data->selected_interface_index);
        }
    }
    
    // Update network information if we have a valid selection
    if (data->selected_interface_index >= 0 && data->selected_interface_index < interface_count) {
        const NetworkInfo *interface_info = get_interface_info(data->selected_interface_index);
        
        // Update download speed (convert KB/s -> Mbps)
        double download_mbps = get_current_rx_speed(data->selected_interface_index) * 8.0 / 1024.0;
        char download_str[32];
        snprintf(download_str, sizeof(download_str), "%.2f Mbps", download_mbps);
        g_print("Setting download speed label: %s\n", download_str);
        if (data->download_speed_label && GTK_IS_LABEL(data->download_speed_label)) {
            gtk_label_set_text(GTK_LABEL(data->download_speed_label), download_str);
        }
        
        // Update upload speed (convert KB/s -> Mbps)
        double upload_mbps = get_current_tx_speed(data->selected_interface_index) * 8.0 / 1024.0;
        char upload_str[32];
        snprintf(upload_str, sizeof(upload_str), "%.2f Mbps", upload_mbps);
        g_print("Setting upload speed label: %s\n", upload_str);
        if (data->upload_speed_label && GTK_IS_LABEL(data->upload_speed_label)) {
            gtk_label_set_text(GTK_LABEL(data->upload_speed_label), upload_str);
        }
        
        // Update interface specifications
        if (data->interface_type_value && GTK_IS_LABEL(data->interface_type_value)) {
            gtk_label_set_text(GTK_LABEL(data->interface_type_value), interface_info->interface_type);
        }
        
        if (data->ip_address_value && GTK_IS_LABEL(data->ip_address_value)) {
            gtk_label_set_text(GTK_LABEL(data->ip_address_value), interface_info->ip_address);
        }
        
        // MAC Address
        if (data->mac_address_value && GTK_IS_LABEL(data->mac_address_value)) {
            gtk_label_set_text(GTK_LABEL(data->mac_address_value), interface_info->mac_address);
        }

        // MTU
        if (data->mtu_value && GTK_IS_LABEL(data->mtu_value)) {
            char mtu_str[16];
            if (interface_info->mtu > 0) {
                snprintf(mtu_str, sizeof(mtu_str), "%d", interface_info->mtu);
            } else {
                snprintf(mtu_str, sizeof(mtu_str), "N/A");
            }
            gtk_label_set_text(GTK_LABEL(data->mtu_value), mtu_str);
        }

        // Link speed
        if (data->link_speed_value && GTK_IS_LABEL(data->link_speed_value)) {
            char speed_str[16];
            if (interface_info->link_speed_mbps > 0) {
                snprintf(speed_str, sizeof(speed_str), "%d Mbps", interface_info->link_speed_mbps);
            } else {
                snprintf(speed_str, sizeof(speed_str), "N/A");
            }
            gtk_label_set_text(GTK_LABEL(data->link_speed_value), speed_str);
        }
        
        // Update total transferred data (in MB)
        if (data->total_download_value && GTK_IS_LABEL(data->total_download_value)) {
            char total_download[32];
            snprintf(total_download, sizeof(total_download), "%.2f MB", interface_info->current_rx_bytes / (1024.0 * 1024.0));
            gtk_label_set_text(GTK_LABEL(data->total_download_value), total_download);
        }
        
        if (data->total_upload_value && GTK_IS_LABEL(data->total_upload_value)) {
            char total_upload[32];
            snprintf(total_upload, sizeof(total_upload), "%.2f MB", interface_info->current_tx_bytes / (1024.0 * 1024.0));
            gtk_label_set_text(GTK_LABEL(data->total_upload_value), total_upload);
        }
    }
    
    g_print("Queueing redraw of drawing area\n");
    gtk_widget_queue_draw(data->drawing_area);
    g_print("update_network_widgets complete\n");
    return G_SOURCE_CONTINUE;
}

// Function to draw the network traffic graph
static gboolean draw_network_graph(GtkWidget *widget, cairo_t *cr, gpointer data) {
    g_print("draw_network_graph called\n");
    NetworkUpdateData *update_data = (NetworkUpdateData*)data;
    if (!update_data) {
        g_print("ERROR: update_data is NULL\n");
        return FALSE;
    }
    
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    g_print("Graph dimensions: %d x %d\n", width, height);
    
    GtkStyleContext *style_context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color, fg_color, download_color, upload_color;

    // Provide sensible fallbacks
    gdk_rgba_parse(&bg_color, "rgb(24, 25, 26)");
    gdk_rgba_parse(&fg_color, "rgb(238, 238, 236)");
    gdk_rgba_parse(&download_color, "rgb(52, 101, 164)");  // Blue for download
    gdk_rgba_parse(&upload_color, "rgb(78, 154, 6)");      // Green for upload

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
    
    // Draw network traffic graph if we have a valid selection
    gint interface_count = get_interface_count();
    if (update_data->selected_interface_index >= 0 && update_data->selected_interface_index < interface_count) {
        g_print("Getting network traffic history\n");
        const gdouble* rx_history_raw = get_rx_history(update_data->selected_interface_index);
        const gdouble* tx_history_raw = get_tx_history(update_data->selected_interface_index);
        gint history_idx = get_history_index(update_data->selected_interface_index);
        
        // Convert history to Mbps on the fly
        const gdouble* rx_history = rx_history_raw; // alias to keep code adjustments minimal
        const gdouble* tx_history = tx_history_raw;
        
        // Find the maximum value for scaling
        gdouble max_value = 1.0;  // Minimum to avoid division by zero
        for (int i = 0; i < MAX_POINTS; i++) {
            int idx = (history_idx + i) % MAX_POINTS;
            double rx_mbps = rx_history_raw[idx] * 8.0 / 1024.0;
            double tx_mbps = tx_history_raw[idx] * 8.0 / 1024.0;
            if (rx_mbps > max_value) max_value = rx_mbps;
            if (tx_mbps > max_value) max_value = tx_mbps;
        }
        
        // Add 20% headroom
        max_value *= 1.2;
        
        if (rx_history && tx_history) {
            // Draw download graph
            g_print("Drawing download graph\n");
            cairo_set_source_rgba(cr, download_color.red, download_color.green, download_color.blue, 0.9);
            cairo_set_line_width(cr, 2.5);
            for (int i = 0; i < MAX_POINTS; i++) {
                int idx = (history_idx + i) % MAX_POINTS;
                double x = (double)i / (MAX_POINTS - 1) * width;
                double y = height - ((rx_history_raw[idx] * 8.0 / 1024.0) / max_value * height);
                if (i == 0) cairo_move_to(cr, x, y);
                else cairo_line_to(cr, x, y);
            }
            cairo_stroke(cr);
            
            // Draw upload graph
            g_print("Drawing upload graph\n");
            cairo_set_source_rgba(cr, upload_color.red, upload_color.green, upload_color.blue, 0.9);
            cairo_set_line_width(cr, 2.5);
            for (int i = 0; i < MAX_POINTS; i++) {
                int idx = (history_idx + i) % MAX_POINTS;
                double x = (double)i / (MAX_POINTS - 1) * width;
                double y = height - ((tx_history_raw[idx] * 8.0 / 1024.0) / max_value * height);
                if (i == 0) cairo_move_to(cr, x, y);
                else cairo_line_to(cr, x, y);
            }
            cairo_stroke(cr);
            
            // Draw legend
            cairo_set_line_width(cr, 1.0);
            
            // Download legend
            cairo_set_source_rgba(cr, download_color.red, download_color.green, download_color.blue, 0.9);
            cairo_rectangle(cr, width - 100, 10, 10, 10);
            cairo_fill(cr);
            
            cairo_set_source_rgba(cr, fg_color.red, fg_color.green, fg_color.blue, 0.9);
            cairo_move_to(cr, width - 85, 20);
            cairo_show_text(cr, "Download (Mbps)");
            
            // Upload legend
            cairo_set_source_rgba(cr, upload_color.red, upload_color.green, upload_color.blue, 0.9);
            cairo_rectangle(cr, width - 100, 30, 10, 10);
            cairo_fill(cr);
            
            cairo_set_source_rgba(cr, fg_color.red, fg_color.green, fg_color.blue, 0.9);
            cairo_move_to(cr, width - 85, 40);
            cairo_show_text(cr, "Upload (Mbps)");
        }
    }

    g_print("draw_network_graph complete\n");
    return FALSE;
}

// Right-click menu handler
static gboolean on_network_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->button == 3) { // Right mouse button
        GtkWidget *menu = GTK_WIDGET(user_data);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

// Interface combo box change handler
static void on_interface_combo_changed(GtkComboBox *widget, gpointer user_data) {
    NetworkUpdateData *data = (NetworkUpdateData*)user_data;
    if (!data) return;
    
    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter(widget, &iter)) {
        GtkTreeModel *model = gtk_combo_box_get_model(widget);
        gint index;
        gtk_tree_model_get(model, &iter, 1, &index, -1);
        data->selected_interface_index = index;
        update_network_widgets(data);
    }
}

// Dialog for Refresh Period
static void show_refresh_dialog(GtkWidget *parent, NetworkUpdateData *data) {
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
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2000); // Default 2 seconds
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
        data->timeout_id = g_timeout_add(data->update_interval, (GSourceFunc)update_network_widgets, data);
    }
    
    gtk_widget_destroy(dialog);
}

static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    // user_data is the drawing_area, but we need the NetworkUpdateData
    // Get the parent grid to find the NetworkUpdateData
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(drawing_area)); // graph_frame -> main_grid
    
    // Get the NetworkUpdateData from the parent's data
    gpointer update_data = g_object_get_data(G_OBJECT(parent), "update_data");
    
    show_refresh_dialog(drawing_area, update_data);
}

GtkWidget* create_network_tab(void) {
    // Initialize network data
    network_data_init();
    
    GtkWidget *main_grid = gtk_grid_new();
    gtk_widget_set_hexpand(main_grid, TRUE);
    gtk_widget_set_vexpand(main_grid, TRUE);
    gtk_widget_set_margin_start(main_grid, 10);
    gtk_widget_set_margin_end(main_grid, 10);
    gtk_widget_set_margin_top(main_grid, 10);
    gtk_widget_set_margin_bottom(main_grid, 10);
    gtk_grid_set_column_spacing(GTK_GRID(main_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(main_grid), 10);

    // Create interface selection combo box
    GtkWidget *combo_label = gtk_label_new("Interface:");
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

    // Create download/upload speed labels
    GtkWidget *download_label = gtk_label_new("Download:");
    gtk_widget_set_halign(download_label, GTK_ALIGN_END);
    
    GtkWidget *download_value = gtk_label_new("0.0 Mbps");
    gtk_widget_set_halign(download_value, GTK_ALIGN_START);
    
    GtkWidget *upload_label = gtk_label_new("Upload:");
    gtk_widget_set_halign(upload_label, GTK_ALIGN_END);
    
    GtkWidget *upload_value = gtk_label_new("0.0 Mbps");
    gtk_widget_set_halign(upload_value, GTK_ALIGN_START);
    
    // Create network info grid
    GtkWidget *info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(info_grid), 15);
    gtk_grid_set_row_spacing(GTK_GRID(info_grid), 8);
    
    // Add network info labels
    int row = 0;
    
    // Add interface type
    GtkWidget *type_label = gtk_label_new("Type:");
    gtk_widget_set_halign(type_label, GTK_ALIGN_START);
    GtkWidget *type_value = gtk_label_new("Unknown");
    gtk_widget_set_halign(type_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), type_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), type_value, 1, row, 1, 1);
    row++;
    
    // Add IP address
    GtkWidget *ip_label = gtk_label_new("IP Address:");
    gtk_widget_set_halign(ip_label, GTK_ALIGN_START);
    GtkWidget *ip_value = gtk_label_new("Not connected");
    gtk_widget_set_halign(ip_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), ip_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ip_value, 1, row, 1, 1);
    row++;
    
    // Add MAC Address
    GtkWidget *mac_label = gtk_label_new("MAC Address:");
    gtk_widget_set_halign(mac_label, GTK_ALIGN_START);
    GtkWidget *mac_value = gtk_label_new("--:--:--:--:--:--");
    gtk_widget_set_halign(mac_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), mac_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), mac_value, 1, row, 1, 1);
    row++;

    // Add MTU
    GtkWidget *mtu_label = gtk_label_new("MTU:");
    gtk_widget_set_halign(mtu_label, GTK_ALIGN_START);
    GtkWidget *mtu_value = gtk_label_new("N/A");
    gtk_widget_set_halign(mtu_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), mtu_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), mtu_value, 1, row, 1, 1);
    row++;

    // Add Link Speed
    GtkWidget *link_speed_label = gtk_label_new("Link Speed:");
    gtk_widget_set_halign(link_speed_label, GTK_ALIGN_START);
    GtkWidget *link_speed_value = gtk_label_new("N/A");
    gtk_widget_set_halign(link_speed_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), link_speed_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), link_speed_value, 1, row, 1, 1);
    row++;
    
    // Add total download
    GtkWidget *total_download_label = gtk_label_new("Total Download:");
    gtk_widget_set_halign(total_download_label, GTK_ALIGN_START);
    GtkWidget *total_download_value = gtk_label_new("0.00 MB");
    gtk_widget_set_halign(total_download_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), total_download_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), total_download_value, 1, row, 1, 1);
    row++;
    
    // Add total upload
    GtkWidget *total_upload_label = gtk_label_new("Total Upload:");
    gtk_widget_set_halign(total_upload_label, GTK_ALIGN_START);
    GtkWidget *total_upload_value = gtk_label_new("0.00 MB");
    gtk_widget_set_halign(total_upload_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), total_upload_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), total_upload_value, 1, row, 1, 1);
    row++;
    
    // Create info frame
    GtkWidget *info_frame = gtk_frame_new("Network Information");
    gtk_frame_set_shadow_type(GTK_FRAME(info_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(info_frame), info_grid);

    // Create right-click menu
    GtkWidget *popup_menu = gtk_menu_new();
    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Set Refresh Rate");
    gtk_menu_shell_append(GTK_MENU_SHELL(popup_menu), refresh_item);
    g_signal_connect(G_OBJECT(refresh_item), "activate", G_CALLBACK(on_refresh_activate), drawing_area);
    gtk_widget_show_all(popup_menu);

    // Add widgets to main grid
    gtk_grid_attach(GTK_GRID(main_grid), combo_box, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(main_grid), graph_frame, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(main_grid), download_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), download_value, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), upload_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), upload_value, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), info_frame, 0, 4, 2, 1);
    
    // Create update data
    NetworkUpdateData *data = g_new0(NetworkUpdateData, 1);
    data->drawing_area = drawing_area;
    data->download_speed_label = download_value;
    data->upload_speed_label = upload_value;
    data->interface_combo = combo;
    data->interface_type_value = type_value;
    data->ip_address_value = ip_value;
    data->mac_address_value = mac_value;
    data->mtu_value = mtu_value;
    data->link_speed_value = link_speed_value;
    data->total_download_value = total_download_value;
    data->total_upload_value = total_upload_value;
    data->selected_interface_index = 0;
    data->update_interval = 2000;  // 2 seconds
    
    // Connect signals
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_network_graph), data);
    g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(on_interface_combo_changed), data);
    g_signal_connect(G_OBJECT(drawing_area), "button-press-event", G_CALLBACK(on_network_tab_button_press), popup_menu);
    
    // Add events
    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    
    // Set up update timer
    data->timeout_id = g_timeout_add(data->update_interval, (GSourceFunc)update_network_widgets, data);
    
    // Store data in main_grid for cleanup
    g_object_set_data_full(G_OBJECT(main_grid), "update_data", data, cleanup_network_update_data);
    
    // Initial update
    update_network_widgets(data);
    
    return main_grid;
}

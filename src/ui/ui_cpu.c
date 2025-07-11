#include "ui/ui_cpu.h"
#include "cpu/cpu_data.h"
#include <cairo.h>
#include <math.h>

static gboolean draw_cpu_graph(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean update_cpu_widgets(gpointer user_data);
static gboolean on_cpu_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);

typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *cpu_label_value;
    GtkWidget *cpu_freq_value;
    guint update_interval;
    guint timeout_id;
} CpuUpdateData;

// Dialog for Active Logical Processors
static void show_processors_dialog(GtkWidget *parent) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Active Logical Processors",
                                                  GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Show Individual Graphs", GTK_RESPONSE_YES,
                                                  "Show Combined Graph", GTK_RESPONSE_NO,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    char message[100];
    snprintf(message, sizeof(message), "Number of logical processors: %d", get_cpu_threads());
    
    GtkWidget *label = gtk_label_new(message);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    
    // Create a grid to show current CPU usage for each core
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    
    int num_cores = get_cpu_threads();
    int columns = 4; // Show in 4 columns
    
    for (int i = 0; i < num_cores; i++) {
        int row = i / columns;
        int col = i % columns;
        
        char core_label[32];
        snprintf(core_label, sizeof(core_label), "CPU %d:", i);
        
        char usage_str[32];
        snprintf(usage_str, sizeof(usage_str), "%.1f%%", get_cpu_usage_by_core(i));
        
        GtkWidget *label_core = gtk_label_new(core_label);
        GtkWidget *label_usage = gtk_label_new(usage_str);
        
        gtk_widget_set_halign(label_core, GTK_ALIGN_START);
        gtk_widget_set_halign(label_usage, GTK_ALIGN_START);
        
        gtk_grid_attach(GTK_GRID(grid), label_core, col*2, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), label_usage, col*2+1, row, 1, 1);
    }
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_YES) {
        set_show_per_cpu_graphs(TRUE);
        gtk_widget_queue_draw(parent);
    } else if (result == GTK_RESPONSE_NO) {
        set_show_per_cpu_graphs(FALSE);
        gtk_widget_queue_draw(parent);
    }
    
    gtk_widget_destroy(dialog);
}

// Dialog for Refresh Period
static void show_refresh_dialog(GtkWidget *parent, gpointer user_data) {
    CpuUpdateData *data = (CpuUpdateData*)user_data;
    
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
        data->timeout_id = g_timeout_add(data->update_interval, (GSourceFunc)update_cpu_widgets, data);
    }
    
    gtk_widget_destroy(dialog);
}

static void on_processors_activate(GtkMenuItem *item, gpointer user_data) {
    // user_data is the drawing_area
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    show_processors_dialog(drawing_area);
}

static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    // user_data is the drawing_area, but we need the CpuUpdateData
    // Get the parent grid to find the CpuUpdateData
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(drawing_area)); // graph_frame -> main_grid
    
    // Get the CpuUpdateData from the parent's data
    gpointer update_data = g_object_get_data(G_OBJECT(parent), "update_data");
    
    show_refresh_dialog(drawing_area, update_data);
}

GtkWidget* create_cpu_tab(void) {
    GtkWidget *main_grid = gtk_grid_new();
    gtk_widget_set_hexpand(main_grid, TRUE);
    gtk_widget_set_vexpand(main_grid, TRUE);
    gtk_widget_set_margin_start(main_grid, 10);
    gtk_widget_set_margin_end(main_grid, 10);
    gtk_widget_set_margin_top(main_grid, 10);
    gtk_widget_set_margin_bottom(main_grid, 10);
    gtk_grid_set_column_spacing(GTK_GRID(main_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(main_grid), 10);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 250);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_cpu_graph), NULL);

    // Create the right-click menu
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *processors_item = gtk_menu_item_new_with_label("Active Logical Processors");
    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Refresh Period");
    g_signal_connect(processors_item, "activate", G_CALLBACK(on_processors_activate), drawing_area);
    g_signal_connect(refresh_item, "activate", G_CALLBACK(on_refresh_activate), drawing_area);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), processors_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);
    gtk_widget_show_all(menu);
    
    // Connect the right-click event to the drawing area
    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_cpu_tab_button_press), menu);

    GtkWidget *graph_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(graph_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(graph_frame), drawing_area);

    GtkWidget *info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(info_grid), 15);
    gtk_grid_set_row_spacing(GTK_GRID(info_grid), 8);
    gtk_widget_set_valign(info_grid, GTK_ALIGN_START);

    GtkWidget *info_frame = gtk_frame_new("CPU Specifications");
    gtk_frame_set_shadow_type(GTK_FRAME(info_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(info_frame), info_grid);

    int row = 0;
    #define ADD_CPU_SPEC(label_text, value_text) \
        do { \
            GtkWidget *label = gtk_label_new(label_text); \
            gtk_widget_set_halign(label, GTK_ALIGN_START); \
            GtkWidget *value = gtk_label_new(value_text); \
            gtk_widget_set_halign(value, GTK_ALIGN_START); \
            gtk_label_set_selectable(GTK_LABEL(value), TRUE); \
            gtk_grid_attach(GTK_GRID(info_grid), label, 0, row, 1, 1); \
            gtk_grid_attach(GTK_GRID(info_grid), value, 1, row, 1, 1); \
            row++; \
        } while (0)
        
    ADD_CPU_SPEC("Vendor:", get_cpu_vendor_id());
    ADD_CPU_SPEC("Model:", get_cpu_model());
    ADD_CPU_SPEC("Architecture:", get_cpu_architecture());
    char cores_str[32];
    snprintf(cores_str, sizeof(cores_str), "%d Cores, %d Threads", get_cpu_cores(), get_cpu_threads());
    ADD_CPU_SPEC("Processors:", cores_str);
    char family_str[32];
    snprintf(family_str, sizeof(family_str), "Family %s, Stepping %s", get_cpu_family(), get_cpu_stepping());
    ADD_CPU_SPEC("Family/Stepping:", family_str);
    ADD_CPU_SPEC("BogoMIPS:", get_cpu_bogomips());
    ADD_CPU_SPEC("Address Sizes:", get_cpu_address_sizes());
    char cache_str[32];
    snprintf(cache_str, sizeof(cache_str), "%sK", get_cpu_cache_info());
    ADD_CPU_SPEC("Cache Size:", cache_str);

    GtkWidget *cpu_freq_label = gtk_label_new("Frequency:");
    gtk_widget_set_halign(cpu_freq_label, GTK_ALIGN_START);
    GtkWidget *cpu_freq_value = gtk_label_new("N/A");
    gtk_widget_set_halign(cpu_freq_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(info_grid), cpu_freq_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), cpu_freq_value, 1, row, 1, 1);

    GtkWidget *usage_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *cpu_label_title = gtk_label_new("Usage:");
    GtkWidget *cpu_label_value = gtk_label_new("0.0%");

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "label.cpu-usage { font-weight: bold; font-size: 14px; }", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(cpu_label_value);
    gtk_style_context_add_class(context, "cpu-usage");
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);

    gtk_box_pack_start(GTK_BOX(usage_box), cpu_label_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(usage_box), cpu_label_value, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(main_grid), usage_box, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), graph_frame, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), info_frame, 1, 0, 1, 2);
    
    CpuUpdateData *update_data = g_new(CpuUpdateData, 1);
    update_data->drawing_area = drawing_area;
    update_data->cpu_label_value = cpu_label_value;
    update_data->cpu_freq_value = cpu_freq_value;
    update_data->update_interval = 1000; // Default to 1 second
    
    // Store the update_data in the main_grid's data
    g_object_set_data(G_OBJECT(main_grid), "update_data", update_data);
    
    // Set up the timeout with the initial interval
    update_data->timeout_id = g_timeout_add(update_data->update_interval, (GSourceFunc)update_cpu_widgets, update_data);
    g_signal_connect(main_grid, "destroy", G_CALLBACK(g_free), update_data);

    return main_grid;
}

static gboolean on_cpu_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right button is 3
        GtkWidget *menu = GTK_WIDGET(user_data);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

static gboolean update_cpu_widgets(gpointer user_data) {
    CpuUpdateData *data = (CpuUpdateData*)user_data;
    cpu_data_update();

    char cpu_str[32];
    snprintf(cpu_str, sizeof(cpu_str), "%.1f%%", get_current_cpu_usage());
    gtk_label_set_text(GTK_LABEL(data->cpu_label_value), cpu_str);

    char freq_str[32];
    gdouble freq_mhz = get_cpu_freq_mhz();
    if (freq_mhz > 1000) snprintf(freq_str, sizeof(freq_str), "%.2f GHz", freq_mhz / 1000.0);
    else snprintf(freq_str, sizeof(freq_str), "%.0f MHz", freq_mhz);
    gtk_label_set_text(GTK_LABEL(data->cpu_freq_value), freq_str);

    gtk_widget_queue_draw(data->drawing_area);
    return G_SOURCE_CONTINUE;
}

static gboolean draw_cpu_graph(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    
    GtkStyleContext *style_context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color, fg_color, accent_color;

    // Provide sensible fallbacks
    gdk_rgba_parse(&bg_color, "rgb(24, 25, 26)");
    gdk_rgba_parse(&fg_color, "rgb(238, 238, 236)");
    gdk_rgba_parse(&accent_color, "rgb(53, 132, 228)");

    // Try to get theme colors
    gtk_style_context_lookup_color(style_context, "theme_bg_color", &bg_color);
    gtk_style_context_lookup_color(style_context, "theme_fg_color", &fg_color);
    gtk_style_context_lookup_color(style_context, "theme_selected_bg_color", &accent_color);

    gdk_cairo_set_source_rgba(cr, &bg_color);
    cairo_paint(cr);

    GdkRGBA grid_color = fg_color;
    grid_color.alpha = 0.2;
    
    if (get_show_per_cpu_graphs()) {
        int num_cores = get_cpu_threads();
        int rows = (num_cores + 1) / 2;  // Ceiling division
        int cols = (num_cores > 1) ? 2 : 1;
        
        int graph_width = width / cols;
        int graph_height = height / rows;
        
        for (int i = 0; i < num_cores; i++) {
            int row = i / cols;
            int col = i % cols;
            
            // Save the current state
            cairo_save(cr);
            
            // Translate to the position of this CPU's graph
            cairo_translate(cr, col * graph_width, row * graph_height);
            
            // Draw grid lines
            gdk_cairo_set_source_rgba(cr, &grid_color);
            cairo_set_line_width(cr, 0.8);
            for (int j = 1; j < 4; j++) {
                double y = graph_height * j / 4.0;
                cairo_move_to(cr, 0, y);
                cairo_line_to(cr, graph_width, y);
            }
            cairo_stroke(cr);
            
            const gdouble* history = get_cpu_usage_history_by_core(i);
            if (history) {
                gint history_idx = get_cpu_usage_history_index();
                
                // Graph fill
                cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, graph_height);
                cairo_pattern_add_color_stop_rgba(fill, 0, accent_color.red, accent_color.green, accent_color.blue, 0.7);
                cairo_pattern_add_color_stop_rgba(fill, 1, accent_color.red, accent_color.green, accent_color.blue, 0.1);
                cairo_set_source(cr, fill);
                
                cairo_move_to(cr, 0, graph_height);
                for (int j = 0; j < MAX_POINTS; j++) {
                    int idx = (history_idx + j) % MAX_POINTS;
                    double x = (double)j / (MAX_POINTS - 1) * graph_width;
                    double y = graph_height - (history[idx] / 100.0 * graph_height);
                    cairo_line_to(cr, x, y);
                }
                cairo_line_to(cr, graph_width, graph_height);
                cairo_close_path(cr);
                cairo_fill(cr);
                cairo_pattern_destroy(fill);
                
                // Graph line
                cairo_set_source_rgba(cr, accent_color.red, accent_color.green, accent_color.blue, 0.9);
                cairo_set_line_width(cr, 2.0);
                for (int j = 0; j < MAX_POINTS; j++) {
                    int idx = (history_idx + j) % MAX_POINTS;
                    double x = (double)j / (MAX_POINTS - 1) * graph_width;
                    double y = graph_height - (history[idx] / 100.0 * graph_height);
                    if (j == 0) cairo_move_to(cr, x, y);
                    else cairo_line_to(cr, x, y);
                }
                cairo_stroke(cr);
            }
            
            // Draw CPU number label
            cairo_set_source_rgba(cr, fg_color.red, fg_color.green, fg_color.blue, 0.9);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 12);
            
            char cpu_label[32];
            snprintf(cpu_label, sizeof(cpu_label), "CPU %d: %.1f%%", i, get_cpu_usage_by_core(i));
            
            cairo_move_to(cr, 5, 15);
            cairo_show_text(cr, cpu_label);
            
            // Restore the saved state
            cairo_restore(cr);
        }
    } else {
        // Draw grid lines for the main graph
        gdk_cairo_set_source_rgba(cr, &grid_color);
        cairo_set_line_width(cr, 0.8);
        for (int i = 1; i < 4; i++) {
            double y = height * i / 4.0;
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, width, y);
        }
        cairo_stroke(cr);
        
        const gdouble* history = get_cpu_usage_history();
        gint history_idx = get_cpu_usage_history_index();
        
        cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, height);
        cairo_pattern_add_color_stop_rgba(fill, 0, accent_color.red, accent_color.green, accent_color.blue, 0.7);
        cairo_pattern_add_color_stop_rgba(fill, 1, accent_color.red, accent_color.green, accent_color.blue, 0.1);
        cairo_set_source(cr, fill);

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

    return FALSE;
}

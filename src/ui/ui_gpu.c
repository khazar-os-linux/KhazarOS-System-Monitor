#include "ui/ui_gpu.h"
#include "gpu/gpu_data.h"
#include <cairo.h>
#include <math.h>

// Forward declarations
static gboolean draw_filled_graph(GtkWidget *widget, cairo_t *cr, gpointer user_data);
static gboolean update_gpu_widgets(gpointer user_data);
static gboolean on_gpu_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);

typedef struct {
    GtkWidget *gpu_area;
    GtkWidget *vram_area;
    GtkWidget *usage_value_label;
    GtkWidget *vram_value_label;
    GtkWidget *gpu_name_value;
    GtkWidget *vram_total_value;
    GtkWidget *vendor_value;
    GtkWidget *driver_value;
    guint update_interval;
    guint timeout_id;
    gint gpu_index;  // Index of the GPU this data belongs to
} GpuUpdateData;

// Cleanup function for the update data
static void cleanup_gpu_update_data(gpointer data) {
    if (!data) return;
    
    GpuUpdateData *update_data = (GpuUpdateData*)data;
    
    if (update_data->timeout_id > 0) {
        g_source_remove(update_data->timeout_id);
        update_data->timeout_id = 0;
    }
    
    g_free(update_data);
}

//---------------------------------------------------------------------------
static gboolean draw_filled_graph(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GpuUpdateData *data = (GpuUpdateData*)user_data;
    if (!data) return FALSE;
    GtkAllocation allocation; gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;

    // Colors similar to CPU graph
    GdkRGBA bg_color; gdk_rgba_parse(&bg_color, "rgb(24,25,26)");
    GdkRGBA fg_color; gdk_rgba_parse(&fg_color, "rgb(238,238,236)");
    GdkRGBA usage_color; gdk_rgba_parse(&usage_color, "rgb(52,101,164)");
    GdkRGBA vram_color; gdk_rgba_parse(&vram_color, "rgb(233,185,110)");

    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    gtk_style_context_lookup_color(context, "theme_bg_color", &bg_color);
    gtk_style_context_lookup_color(context, "theme_fg_color", &fg_color);

    gdk_cairo_set_source_rgba(cr, &bg_color);
    cairo_paint(cr);

    // grid lines (like CPU)
    GdkRGBA grid_color = fg_color; grid_color.alpha = 0.2;
    gdk_cairo_set_source_rgba(cr, &grid_color);
    cairo_set_line_width(cr, 0.8);
    for (int i=1;i<4;i++) { double y = height * i / 4.0; cairo_move_to(cr,0,y); cairo_line_to(cr,width,y);} cairo_stroke(cr);

    gboolean is_gpu_graph = (widget == data->gpu_area);
    
    // Get GPU info for this specific GPU
    const GPUInfo *gpu_info = gpu_get_info(data->gpu_index);
    if (!gpu_info) return FALSE;
    
    const gdouble *hist = is_gpu_graph ? gpu_info->usage_history : gpu_info->vram_history;
    gint idx = gpu_info->history_index;

    // find max for scaling
    gdouble max_val = 100.0; // percentages

    // Choose color
    GdkRGBA col = is_gpu_graph ? usage_color : vram_color;
    // Build filled area path
    cairo_set_source_rgba(cr, col.red, col.green, col.blue, 0.3);
    cairo_move_to(cr, 0, height);
    for (int i=0;i<GPU_MAX_POINTS;i++) {
        int h=(idx+i)%GPU_MAX_POINTS;
        double x=(double)i/(GPU_MAX_POINTS-1)*width;
        double y=height-(hist[h]/max_val*height);
        cairo_line_to(cr,x,y);
    }
    cairo_line_to(cr,width,height);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Draw edge line darker
    cairo_set_source_rgba(cr, col.red, col.green, col.blue, 0.9);
    cairo_set_line_width(cr,2.0);
    for(int i=0;i<GPU_MAX_POINTS;i++){
        int h=(idx+i)%GPU_MAX_POINTS; double x=(double)i/(GPU_MAX_POINTS-1)*width; double y=height-(hist[h]/max_val*height); if(i==0) cairo_move_to(cr,x,y); else cairo_line_to(cr,x,y);} cairo_stroke(cr);

    // Legend title
    cairo_set_line_width(cr,1.0);
    cairo_set_source_rgba(cr, col.red, col.green, col.blue,0.9);
    cairo_rectangle(cr,width-90,10,10,10); cairo_fill(cr);
    cairo_set_source_rgba(cr, fg_color.red, fg_color.green, fg_color.blue,0.9);
    cairo_move_to(cr,width-75,20);
    cairo_show_text(cr, is_gpu_graph?"GPU %":"VRAM %");

    return FALSE;
}

static gboolean update_gpu_widgets(gpointer user_data) {
    GpuUpdateData *data = (GpuUpdateData*)user_data;
    if (!data) return G_SOURCE_REMOVE;

    gpu_data_update();
    
    // Get GPU info for this specific GPU
    const GPUInfo *gpu_info = gpu_get_info(data->gpu_index);
    if (!gpu_info) return G_SOURCE_CONTINUE;

    char buf[64];
    // VRAM usage string – handle Intel integrated GPUs specially (shared memory)
    if (g_strcmp0(gpu_info->vendor, "Intel") == 0 || gpu_info->vram_total_mb == 0) {
        // Show N/A or Shared memory where dedicated VRAM metric is meaningless
        gtk_label_set_text(GTK_LABEL(data->vram_value_label), "Shared");
        gtk_label_set_text(GTK_LABEL(data->vram_total_value), "Shared");
    } else {
        snprintf(buf, sizeof(buf), "%.0f / %.0f MB (%.1f%%)",
                 gpu_info->vram_used_mb,
                 gpu_info->vram_total_mb,
                 gpu_info->vram_usage_percent);
        gtk_label_set_text(GTK_LABEL(data->vram_value_label), buf);

        snprintf(buf, sizeof(buf), "%.0f MB", gpu_info->vram_total_mb);
        gtk_label_set_text(GTK_LABEL(data->vram_total_value), buf);
    }

    // Update name after VRAM handling to avoid redundant buffer reuse
    gtk_label_set_text(GTK_LABEL(data->gpu_name_value), gpu_info->name);

    gtk_label_set_text(GTK_LABEL(data->vendor_value), gpu_info->vendor);
    gtk_label_set_text(GTK_LABEL(data->driver_value), gpu_info->driver_version);

    snprintf(buf, sizeof(buf), "%.1f%%", gpu_info->usage_percent);
    gtk_label_set_text(GTK_LABEL(data->usage_value_label), buf);

    gtk_widget_queue_draw(data->gpu_area);
    gtk_widget_queue_draw(data->vram_area);
    return G_SOURCE_CONTINUE;
}

static gboolean on_gpu_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->button == 3) {
        GtkWidget *menu = GTK_WIDGET(user_data);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

// Refresh dialog similar to others
static void show_refresh_dialog(GtkWidget *parent, GpuUpdateData *data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Refresh Period",
                                                  GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Apply", GTK_RESPONSE_APPLY,
                                                  "Cancel", GTK_RESPONSE_CANCEL,
                                                  NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *label = gtk_label_new("Update interval (ms):");
    GtkWidget *spin = gtk_spin_button_new_with_range(200, 5000, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), data->update_interval);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(content), hbox);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY) {
        data->update_interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        if (data->timeout_id) g_source_remove(data->timeout_id);
        data->timeout_id = g_timeout_add(data->update_interval, update_gpu_widgets, data);
    }
    gtk_widget_destroy(dialog);
}

static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(drawing_area));
    GpuUpdateData *data = g_object_get_data(G_OBJECT(parent), "gpu_update_data");
    show_refresh_dialog(drawing_area, data);
}

// Create a single GPU tab for the specified GPU index
static GtkWidget* create_single_gpu_tab(gint gpu_index) {
    const GPUInfo *gpu_info = gpu_get_info(gpu_index);
    if (!gpu_info) return NULL;
    
    GtkWidget *main_grid = gtk_grid_new();
    gtk_widget_set_margin_start(main_grid, 10);
    gtk_widget_set_margin_end(main_grid, 10);
    gtk_widget_set_margin_top(main_grid, 10);
    gtk_widget_set_margin_bottom(main_grid, 10);
    gtk_grid_set_column_spacing(GTK_GRID(main_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(main_grid), 10);

    // GPU usage graph
    GtkWidget *usage_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(usage_area, -1, 180);
    gtk_widget_set_hexpand(usage_area, TRUE);
    gtk_widget_set_vexpand(usage_area, TRUE);
    GtkWidget *usage_frame = gtk_frame_new("GPU Usage %");
    gtk_frame_set_shadow_type(GTK_FRAME(usage_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(usage_frame), usage_area);

    // VRAM usage graph
    GtkWidget *vram_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(vram_area, -1, 180);
    gtk_widget_set_hexpand(vram_area, TRUE);
    gtk_widget_set_vexpand(vram_area, TRUE);
    GtkWidget *vram_frame = gtk_frame_new("VRAM %");
    gtk_frame_set_shadow_type(GTK_FRAME(vram_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(vram_frame), vram_area);

    // Specs grid
    GtkWidget *spec_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(spec_grid), 15);
    gtk_grid_set_row_spacing(GTK_GRID(spec_grid), 8);

    int row = 0;
    GtkWidget *name_label = gtk_label_new("Name:");
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    GtkWidget *name_value = gtk_label_new(gpu_info->name);
    gtk_widget_set_halign(name_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(spec_grid), name_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(spec_grid), name_value, 1, row, 1, 1);
    row++;

    // Values to be placed in spec grid only
    GtkWidget *usage_value = gtk_label_new("0.0%");
    gtk_widget_set_halign(usage_value, GTK_ALIGN_START);
    GtkWidget *vram_value = gtk_label_new("0 / 0 MB (0%)");
    gtk_widget_set_halign(vram_value, GTK_ALIGN_START);

    // GPU usage row
    GtkWidget *gpu_usage_label = gtk_label_new("GPU Usage:");
    gtk_widget_set_halign(gpu_usage_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(spec_grid), gpu_usage_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(spec_grid), usage_value, 1, row, 1, 1);
    row++;

    // VRAM usage row
    GtkWidget *vram_usage_label = gtk_label_new("VRAM Usage:");
    gtk_widget_set_halign(vram_usage_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(spec_grid), vram_usage_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(spec_grid), vram_value, 1, row, 1, 1);
    row++;

    GtkWidget *vram_total_label = gtk_label_new("VRAM Total:");
    gtk_widget_set_halign(vram_total_label, GTK_ALIGN_START);
    GtkWidget *vram_total_value = gtk_label_new("0 MB");
    gtk_widget_set_halign(vram_total_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(spec_grid), vram_total_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(spec_grid), vram_total_value, 1, row, 1, 1);
    row++;

    // Vendor row
    GtkWidget *vendor_label = gtk_label_new("Vendor:");
    gtk_widget_set_halign(vendor_label, GTK_ALIGN_START);
    GtkWidget *vendor_value = gtk_label_new(gpu_info->vendor);
    gtk_widget_set_halign(vendor_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(spec_grid), vendor_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(spec_grid), vendor_value, 1, row, 1, 1);
    row++;

    // Driver version row
    GtkWidget *drv_label = gtk_label_new("Driver:");
    gtk_widget_set_halign(drv_label, GTK_ALIGN_START);
    GtkWidget *drv_value = gtk_label_new(gpu_info->driver_version);
    gtk_widget_set_halign(drv_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(spec_grid), drv_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(spec_grid), drv_value, 1, row, 1, 1);
    row++;

    GtkWidget *spec_frame = gtk_frame_new("GPU Specifications");
    gtk_frame_set_shadow_type(GTK_FRAME(spec_frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(spec_frame), spec_grid);

    // Popup menu
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Refresh Period");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);
    g_signal_connect(refresh_item, "activate", G_CALLBACK(on_refresh_activate), usage_area);
    gtk_widget_show_all(menu);

    // Assemble main_grid
    // Left column: graphs and labels
    gtk_grid_attach(GTK_GRID(main_grid), usage_frame, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), vram_frame, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), spec_frame, 1, 0, 1, 2);

    // Update data struct
    GpuUpdateData *data = g_new0(GpuUpdateData, 1);
    data->gpu_area = usage_area;
    data->vram_area = vram_area;
    data->usage_value_label = usage_value;
    data->vram_value_label = vram_value;
    data->gpu_name_value = name_value;
    data->vram_total_value = vram_total_value;
    data->vendor_value = vendor_value;
    data->driver_value = drv_value;
    data->update_interval = 2000;
    data->gpu_index = gpu_index;

    // Connect the draw signals
    g_signal_connect(usage_area, "draw", G_CALLBACK(draw_filled_graph), data);
    g_signal_connect(vram_area, "draw", G_CALLBACK(draw_filled_graph), data);
    
    gtk_widget_add_events(vram_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(vram_area, "button-press-event", G_CALLBACK(on_gpu_tab_button_press), menu);

    data->timeout_id = g_timeout_add(data->update_interval, update_gpu_widgets, data);
    g_object_set_data_full(G_OBJECT(main_grid), "gpu_update_data", data, cleanup_gpu_update_data);

    update_gpu_widgets(data);

    return main_grid;
}

GtkWidget* create_gpu_tab(void) {
    gpu_data_init();
    // Run one immediate update so we have VRAM values before building UI
    gpu_data_update();

    // Build a list of valid GPU indices (skip dummy/empty GPUs)
    gint gpu_total = gpu_get_count();
    gint valid_indices[16]; // supports up to 16 GPUs
    gint valid_count = 0;
    for (gint i = 0; i < gpu_total; i++) {
        const GPUInfo *info = gpu_get_info(i);
        if (!info) continue;

        // Skip NVIDIA adapters that report no VRAM (likely headless / off)
        if (g_strcmp0(info->vendor, "NVIDIA") == 0 && info->vram_total_mb == 0) {
            continue;
        }

        // Skip completely empty placeholder GPUs
        if (info->vram_total_mb == 0 && info->usage_percent == 0) {
            continue;
        }

        valid_indices[valid_count++] = i;
    }

    if (valid_count == 0) {
        // No valid GPUs – return a label indicating that
        return gtk_label_new("No GPU detected");
    }

    // If only one valid GPU, show single tab content directly
    if (valid_count == 1) {
        return create_single_gpu_tab(valid_indices[0]);
    }

    // Otherwise, create notebook with one tab per GPU
    GtkWidget *gpu_notebook = gtk_notebook_new();
    for (gint idx = 0; idx < valid_count; idx++) {
        gint i = valid_indices[idx];
        const GPUInfo *gpu_info = gpu_get_info(i);
        GtkWidget *gpu_tab = create_single_gpu_tab(i);
        if (!gpu_tab) continue;
        char tab_label[64];
        snprintf(tab_label, sizeof(tab_label), "GPU %d: %s", i, gpu_info->vendor);
        gtk_notebook_append_page(GTK_NOTEBOOK(gpu_notebook), gpu_tab, gtk_label_new(tab_label));
    }

    return gpu_notebook;
} 
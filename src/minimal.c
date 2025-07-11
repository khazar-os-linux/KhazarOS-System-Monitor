#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_POINTS 60

static gdouble cpu_usage_history[MAX_POINTS] = {0.0};
static gint cpu_usage_index = 0;
static gdouble current_cpu_usage = 0.0;
static gulong prev_cpu_total = 0;
static gulong prev_cpu_idle = 0;

// Dialog for Active Logical Processors
static void show_processors_dialog(GtkWidget *parent) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Active Logical Processors",
                                                  GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    char message[100];
    snprintf(message, sizeof(message), "Number of logical processors: %d", cpu_count);
    
    GtkWidget *label = gtk_label_new(message);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Dialog for Refresh Period
static void show_refresh_dialog(GtkWidget *parent, gpointer user_data) {
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
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1000); // Default 1 second
    
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);
    
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_APPLY) {
        guint interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        g_print("Setting refresh interval to %d ms\n", interval);
        // In a real app, we would update the refresh interval here
    }
    
    gtk_widget_destroy(dialog);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    
    // Background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);
    
    // Grid lines
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.3);
    cairo_set_line_width(cr, 0.8);
    for (int i = 1; i < 4; i++) {
        double y = height * i / 4.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
    }
    cairo_stroke(cr);
    
    // Graph fill
    cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(fill, 0, 0.2, 0.5, 0.9, 0.7);
    cairo_pattern_add_color_stop_rgba(fill, 1, 0.2, 0.5, 0.9, 0.1);
    cairo_set_source(cr, fill);
    
    cairo_move_to(cr, 0, height);
    for (int i = 0; i < MAX_POINTS; i++) {
        int idx = (cpu_usage_index + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (cpu_usage_history[idx] / 100.0 * height);
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);
    
    // Graph line
    cairo_set_source_rgba(cr, 0.2, 0.6, 1.0, 0.9);
    cairo_set_line_width(cr, 2.5);
    for (int i = 0; i < MAX_POINTS; i++) {
        int idx = (cpu_usage_index + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (cpu_usage_history[idx] / 100.0 * height);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
    
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right button
        GtkWidget *menu = GTK_WIDGET(user_data);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

static void update_cpu_data(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;
    
    char line[256];
    gulong user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    
    if (fgets(line, sizeof(line), fp) && strncmp(line, "cpu ", 4) == 0) {
        sscanf(line, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    }
    fclose(fp);
    
    gulong total = user + nice + system + idle + iowait + irq + softirq + steal;
    gulong idle_total = idle + iowait;
    
    if (prev_cpu_total > 0 && total > prev_cpu_total) {
        gulong total_diff = total - prev_cpu_total;
        gulong idle_diff = idle_total - prev_cpu_idle;
        current_cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
        cpu_usage_history[cpu_usage_index] = current_cpu_usage > 0 ? current_cpu_usage : 0;
        cpu_usage_index = (cpu_usage_index + 1) % MAX_POINTS;
    }
    
    prev_cpu_total = total;
    prev_cpu_idle = idle_total;
}

static gboolean update_data(gpointer user_data) {
    GtkWidget *drawing_area = GTK_WIDGET(user_data);
    
    update_cpu_data();
    gtk_widget_queue_draw(drawing_area);
    return G_SOURCE_CONTINUE;
}

static void on_processors_activate(GtkMenuItem *item, gpointer user_data) {
    show_processors_dialog(GTK_WIDGET(user_data));
}

static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    show_refresh_dialog(GTK_WIDGET(user_data), NULL);
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *menu, *processors_item, *refresh_item;
    
    gtk_init(&argc, &argv);
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "CPU Graph");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 250);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(on_draw), NULL);
    
    // Create right-click menu
    menu = gtk_menu_new();
    processors_item = gtk_menu_item_new_with_label("Active Logical Processors");
    refresh_item = gtk_menu_item_new_with_label("Refresh Period");
    g_signal_connect(processors_item, "activate", G_CALLBACK(on_processors_activate), drawing_area);
    g_signal_connect(refresh_item, "activate", G_CALLBACK(on_refresh_activate), drawing_area);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), processors_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);
    gtk_widget_show_all(menu);
    
    // Connect right-click event
    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press), menu);
    
    // Add drawing area to window
    gtk_container_add(GTK_CONTAINER(window), drawing_area);
    
    // Set up timer for data updates
    g_timeout_add(1000, update_data, drawing_area);
    
    gtk_widget_show_all(window);
    gtk_main();
    
    return 0;
} 
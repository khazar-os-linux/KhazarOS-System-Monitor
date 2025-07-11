#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_POINTS 60
#define MAX_CPU_CORES 64

// AppData structure for UI updates
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *usage_label;
    GtkWidget *freq_label;
    guint update_interval;
    guint timeout_id;
} AppData;

// CPU data structures
static gdouble cpu_usage_history[MAX_POINTS] = {0.0};
static gdouble per_cpu_usage_history[MAX_CPU_CORES][MAX_POINTS] = {{0.0}};
static gulong prev_cpu_stats[MAX_CPU_CORES][4] = {{0}}; // user, nice, system, idle
static gint cpu_usage_index = 0;
static gdouble current_cpu_usage = 0.0;
static gdouble current_per_cpu_usage[MAX_CPU_CORES] = {0.0};
static gulong prev_cpu_total = 0;
static gulong prev_cpu_idle = 0;
static gchar *cpu_model = NULL;
static gint cpu_cores = 0;
static gint cpu_threads = 0;
static gdouble cpu_freq_mhz = 0.0;
static gchar *cpu_cache_info = NULL;
static gchar *cpu_architecture = NULL;
static gchar *cpu_stepping = NULL;
static gchar *cpu_family = NULL;
static gchar *cpu_vendor_id = NULL;
static gchar *cpu_bogomips = NULL;
static gchar *cpu_address_sizes = NULL;
static gboolean show_per_cpu_graphs = FALSE;

// CPU data functions
static void parse_cpuinfo(void) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *key = strtok(line, "\t:");
        char *value = strtok(NULL, "\n");

        if (key && value) {
            while (*value == ' ' || *value == '\t') value++;
            
            if (strncmp(key, "model name", 10) == 0 && !cpu_model) cpu_model = g_strdup(value);
            else if (strncmp(key, "vendor_id", 9) == 0 && !cpu_vendor_id) cpu_vendor_id = g_strdup(value);
            else if (strncmp(key, "cpu family", 10) == 0 && !cpu_family) cpu_family = g_strdup(value);
            else if (strncmp(key, "stepping", 8) == 0 && !cpu_stepping) cpu_stepping = g_strdup(value);
            else if (strncmp(key, "bogomips", 8) == 0 && !cpu_bogomips) cpu_bogomips = g_strdup(value);
            else if (strncmp(key, "address sizes", 13) == 0 && !cpu_address_sizes) cpu_address_sizes = g_strdup(value);
        }
    }
    fclose(fp);

    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        GHashTable *physical_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "physical id", 11) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (isspace(*colon)) colon++;
                    size_t len = strlen(colon);
                    if (len > 0 && colon[len-1] == '\n') colon[len-1] = '\0';
                    g_hash_table_replace(physical_ids, g_strdup(colon), GINT_TO_POINTER(1));
                }
            }
        }
        cpu_cores = g_hash_table_size(physical_ids);
        g_hash_table_destroy(physical_ids);
        fclose(fp);
    }
    if (cpu_cores <= 0) cpu_cores = 1;

    cpu_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_threads > MAX_CPU_CORES) cpu_threads = MAX_CPU_CORES;
}

static void get_cpu_freq(void) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;
    char line[256];
    gdouble total_freq = 0.0;
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu MHz", 7) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                total_freq += atof(colon + 1);
                count++;
            }
        }
    }
    fclose(fp);
    if (count > 0) cpu_freq_mhz = total_freq / count;
}

static void get_cache_info(void) {
    FILE *fp = fopen("/sys/devices/system/cpu/cpu0/cache/index3/size", "r");
    if (!fp) fp = fopen("/sys/devices/system/cpu/cpu0/cache/index2/size", "r");
    if (fp) {
        char size_str[32];
        if (fgets(size_str, sizeof(size_str), fp)) {
            size_str[strcspn(size_str, "\nK")] = 0; // Remove newline and 'K'
            cpu_cache_info = g_strdup(size_str);
        }
        fclose(fp);
    }
}

static void get_architecture(void) {
    FILE *fp = popen("uname -m", "r");
    if (fp) {
        char arch_str[32];
        if (fgets(arch_str, sizeof(arch_str), fp)) {
            arch_str[strcspn(arch_str, "\n")] = 0;
            cpu_architecture = g_strdup(arch_str);
        }
        pclose(fp);
    }
}

static void cpu_data_init(void) {
    parse_cpuinfo();
    get_cpu_freq();
    get_cache_info();
    get_architecture();
    
    // Initialize the CPU usage history
    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        gulong user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        
        if (fgets(line, sizeof(line), fp) && strncmp(line, "cpu ", 4) == 0) {
            sscanf(line, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        }
        
        // Initialize per-CPU stats
        for (int i = 0; i < cpu_threads; i++) {
            char cpu_line[32];
            snprintf(cpu_line, sizeof(cpu_line), "cpu%d ", i);
            
            // Reset file position
            rewind(fp);
            
            // Find the specific CPU line
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, cpu_line, strlen(cpu_line)) == 0) {
                    gulong cpu_user = 0, cpu_nice = 0, cpu_system = 0, cpu_idle = 0;
                    sscanf(line, "%*s %lu %lu %lu %lu", &cpu_user, &cpu_nice, &cpu_system, &cpu_idle);
                    
                    prev_cpu_stats[i][0] = cpu_user;
                    prev_cpu_stats[i][1] = cpu_nice;
                    prev_cpu_stats[i][2] = cpu_system;
                    prev_cpu_stats[i][3] = cpu_idle;
                    break;
                }
            }
        }
        
        fclose(fp);
        
        prev_cpu_total = user + nice + system + idle + iowait + irq + softirq + steal;
        prev_cpu_idle = idle + iowait;
    }
}

static void cpu_data_cleanup(void) {
    g_free(cpu_model); g_free(cpu_cache_info); g_free(cpu_architecture);
    g_free(cpu_stepping); g_free(cpu_family); g_free(cpu_vendor_id);
    g_free(cpu_bogomips); g_free(cpu_address_sizes);
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
    
    gulong total = user + nice + system + idle + iowait + irq + softirq + steal;
    gulong idle_total = idle + iowait;
    
    if (prev_cpu_total > 0 && total > prev_cpu_total) {
        gulong total_diff = total - prev_cpu_total;
        gulong idle_diff = idle_total - prev_cpu_idle;
        current_cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
        cpu_usage_history[cpu_usage_index] = current_cpu_usage > 0 ? current_cpu_usage : 0;
    }
    
    prev_cpu_total = total;
    prev_cpu_idle = idle_total;
    
    // Update per-CPU usage
    for (int i = 0; i < cpu_threads; i++) {
        char cpu_line[32];
        snprintf(cpu_line, sizeof(cpu_line), "cpu%d ", i);
        
        // Reset file position
        rewind(fp);
        
        // Find the specific CPU line
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, cpu_line, strlen(cpu_line)) == 0) {
                gulong cpu_user = 0, cpu_nice = 0, cpu_system = 0, cpu_idle = 0;
                sscanf(line, "%*s %lu %lu %lu %lu", &cpu_user, &cpu_nice, &cpu_system, &cpu_idle);
                
                gulong prev_total = prev_cpu_stats[i][0] + prev_cpu_stats[i][1] + 
                                   prev_cpu_stats[i][2] + prev_cpu_stats[i][3];
                gulong curr_total = cpu_user + cpu_nice + cpu_system + cpu_idle;
                
                if (curr_total > prev_total) {
                    gulong total_diff = curr_total - prev_total;
                    gulong idle_diff = cpu_idle - prev_cpu_stats[i][3];
                    current_per_cpu_usage[i] = 100.0 * (total_diff - idle_diff) / total_diff;
                    per_cpu_usage_history[i][cpu_usage_index] = current_per_cpu_usage[i] > 0 ? current_per_cpu_usage[i] : 0;
                }
                
                prev_cpu_stats[i][0] = cpu_user;
                prev_cpu_stats[i][1] = cpu_nice;
                prev_cpu_stats[i][2] = cpu_system;
                prev_cpu_stats[i][3] = cpu_idle;
                
                break;
            }
        }
    }
    
    // Increment the index after all CPUs are updated
    cpu_usage_index = (cpu_usage_index + 1) % MAX_POINTS;
    
    // Update CPU frequency
    get_cpu_freq();
    
    fclose(fp);
}

// Dialog for Active Logical Processors
static void show_processors_dialog(GtkWidget *parent, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Active Logical Processors",
                                                  GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Show Individual Graphs", GTK_RESPONSE_YES,
                                                  "Show Combined Graph", GTK_RESPONSE_NO,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    char message[100];
    snprintf(message, sizeof(message), "Number of logical processors: %d", cpu_threads);
    
    GtkWidget *label = gtk_label_new(message);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_YES) {
        show_per_cpu_graphs = TRUE;
        gtk_widget_queue_draw(data->drawing_area);
    } else if (result == GTK_RESPONSE_NO) {
        show_per_cpu_graphs = FALSE;
        gtk_widget_queue_draw(data->drawing_area);
    }
    
    gtk_widget_destroy(dialog);
}

// Dialog for Refresh Period
static void show_refresh_dialog(GtkWidget *parent, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    
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
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), data->update_interval);
    
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);
    
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_APPLY) {
        data->update_interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        g_print("Setting refresh interval to %d ms\n", data->update_interval);
    }
    
    gtk_widget_destroy(dialog);
}

static void draw_cpu_graph(cairo_t *cr, int width, int height, gdouble usage_history[], int index) {
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
        int idx = (index + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (usage_history[idx] / 100.0 * height);
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
        int idx = (index + i) % MAX_POINTS;
        double x = (double)i / (MAX_POINTS - 1) * width;
        double y = height - (usage_history[idx] / 100.0 * height);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width, height = allocation.height;
    
    // Background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);
    
    if (show_per_cpu_graphs) {
        int rows = (cpu_threads + 1) / 2;  // Ceiling division
        int cols = (cpu_threads > 1) ? 2 : 1;
        
        int graph_width = width / cols;
        int graph_height = height / rows;
        
        for (int i = 0; i < cpu_threads; i++) {
            int row = i / cols;
            int col = i % cols;
            
            // Save the current state
            cairo_save(cr);
            
            // Translate to the position of this CPU's graph
            cairo_translate(cr, col * graph_width, row * graph_height);
            
            // Draw the CPU graph
            draw_cpu_graph(cr, graph_width, graph_height, per_cpu_usage_history[i], cpu_usage_index);
            
            // Draw CPU number label
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 12);
            
            char cpu_label[16];
            snprintf(cpu_label, sizeof(cpu_label), "CPU %d: %.1f%%", i, current_per_cpu_usage[i]);
            
            cairo_text_extents_t extents;
            cairo_text_extents(cr, cpu_label, &extents);
            cairo_move_to(cr, 5, 15);
            cairo_show_text(cr, cpu_label);
            
            // Restore the saved state
            cairo_restore(cr);
        }
    } else {
        // Draw the overall CPU graph
        draw_cpu_graph(cr, width, height, cpu_usage_history, cpu_usage_index);
    }
    
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

static gboolean update_ui(gpointer user_data) {
    AppData *data = (AppData*)user_data;
    
    update_cpu_data();
    
    char usage_str[32];
    snprintf(usage_str, sizeof(usage_str), "CPU Usage: %.1f%%", current_cpu_usage);
    gtk_label_set_text(GTK_LABEL(data->usage_label), usage_str);
    
    char freq_str[32];
    if (cpu_freq_mhz > 1000) {
        snprintf(freq_str, sizeof(freq_str), "CPU Frequency: %.2f GHz", cpu_freq_mhz / 1000.0);
    } else {
        snprintf(freq_str, sizeof(freq_str), "CPU Frequency: %.0f MHz", cpu_freq_mhz);
    }
    gtk_label_set_text(GTK_LABEL(data->freq_label), freq_str);
    
    gtk_widget_queue_draw(data->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void on_processors_activate(GtkMenuItem *item, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    show_processors_dialog(data->drawing_area, data);
}

static void on_refresh_activate(GtkMenuItem *item, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    show_refresh_dialog(data->drawing_area, data);
    
    // Remove the old timeout and add a new one with the updated interval
    if (data->timeout_id > 0) {
        g_source_remove(data->timeout_id);
    }
    data->timeout_id = g_timeout_add(data->update_interval, update_ui, data);
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    
    // Remove the timeout
    if (data->timeout_id > 0) {
        g_source_remove(data->timeout_id);
    }
    
    // Free memory
    g_free(data);
    
    // Clean up CPU data
    cpu_data_cleanup();
    
    // Quit the application
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *vbox;
    GtkWidget *drawing_area;
    GtkWidget *info_box;
    GtkWidget *usage_label;
    GtkWidget *freq_label;
    GtkWidget *menu, *processors_item, *refresh_item;
    AppData *app_data;
    
    gtk_init(&argc, &argv);
    
    // Initialize CPU data
    cpu_data_init();
    
    // Create application data
    app_data = g_new(AppData, 1);
    app_data->update_interval = 1000; // Default: 1 second
    
    // Create the main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "CPU Monitor");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app_data);
    
    // Create the main layout
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    // Create the info box
    info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_start(GTK_BOX(vbox), info_box, FALSE, FALSE, 0);
    
    // Create the usage and frequency labels
    usage_label = gtk_label_new("CPU Usage: 0.0%");
    freq_label = gtk_label_new("CPU Frequency: N/A");
    gtk_box_pack_start(GTK_BOX(info_box), usage_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), freq_label, FALSE, FALSE, 0);
    
    // Create the drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, -1, 300);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(on_draw), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
    
    // Create right-click menu
    menu = gtk_menu_new();
    processors_item = gtk_menu_item_new_with_label("Active Logical Processors");
    refresh_item = gtk_menu_item_new_with_label("Refresh Period");
    g_signal_connect(processors_item, "activate", G_CALLBACK(on_processors_activate), app_data);
    g_signal_connect(refresh_item, "activate", G_CALLBACK(on_refresh_activate), app_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), processors_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);
    gtk_widget_show_all(menu);
    
    // Connect right-click event
    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press), menu);
    
    // Store widgets in app data
    app_data->drawing_area = drawing_area;
    app_data->usage_label = usage_label;
    app_data->freq_label = freq_label;
    
    // Set up timer for data updates
    app_data->timeout_id = g_timeout_add(app_data->update_interval, update_ui, app_data);
    
    gtk_widget_show_all(window);
    gtk_main();
    
    return 0;
} 
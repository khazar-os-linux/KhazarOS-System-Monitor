#include <gtk/gtk.h>
#include <glib.h>
#include "cpu/cpu_data.h"
#include "ui/ui_cpu.h"
#include "memory/memory_data.h"
#include "ui/ui_memory.h"
#include "disk/disk_data.h"
#include "ui/ui_disk.h"
#include "network/network_data.h"
#include "ui/ui_network.h"
#include "gpu/gpu_data.h"
#include "ui/ui_gpu.h"
#include "ui/ui_app.h"
#include "ui/ui_about.h"
#include "utils/icon_cache.h"
#include "utils/hotkey.h"
#include <signal.h>

static void on_shutdown(GtkApplication* app, gpointer user_data) {
    ui_app_cleanup();
    cpu_data_cleanup();
    memory_data_cleanup();
    disk_data_cleanup();
    network_data_cleanup();
    gpu_data_cleanup();
}

static void activate (GtkApplication* app, gpointer user_data) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "System Monitor");
    gtk_window_set_default_size(GTK_WINDOW(window), 850, 600);
    
    GtkWidget *main_notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), main_notebook);

    GtkWidget *performance_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(performance_notebook), GTK_POS_LEFT);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_cpu_tab(), gtk_label_new("CPU"));
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_memory_tab(), gtk_label_new("RAM"));
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_disk_tab(), gtk_label_new("Disks"));
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_network_tab(), gtk_label_new("Network"));
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_gpu_tab(), gtk_label_new("GPU"));
  
    gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), performance_notebook, gtk_label_new("Performance"));

    gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), create_apps_tab(), gtk_label_new("Apps"));
    gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), create_about_tab(), gtk_label_new("About"));

    gtk_widget_show_all(window);

    // Set application icon
    {
        GError *icon_error = NULL;
        if (!gtk_window_set_icon_from_file(GTK_WINDOW(window), "khos-sm-logo.svg", &icon_error)) {
            g_clear_error(&icon_error);
        }
    }

    ensure_hotkey_binding();
}

int main (int argc, char **argv) {
    cpu_data_init();
    memory_data_init();

    GtkApplication *app = gtk_application_new("org.gtk.systemmonitor", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    ui_app_cleanup();
    free_icon_cache();
    cpu_data_cleanup();
    memory_data_cleanup();
    disk_data_cleanup();
    network_data_cleanup();
    gpu_data_cleanup();

    return status;
}

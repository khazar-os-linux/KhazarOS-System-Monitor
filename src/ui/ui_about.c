#include "ui/ui_about.h"
#include <gtk/gtk.h>

/* Internal callback for GitHub button */
static void on_about_github_clicked(GtkButton *btn, gpointer user_data) {
    const char *url = "https://github.com/khazar-os-linux";
#if GTK_CHECK_VERSION(3,22,0)
    gtk_show_uri_on_window(NULL, url, GDK_CURRENT_TIME, NULL);
#else
    gtk_show_uri(NULL, url, GDK_CURRENT_TIME, NULL);
#endif
}

GtkWidget* create_about_tab(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);

    const char *info[] = {
        "KhazarOS System Monitor",
        "Version: 0.1.0 beta",
        "License: GPL 3.0",
    };

    for (guint i = 0; i < G_N_ELEMENTS(info); ++i) {
        GtkWidget *lbl = gtk_label_new(info[i]);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    }

    /* GitHub button */
    GtkWidget *gh_btn = gtk_button_new_with_label("Open GitHub page");
    g_signal_connect(gh_btn, "clicked", G_CALLBACK(on_about_github_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), gh_btn, FALSE, FALSE, 0);

    return box;
} 
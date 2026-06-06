#ifndef UI_APP_H
#define UI_APP_H

#include <gtk/gtk.h>

GtkWidget* create_apps_tab(void);
void ui_app_cleanup(void);
void free_icon_cache(void); /* exposed for shutdown */

#endif

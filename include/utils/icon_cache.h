#ifndef ICON_CACHE_H
#define ICON_CACHE_H

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

GdkPixbuf* get_icon_for_app(const char *app_name);
void free_icon_cache(void);

#endif
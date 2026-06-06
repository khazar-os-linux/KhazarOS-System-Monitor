#include "utils/icon_cache.h"
#include <string.h>

static GHashTable *icon_cache = NULL;

GdkPixbuf* get_icon_for_app(const char *app_name) {
    if (!icon_cache) {
        icon_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }
    GdkPixbuf *pix = g_hash_table_lookup(icon_cache, app_name);
    if (pix) return pix;

    GtkIconTheme *theme = gtk_icon_theme_get_default();
    GError *error = NULL;
    pix = gtk_icon_theme_load_icon(theme, app_name, 24, 0, &error);
    if (!pix) {
        if (error) g_error_free(error);
        pix = gtk_icon_theme_load_icon(theme, "application-x-executable", 24, 0, NULL);
    }
    if (pix) {
        g_hash_table_insert(icon_cache, g_strdup(app_name), g_object_ref(pix));
    }
    return pix;
}

void free_icon_cache(void) {
    if (icon_cache) {
        g_hash_table_destroy(icon_cache);
        icon_cache = NULL;
    }
}
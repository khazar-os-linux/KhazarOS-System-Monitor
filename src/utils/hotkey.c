#include "utils/hotkey.h"
#include <gio/gio.h>
#include <string.h>

void ensure_hotkey_binding(void) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        g_printerr("GSettings schema source not available; global shortcut not installed.\n");
        return;
    }

    GSettingsSchema *schema = g_settings_schema_source_lookup(
        source, "org.gnome.settings-daemon.plugins.media-keys", TRUE);
    if (!schema) {
        g_printerr("GNOME media-keys schema not found; global shortcut not installed.\n");
        return;
    }
    g_settings_schema_unref(schema);

    const gchar *binding_path = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/khos-system-monitor/";

    GSettings *media_keys = g_settings_new("org.gnome.settings-daemon.plugins.media-keys");
    if (!media_keys) {
        g_printerr("Failed to open media-keys GSettings schema; global shortcut not installed.\n");
        return;
    }

    gchar **current = g_settings_get_strv(media_keys, "custom-keybindings");
    gboolean found = FALSE;
    for (gchar **p = current; p && *p; ++p) {
        if (g_strcmp0(*p, binding_path) == 0) {
            found = TRUE;
            break;
        }
    }

    if (!found) {
        GPtrArray *array = g_ptr_array_new();
        for (gchar **p = current; p && *p; ++p) {
            g_ptr_array_add(array, g_strdup(*p));
        }
        g_ptr_array_add(array, g_strdup(binding_path));
        g_ptr_array_add(array, NULL);
        g_settings_set_strv(media_keys, "custom-keybindings", (const gchar * const *)array->pdata);
        for (guint i = 0; i < array->len; ++i) {
            g_free(g_ptr_array_index(array, i));
        }
        g_ptr_array_free(array, TRUE);
    }

    g_strfreev(current);

    GSettings *binding = g_settings_new_with_path(
        "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding",
        binding_path);

    if (!binding) {
        g_printerr("Failed to create binding GSettings object.\n");
        g_object_unref(media_keys);
        return;
    }

    gchar *exe_path = g_file_read_link("/proc/self/exe", NULL);
    const gchar *command = exe_path ? exe_path : "system-monitor";

    g_settings_set_string(binding, "name", "KHOS System Monitor");
    g_settings_set_string(binding, "command", command);
    g_settings_set_string(binding, "binding", "<Primary><Shift>Escape");

    if (exe_path)
        g_free(exe_path);

    g_object_unref(binding);
    g_object_unref(media_keys);
}
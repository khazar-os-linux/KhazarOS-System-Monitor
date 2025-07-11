#include <gtk/gtk.h>
#include <glib.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
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
#include "ui/ui_about.h"
#include <signal.h>
#include <gio/gio.h>

// Struct to store previous CPU times for a process
typedef struct {
    gulong prev_utime;
    gulong prev_stime;
    gboolean seen_this_cycle;
} ProcessCpuData;

// Global GHashTable to store ProcessCpuData for each PID
static GHashTable *process_cpu_times_hash = NULL;
static gulong prev_total_system_jiffies = 0;

// Define columns for the Apps tab tree view (used by GtkTreeStore)
enum {
  COLUMN_APP_ICON,
  COLUMN_APP_NAME,
  COLUMN_APP_PID,
  COLUMN_APP_CPU_STR,
  COLUMN_APP_MEM_STR,
  N_APP_COLUMNS
};

// Services columns
enum {
  COLUMN_SVC_NAME,
  COLUMN_SVC_DESC,
  COLUMN_SVC_ACTIVE,
  N_SVC_COLUMNS
};

// Global search filter (lower-case)
static char apps_search_filter[128] = "";

// Icon cache
static GHashTable *icon_cache = NULL;

// Forward declaration for apps list update function
static gboolean update_apps_list(gpointer user_data);
// static GtkWidget* create_startup_tab(void); /* disabled */
/* create_about_tab declared in ui_about.h */
static void ensure_hotkey_binding(void);

static void pid_cell_data_func (GtkTreeViewColumn *tree_column,
                                GtkCellRenderer   *cell,
                                GtkTreeModel      *tree_model,
                                GtkTreeIter       *iter,
                                gpointer           data) {
    guint pid_val;
    gtk_tree_model_get (tree_model, iter, COLUMN_APP_PID, &pid_val, -1);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%u", pid_val);
    g_object_set (cell, "text", pid_str, NULL);
}

static gulong get_total_system_jiffies() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("fopen /proc/stat failed");
        return 0;
    }
    char line[256];
    gulong user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    user = nice = system = idle = iowait = irq = softirq = steal = guest = guest_nice = 0;
    if (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            sscanf(line, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
        }
    }
    fclose(fp);
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

// Helper functions for iterating through the process hash table
static void mark_process_unseen(gpointer key, gpointer value, gpointer user_data) {
    ((ProcessCpuData*)value)->seen_this_cycle = FALSE;
}

static void collect_unseen_processes(gpointer key, gpointer value, gpointer user_data) {
    if (!((ProcessCpuData*)value)->seen_this_cycle) {
        GList **keys_to_remove = (GList**)user_data;
        *keys_to_remove = g_list_prepend(*keys_to_remove, key);
    }
}

/* ----------------------------------------------------------------------------------
 *  Context-menu helpers for the "Apps" tab
 * --------------------------------------------------------------------------------*/

typedef struct {
    guint interval_seconds;
    guint timeout_id;
    GtkTreeView *tree_view;
} AppsUpdateData;

static gchar get_process_state(pid_t pid) {
    char stat_path[64];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    FILE *fp = fopen(stat_path, "r");
    if (!fp) return 0;
    // pid (comm) state ... -> third field is state char
    int dummy; char comm[256]; char state = 0;
    if (fscanf(fp, "%d %255s %c", &dummy, comm, &state) != 3) state = 0;
    fclose(fp);
    return state;
}

// Recursively collect PIDs from selected iter (handles parent row)
static void collect_pids(GtkTreeModel *model, GtkTreeIter *iter, GArray *pids) {
    guint pid_val = 0;
    gtk_tree_model_get(model, iter, COLUMN_APP_PID, &pid_val, -1);
    if (pid_val > 0) {
        g_array_append_val(pids, pid_val);
    } else {
        GtkTreeIter child;
        if (gtk_tree_model_iter_children(model, &child, iter)) {
            do {
                collect_pids(model, &child, pids);
            } while (gtk_tree_model_iter_next(model, &child));
        }
    }
}

static GArray* get_selected_pids(GtkTreeView *tree_view) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return NULL;
    GArray *pids = g_array_new(FALSE, FALSE, sizeof(guint));
    collect_pids(model, &iter, pids);
    return pids;
}

#if 0
static void on_stop_activate(GtkMenuItem *item, gpointer user_data) {
    /* function disabled */
}
#endif

static gboolean confirm_action(GtkWindow *parent, const gchar *message) {
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_YES_NO,
                                              "%s", message);
    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return res == GTK_RESPONSE_YES;
}

static void on_kill_activate(GtkMenuItem *item, gpointer user_data) {
    GtkTreeView *tree_view = GTK_TREE_VIEW(user_data);
    GtkWidget *parent_window = gtk_widget_get_toplevel(GTK_WIDGET(tree_view));
    if (!confirm_action(GTK_WINDOW(parent_window), "Are you sure you want to kill the selected process(es)?"))
        return;

    GArray *pids = get_selected_pids(tree_view);
    if (!pids) return;

    for (guint i = 0; i < pids->len; ++i) {
        guint pid = g_array_index(pids, guint, i);
        if (kill((pid_t)pid, SIGTERM) != 0) {
            g_printerr("Failed to SIGTERM pid %u: %s\n", pid, g_strerror(errno));
        }
    }

    g_usleep(200000); // 200ms

    for (guint i = 0; i < pids->len; ++i) {
        guint pid = g_array_index(pids, guint, i);
        if (kill((pid_t)pid, 0) == 0) { // still exists
            if (kill((pid_t)pid, SIGKILL) != 0) {
                g_printerr("Failed to SIGKILL pid %u: %s\n", pid, g_strerror(errno));
            }
        }
    }
    g_array_free(pids, TRUE);
}

static GdkPixbuf* get_icon_for_app(const char *app_name) {
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

static gboolean on_apps_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right-click
        GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
        GtkTreePath *path = NULL;
        if (gtk_tree_view_get_path_at_pos(tree_view, (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL)) {
            GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
            gtk_tree_selection_unselect_all(selection);
            gtk_tree_selection_select_path(selection, path);
            gtk_menu_popup_at_pointer(GTK_MENU(user_data), (GdkEvent*)event);
            gtk_tree_path_free(path);
            return TRUE; // Event handled
        }
    }
    return FALSE;
}

/* ----------------------------------------------------------------------------------
 *  Sorting helpers – keep the Apps list order stable (alphabetical by name, PID)
 * --------------------------------------------------------------------------------*/

static gint sort_by_app_name(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    gchar *name_a = NULL;
    gchar *name_b = NULL;
    gtk_tree_model_get(model, a, COLUMN_APP_NAME, &name_a, -1);
    gtk_tree_model_get(model, b, COLUMN_APP_NAME, &name_b, -1);
    gint result = g_strcmp0(name_a, name_b);
    g_free(name_a);
    g_free(name_b);
    return result;
}

static gint sort_by_pid(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    guint pid_a = 0, pid_b = 0;
    gtk_tree_model_get(model, a, COLUMN_APP_PID, &pid_a, -1);
    gtk_tree_model_get(model, b, COLUMN_APP_PID, &pid_b, -1);
    if (pid_a < pid_b) return -1;
    if (pid_a > pid_b) return 1;
    return 0;
}

// Helper to get process memory in KB
static gulong get_process_memory_kb(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path,"r");
    if (!fp) return 0;
    char line[256];
    gulong kb=0;
    while (fgets(line,sizeof(line),fp)) {
        if (strncmp(line,"VmRSS:",6)==0) {
            sscanf(line+6, "%lu", &kb);
            break;
        }
    }
    fclose(fp);
    return kb;
}

static gint sort_by_cpu_str(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    gchar *str_a=NULL,*str_b=NULL;
    gtk_tree_model_get(model,a,COLUMN_APP_CPU_STR,&str_a,-1);
    gtk_tree_model_get(model,b,COLUMN_APP_CPU_STR,&str_b,-1);
    gdouble va = g_ascii_strtod(str_a,NULL);
    gdouble vb = g_ascii_strtod(str_b,NULL);
    g_free(str_a); g_free(str_b);
    return (va<vb)?-1:(va>vb);
}

static gint sort_by_mem_str(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    gchar *str_a=NULL,*str_b=NULL;
    gtk_tree_model_get(model,a,COLUMN_APP_MEM_STR,&str_a,-1);
    gtk_tree_model_get(model,b,COLUMN_APP_MEM_STR,&str_b,-1);
    gdouble va = g_ascii_strtod(str_a,NULL);
    gdouble vb = g_ascii_strtod(str_b,NULL);
    g_free(str_a); g_free(str_b);
    return (va<vb)?-1:(va>vb);
}

/* ----------------------------------------------------------------------------------
 *  Update the GtkTreeStore for the "Apps" tab
 * --------------------------------------------------------------------------------*/

/* Helper to restore vertical scroll position after TreeView internal callbacks */
typedef struct {
    GtkAdjustment *adj;
    gdouble value;
} ScrollRestoreData;

static gboolean restore_scroll_idle(gpointer user_data) {
    ScrollRestoreData *d = (ScrollRestoreData*)user_data;
    /* Clamp value to adjustment bounds */
    gdouble v = CLAMP(d->value, gtk_adjustment_get_lower(d->adj), gtk_adjustment_get_upper(d->adj) - gtk_adjustment_get_page_size(d->adj));
    gtk_adjustment_set_value(d->adj, v);
    g_object_unref(d->adj);
    g_free(d);
    return FALSE; // run once
}

static gboolean update_apps_list(gpointer user_data) {
    GtkTreeView *tree_view = GTK_TREE_VIEW(user_data);
    /* Preserve exact vertical scroll position */
    GtkAdjustment *vadj_preserve = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tree_view));
    gdouble vadj_value = gtk_adjustment_get_value(vadj_preserve);
    GtkTreeStore *tree_store = GTK_TREE_STORE(gtk_tree_view_get_model(tree_view));

    gulong current_total_system_jiffies = get_total_system_jiffies();
    gulong system_jiffies_delta = (prev_total_system_jiffies > 0 && current_total_system_jiffies > prev_total_system_jiffies) 
                                  ? current_total_system_jiffies - prev_total_system_jiffies : 0;

    if (process_cpu_times_hash) {
        g_hash_table_foreach(process_cpu_times_hash, mark_process_unseen, NULL);
    }

    /* ---------------------------------------------------------------------
     *  Preserve expand/collapse state + selection of rows
     * -------------------------------------------------------------------*/
    GHashTable *expanded_apps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    GtkTreeModel *old_model = GTK_TREE_MODEL(tree_store);

    /* Capture expand state */
    GtkTreeIter top_iter;
    if (gtk_tree_model_get_iter_first(old_model, &top_iter)) {
        do {
            GtkTreePath *path_exp = gtk_tree_model_get_path(old_model, &top_iter);
            if (gtk_tree_view_row_expanded(tree_view, path_exp)) {
                gchar *app_tmp = NULL;
                gtk_tree_model_get(old_model, &top_iter, COLUMN_APP_NAME, &app_tmp, -1);
                if (app_tmp) {
                    g_hash_table_add(expanded_apps, g_strdup(app_tmp));
                }
                g_free(app_tmp);
            }
            gtk_tree_path_free(path_exp);
        } while (gtk_tree_model_iter_next(old_model, &top_iter));
    }

    /* Capture current selection */
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeIter sel_iter;
    gboolean had_selection = gtk_tree_selection_get_selected(selection, NULL, &sel_iter);
    guint saved_pid = 0;
    gchar *saved_app = NULL;
    gboolean saved_is_parent = FALSE;
    if (had_selection) {
        gtk_tree_model_get(GTK_TREE_MODEL(tree_store), &sel_iter,
                           COLUMN_APP_PID, &saved_pid,
                           COLUMN_APP_NAME, &saved_app,
                           -1);
        if (saved_pid == 0 && saved_app) {
            saved_is_parent = TRUE;
        }
    }

    /* Capture first visible row path before model reset */
    GtkTreePath *first_visible_path = NULL;
    gtk_tree_view_get_path_at_pos(tree_view, 0, 0, &first_visible_path, NULL, NULL, NULL);

    gtk_tree_store_clear(tree_store);

    // Hash tables to keep track of parent rows and child rows by PID
    GHashTable *parent_iter_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *pid_iter_hash = g_hash_table_new(NULL, NULL); // direct hash, no free for key/value (value freed later)

    // Aggregation hash: app name -> struct {GtkTreeIter*, double cpu, gulong mem_kb}
    typedef struct { GtkTreeIter *iter; double cpu; gulong mem; } AggData;
    GHashTable *agg_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        perror("opendir /proc failed");
        prev_total_system_jiffies = current_total_system_jiffies;
        return TRUE;
    }

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            pid_t pid = atoi(entry->d_name);
            char stat_path[512], comm_path[512], proc_name[256] = {0}, cpu_percent_str[16] = "0.0%", mem_str[16] = "0.0 MB";
            gulong utime = 0, stime = 0, kb = 0;

            snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
            FILE *fp_comm = fopen(comm_path, "r");
            if (fp_comm) {
                if(fgets(proc_name, sizeof(proc_name), fp_comm)) {
                    proc_name[strcspn(proc_name, "\n")] = 0;
                }
                fclose(fp_comm);
            }
            
            snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", entry->d_name);
            FILE *fp_stat = fopen(stat_path, "r");
            if (fp_stat) {
                 char stat_buffer[1024];
                 if (fgets(stat_buffer, sizeof(stat_buffer), fp_stat)) {
                    char *p_open = strchr(stat_buffer, '(');
                    char *p_close = strrchr(stat_buffer, ')');
                    if (p_open && p_close) {
                        *p_close = '\0';
                        sscanf(p_close + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime);
                    }
                }
                fclose(fp_stat);
            }

            kb = get_process_memory_kb(pid);
            snprintf(mem_str, sizeof(mem_str), "%.1f", kb/1024.0);

            if (process_cpu_times_hash && system_jiffies_delta > 0) {
                ProcessCpuData *data = g_hash_table_lookup(process_cpu_times_hash, GINT_TO_POINTER(pid));
                gulong current_process_total = utime + stime;
                if (data) {
                    gulong process_jiffies_delta = current_process_total - (data->prev_utime + data->prev_stime);
                    double cpu_percent = 100.0 * process_jiffies_delta / system_jiffies_delta;
                    snprintf(cpu_percent_str, sizeof(cpu_percent_str), "%.1f%%", cpu_percent > 0.0 ? cpu_percent : 0.0);
                    data->prev_utime = utime;
                    data->prev_stime = stime;
                    data->seen_this_cycle = TRUE;
                } else {
                    data = g_new(ProcessCpuData, 1);
                    data->prev_utime = utime;
                    data->prev_stime = stime;
                    data->seen_this_cycle = TRUE;
                    g_hash_table_insert(process_cpu_times_hash, GINT_TO_POINTER(pid), data);
                }
            } else if (process_cpu_times_hash) {
                 ProcessCpuData *data = g_hash_table_lookup(process_cpu_times_hash, GINT_TO_POINTER(pid));
                 if (!data) {
                    data = g_new(ProcessCpuData, 1);
                    data->prev_utime = utime;
                    data->prev_stime = stime;
                    data->seen_this_cycle = TRUE;
                    g_hash_table_insert(process_cpu_times_hash, GINT_TO_POINTER(pid), data);
                 } else {
                    data->prev_utime = utime;
                    data->prev_stime = stime;
                    data->seen_this_cycle = TRUE;
                 }
            }
            
            // Skip system services (in system.slice) for Apps tab
            char cgroup_path[64];
            snprintf(cgroup_path, sizeof(cgroup_path), "/proc/%d/cgroup", pid);
            FILE *fp_cg = fopen(cgroup_path, "r");
            gboolean is_service = FALSE;
            if (fp_cg) {
                char cg_line[256];
                while (fgets(cg_line, sizeof(cg_line), fp_cg)) {
                    if (strstr(cg_line, "system.slice")) { is_service = TRUE; break; }
                }
                fclose(fp_cg);
            }
            if (is_service) continue;

            // Apply search filter: show only matching app names
            gchar *proc_lower = g_ascii_strdown(proc_name, -1);
            if (apps_search_filter[0] != '\0' && !g_strrstr(proc_lower, apps_search_filter)) {
                g_free(proc_lower);
                continue;
            }
            g_free(proc_lower);

            // Get or create parent row for this application name
            GtkTreeIter *parent_iter_ptr = g_hash_table_lookup(parent_iter_hash, proc_name);
            if (!parent_iter_ptr) {
                parent_iter_ptr = g_new(GtkTreeIter, 1);
                gtk_tree_store_append(tree_store, parent_iter_ptr, NULL); // Top-level row
                gtk_tree_store_set(tree_store, parent_iter_ptr,
                                   COLUMN_APP_ICON, get_icon_for_app(proc_name),
                                   COLUMN_APP_NAME, proc_name,
                                   COLUMN_APP_PID, (guint)0, // No PID for parent row
                                   COLUMN_APP_CPU_STR, "",
                                   COLUMN_APP_MEM_STR, "",
                                   -1);
                g_hash_table_insert(parent_iter_hash, g_strdup(proc_name), parent_iter_ptr);

                // create agg entry
                AggData *ad = g_new0(AggData,1);
                ad->iter = parent_iter_ptr;
                g_hash_table_insert(agg_hash, g_strdup(proc_name), ad);
            }

            // update aggregator
            AggData *ad = g_hash_table_lookup(agg_hash, proc_name);
            if (ad) {
                ad->cpu += atof(cpu_percent_str); // using previously computed string without %
                ad->mem += kb;
            }

            // Add child row for actual process
            GtkTreeIter child_iter;
            gtk_tree_store_append(tree_store, &child_iter, parent_iter_ptr);
            gtk_tree_store_set(tree_store, &child_iter,
                               COLUMN_APP_ICON, get_icon_for_app(proc_name),
                               COLUMN_APP_NAME, proc_name,
                               COLUMN_APP_PID, (guint)pid,
                               COLUMN_APP_CPU_STR, cpu_percent_str,
                               COLUMN_APP_MEM_STR, mem_str,
                               -1);

            // store child iter by pid
            GtkTreeIter *child_copy = g_new(GtkTreeIter,1);
            *child_copy = child_iter;
            g_hash_table_insert(pid_iter_hash, GINT_TO_POINTER(pid), child_copy);
        }
    }
    closedir(proc_dir);

    if (process_cpu_times_hash) {
        GList *keys_to_remove = NULL;
        g_hash_table_foreach(process_cpu_times_hash, collect_unseen_processes, &keys_to_remove);
        
        for (GList *l = keys_to_remove; l != NULL; l = l->next) {
            g_hash_table_remove(process_cpu_times_hash, l->data);
        }
        g_list_free(keys_to_remove);
    }

    /* Re-expand previously expanded top-level rows */
    GtkTreeModel *new_model = GTK_TREE_MODEL(tree_store);
    GtkTreeIter parent_iter_re;
    if (gtk_tree_model_get_iter_first(new_model, &parent_iter_re)) {
        do {
            gchar *app_name = NULL;
            gtk_tree_model_get(new_model, &parent_iter_re, COLUMN_APP_NAME, &app_name, -1);
            if (app_name && g_hash_table_contains(expanded_apps, app_name)) {
                GtkTreePath *path = gtk_tree_model_get_path(new_model, &parent_iter_re);
                gtk_tree_view_expand_row(tree_view, path, FALSE);
                gtk_tree_path_free(path);
            }
            g_free(app_name);
        } while (gtk_tree_model_iter_next(new_model, &parent_iter_re));
    }

    g_hash_table_destroy(expanded_apps);

    /* After processing all processes, set aggregated values on parent rows */
    GHashTableIter itagg; gpointer kagg, vagg;
    g_hash_table_iter_init(&itagg, agg_hash);
    while (g_hash_table_iter_next(&itagg, &kagg, &vagg)) {
        AggData *ad = vagg;
        char cpu_str[16]; snprintf(cpu_str,sizeof(cpu_str),"%.1f", ad->cpu);
        char mem_str[16]; snprintf(mem_str,sizeof(mem_str),"%.1f", ad->mem/1024.0);
        gtk_tree_store_set(tree_store, ad->iter,
                           COLUMN_APP_CPU_STR, cpu_str,
                           COLUMN_APP_MEM_STR, mem_str,
                           -1);
    }

    /* Restore previous selection if possible */
    if (saved_is_parent && saved_app) {
        GtkTreeIter *sel_parent_iter = g_hash_table_lookup(parent_iter_hash, saved_app);
        if (sel_parent_iter) {
            GtkTreePath *sp = gtk_tree_model_get_path(new_model, sel_parent_iter);
            /* Re-select previously selected row BUT do NOT force a scroll –
               this lets the user keep their manual scroll position. */
            gtk_tree_selection_select_path(selection, sp);
            gtk_tree_path_free(sp);
        }
    } else if (!saved_is_parent && saved_pid > 0) {
        GtkTreeIter *sel_child_iter = g_hash_table_lookup(pid_iter_hash, GINT_TO_POINTER(saved_pid));
        if (sel_child_iter) {
            GtkTreePath *sp = gtk_tree_model_get_path(new_model, sel_child_iter);
            gtk_tree_selection_select_path(selection, sp);
            gtk_tree_path_free(sp);
        }
    }

    if (saved_app) g_free(saved_app);

    // Clean-up pid_iter_hash
    GHashTableIter it_pid;
    gpointer key_pid, val_pid;
    g_hash_table_iter_init(&it_pid, pid_iter_hash);
    while (g_hash_table_iter_next(&it_pid, &key_pid, &val_pid)) {
        g_free(val_pid);
    }
    g_hash_table_destroy(pid_iter_hash);

    // free agg data entries already freed via value free
    g_hash_table_destroy(agg_hash);
    g_hash_table_destroy(parent_iter_hash);

    /* Defer restoring scroll position until after GTK finishes any internal scroll-to-cursor
       actions triggered by selection changes. */
    ScrollRestoreData *sd = g_new(ScrollRestoreData,1);
    sd->adj = g_object_ref(vadj_preserve);
    sd->value = vadj_value;
    g_idle_add(restore_scroll_idle, sd);

    /* Restore scroll position only if we did not have (or could not restore) a selection.
       Always freeing the saved path afterwards. */
    if (!had_selection && first_visible_path) {
        gtk_tree_view_scroll_to_cell(tree_view, first_visible_path, NULL, FALSE, 0, 0);
    }
    if (first_visible_path) {
        gtk_tree_path_free(first_visible_path);
    }

    prev_total_system_jiffies = current_total_system_jiffies;

    return TRUE;
}

/* Search entry changed */
static void on_apps_search_changed(GtkEntry *entry, gpointer user_data) {
    const gchar *txt = gtk_entry_get_text(entry);
    gchar *lower = g_ascii_strdown(txt, -1);
    strncpy(apps_search_filter, lower, sizeof(apps_search_filter)-1);
    apps_search_filter[sizeof(apps_search_filter)-1] = '\0';
    g_free(lower);

    update_apps_list(user_data);
}

/* --------------------------- Start New Task ---------------------------*/
static void on_start_task_clicked(GtkButton *btn, gpointer user_data) {
    GtkTreeView *tree_view = GTK_TREE_VIEW(user_data);
    GtkWidget *parent_window = gtk_widget_get_toplevel(GTK_WIDGET(tree_view));

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Start New Task",
                                                   GTK_WINDOW(parent_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "Run", GTK_RESPONSE_OK,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Command to execute ...");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const gchar *cmd = gtk_entry_get_text(GTK_ENTRY(entry));
        if (cmd && *cmd) {
            GError *err = NULL;
            if (!g_spawn_command_line_async(cmd, &err)) {
                GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(parent_window),
                                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                               GTK_MESSAGE_ERROR,
                                                               GTK_BUTTONS_CLOSE,
                                                               "Failed to launch: %s", err->message);
                gtk_dialog_run(GTK_DIALOG(err_dialog));
                gtk_widget_destroy(err_dialog);
                g_error_free(err);
            } else {
                // refresh list after short delay to show new task
                g_timeout_add_seconds(1, (GSourceFunc)update_apps_list, tree_view);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

static void activate (GtkApplication* app, gpointer user_data) {
    g_print("activate function called\n");
    GtkWidget *window;
    GtkWidget *main_notebook;
    GtkWidget *performance_notebook;
    GtkWidget *apps_scrolled_window, *apps_tree_view;
    
    if (!process_cpu_times_hash) {
        g_print("Creating process_cpu_times_hash\n");
        process_cpu_times_hash = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    }
    if (prev_total_system_jiffies == 0) {
        g_print("Initializing prev_total_system_jiffies\n");
        prev_total_system_jiffies = get_total_system_jiffies();
    }

    g_print("Creating window\n");
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "System Monitor");
    gtk_window_set_default_size(GTK_WINDOW(window), 850, 600);
    
    g_print("Creating main notebook\n");
    main_notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), main_notebook);

    g_print("Creating performance notebook\n");
    performance_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(performance_notebook), GTK_POS_LEFT);
    
    g_print("Creating CPU tab\n");
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_cpu_tab(), gtk_label_new("CPU"));
    g_print("Creating memory tab\n");
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_memory_tab(), gtk_label_new("RAM"));
    g_print("Creating disk tab\n");
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_disk_tab(), gtk_label_new("Disks"));
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_network_tab(), gtk_label_new("Network"));
    gtk_notebook_append_page(GTK_NOTEBOOK(performance_notebook), create_gpu_tab(), gtk_label_new("GPU"));
  
    gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), performance_notebook, gtk_label_new("Performance"));

    g_print("Creating apps tab\n");
    apps_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(apps_scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkTreeStore *apps_tree_store = gtk_tree_store_new(N_APP_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
    apps_tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(apps_tree_store));
    g_object_unref(apps_tree_store);

    GtkCellRenderer *pix_renderer = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
    g_object_set(text_renderer, "size-points", 11.0, NULL);

    GtkTreeViewColumn *name_col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(name_col, "App");
    gtk_tree_view_column_pack_start(name_col, pix_renderer, FALSE);
    gtk_tree_view_column_pack_start(name_col, text_renderer, TRUE);
    gtk_tree_view_column_add_attribute(name_col, pix_renderer, "pixbuf", COLUMN_APP_ICON);
    gtk_tree_view_column_add_attribute(name_col, text_renderer, "text", COLUMN_APP_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(apps_tree_view), name_col);
    gtk_tree_view_column_set_sort_column_id(name_col, COLUMN_APP_NAME);
  
    GtkTreeViewColumn *pid_column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(pid_column, "PID");
    gtk_tree_view_column_pack_start(pid_column, text_renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(pid_column, text_renderer, pid_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_sort_column_id(pid_column, COLUMN_APP_PID);
    gtk_tree_view_append_column(GTK_TREE_VIEW(apps_tree_view), pid_column);
  
    GtkTreeViewColumn *cpu_col = gtk_tree_view_column_new_with_attributes("CPU %", text_renderer, "text", COLUMN_APP_CPU_STR, NULL);
    gtk_tree_view_column_set_sort_column_id(cpu_col, COLUMN_APP_CPU_STR);
    gtk_tree_view_append_column(GTK_TREE_VIEW(apps_tree_view), cpu_col);

    GtkTreeViewColumn *mem_col = gtk_tree_view_column_new_with_attributes("Mem MB", text_renderer, "text", COLUMN_APP_MEM_STR, NULL);
    gtk_tree_view_column_set_sort_column_id(mem_col, COLUMN_APP_MEM_STR);
    gtk_tree_view_append_column(GTK_TREE_VIEW(apps_tree_view), mem_col);

    // Create the context menu for right-click actions
    GtkWidget *apps_menu = gtk_menu_new();
    GtkWidget *kill_item = gtk_menu_item_new_with_label("Kill");

    g_signal_connect(kill_item, "activate", G_CALLBACK(on_kill_activate), apps_tree_view);

    gtk_menu_shell_append(GTK_MENU_SHELL(apps_menu), kill_item);
    gtk_widget_show_all(apps_menu);

    // Connect the button-press event for right-click menu
    gtk_widget_add_events(apps_tree_view, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(apps_tree_view, "button-press-event", G_CALLBACK(on_apps_tree_button_press), apps_menu);

    /* Layout: search entry + tree view */
    GtkWidget *apps_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *start_btn = gtk_button_new_with_label("Start new task");
    GtkWidget *search_entry = gtk_search_entry_new();
    gtk_box_pack_start(GTK_BOX(toolbar), start_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), search_entry, TRUE, TRUE, 0);

    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_apps_search_changed), apps_tree_view);
    g_signal_connect(start_btn, "clicked", G_CALLBACK(on_start_task_clicked), apps_tree_view);

    gtk_box_pack_start(GTK_BOX(apps_vbox), toolbar, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(apps_scrolled_window), apps_tree_view);
    gtk_box_pack_start(GTK_BOX(apps_vbox), apps_scrolled_window, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), apps_vbox, gtk_label_new("Apps"));

    /* About tab */
    gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), create_about_tab(), gtk_label_new("About"));
  
    g_print("Updating apps list\n");

    // Create update-data object and store
    AppsUpdateData *apps_upd = g_new0(AppsUpdateData, 1);
    apps_upd->interval_seconds = 2;
    apps_upd->tree_view = GTK_TREE_VIEW(apps_tree_view);
    apps_upd->timeout_id = g_timeout_add_seconds(apps_upd->interval_seconds, (GSourceFunc)update_apps_list, apps_tree_view);
    g_object_set_data(G_OBJECT(apps_tree_view), "apps_update_data", apps_upd);

    update_apps_list(apps_tree_view);

    g_print("Showing window\n");
    gtk_widget_show_all(window);
    g_print("activate function complete\n");

    /* Register sort functions to keep order stable */
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(apps_tree_store);
    gtk_tree_sortable_set_sort_func(sortable, COLUMN_APP_NAME, sort_by_app_name, NULL, NULL);
    gtk_tree_sortable_set_sort_func(sortable, COLUMN_APP_PID, sort_by_pid, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, COLUMN_APP_NAME, GTK_SORT_ASCENDING);
    gtk_tree_sortable_set_sort_func(sortable, COLUMN_APP_CPU_STR, sort_by_cpu_str, NULL, NULL);
    gtk_tree_sortable_set_sort_func(sortable, COLUMN_APP_MEM_STR, sort_by_mem_str, NULL, NULL);

    // Ensure global hotkey exists (GNOME environments)
    ensure_hotkey_binding();
}

static void on_shutdown(GtkApplication* app, gpointer user_data) {
    g_print("on_shutdown called\n");
    
    // Clean up the process hash table
    if (process_cpu_times_hash) {
        g_print("Destroying process_cpu_times_hash\n");
        g_hash_table_destroy(process_cpu_times_hash);
        process_cpu_times_hash = NULL;
        g_print("process_cpu_times_hash destroyed\n");
    } else {
        g_print("process_cpu_times_hash is NULL\n");
    }
    
    // Call cpu_data_cleanup to free memory
    g_print("Calling cpu_data_cleanup\n");
    cpu_data_cleanup();
    g_print("cpu_data_cleanup finished\n");
    
    // Call memory_data_cleanup to free memory
    g_print("Calling memory_data_cleanup\n");
    memory_data_cleanup();
    g_print("memory_data_cleanup finished\n");
    
    // Call disk_data_cleanup to free memory
    g_print("Calling disk_data_cleanup\n");
    disk_data_cleanup();
    g_print("disk_data_cleanup finished\n");
    
    // Call network_data_cleanup to free memory
    g_print("Calling network_data_cleanup\n");
    network_data_cleanup();
    g_print("network_data_cleanup finished\n");
    
    // Call gpu_data_cleanup to free memory
    g_print("Calling gpu_data_cleanup\n");
    gpu_data_cleanup();
    g_print("gpu_data_cleanup finished\n");
    
    g_print("on_shutdown finished\n");
}

/* Services tab removed */
#if 0
/* previous services code disabled */
#endif

/* =====================================================================
 * Startup Applications tab
 * ===================================================================*/

#if 0
static void on_startup_browse_clicked(GtkButton *btn, gpointer user_data) {
    GtkEntry *entry = GTK_ENTRY(user_data);
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Select Application or Script",
                                               NULL,
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               "Cancel", GTK_RESPONSE_CANCEL,
                                               "Open", GTK_RESPONSE_ACCEPT,
                                               NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(entry, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dlg);
}

static void on_add_startup_ok(GtkDialog *dlg, gint response_id, gpointer user_data) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkEntry *entry = GTK_ENTRY(user_data);
        const gchar *cmd = gtk_entry_get_text(entry);
        if (cmd && *cmd) {
            g_print("Startup: would add command '%s' (not yet persisted)\n", cmd);
            // TODO: create .desktop file if full implementation desired
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dlg));
}

static void on_startup_add_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *parent_window = gtk_widget_get_toplevel(GTK_WIDGET(btn));
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add application or script",
                                                   GTK_WINDOW(parent_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "OK", GTK_RESPONSE_ACCEPT,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    GtkWidget *browse_btn = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(hbox), browse_btn, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), hbox);

    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_startup_browse_clicked), entry);
    g_signal_connect(dialog, "response", G_CALLBACK(on_add_startup_ok), entry);

    gtk_widget_show_all(dialog);
}

static GtkWidget* create_startup_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *add_btn = gtk_button_new_with_label("Add application or script");
    gtk_box_pack_start(GTK_BOX(toolbar), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    // Placeholder label; future list can be added here
    GtkWidget *placeholder = gtk_label_new("Startup entries will be listed here.");
    gtk_box_pack_start(GTK_BOX(vbox), placeholder, TRUE, TRUE, 0);

    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_startup_add_clicked), add_btn);

    return vbox;
}
#endif

/* Startup Applications section removed */

/* About tab implementation moved to src/ui/ui_about.c */

/* ---------------------------------------------------------------------
 * Global keybinding (Ctrl+Shift+Esc) integration via GNOME GSettings
 * -------------------------------------------------------------------*/

static void ensure_hotkey_binding(void) {
    const gchar *binding_path = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/khos-system-monitor/";

    // Create/lookup the media-keys settings
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
        // Append our binding path
        GPtrArray *array = g_ptr_array_new();
        for (gchar **p = current; p && *p; ++p) {
            g_ptr_array_add(array, g_strdup(*p));
        }
        g_ptr_array_add(array, g_strdup(binding_path));
        g_ptr_array_add(array, NULL);
        g_settings_set_strv(media_keys, "custom-keybindings", (const gchar * const *)array->pdata);
        // Free temporary array
        for (guint i = 0; i < array->len; ++i) {
            g_free(g_ptr_array_index(array, i));
        }
        g_ptr_array_free(array, TRUE);
    }

    g_strfreev(current);

    // Configure the specific custom keybinding
    GSettings *binding = g_settings_new_with_path(
        "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding",
        binding_path);

    if (!binding) {
        g_printerr("Failed to create binding GSettings object.\n");
        g_object_unref(media_keys);
        return;
    }

    // Determine executable path
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

int main (int argc, char **argv) {
    GtkApplication *app;
    int status;
  
    g_print("Starting application\n");
    g_print("Initializing CPU data\n");
    cpu_data_init();
    g_print("Initializing memory data\n");
    memory_data_init();
    g_print("Initialization complete\n");

    // Set the application icon for the entire application
    g_print("Setting application icon\n");
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if (gtk_icon_theme_has_icon(theme, "khos-system-monitor")) {
        gtk_window_set_default_icon_name("khos-system-monitor");
    } else {
        // Fallback to local file if icon not found in theme
        GError *error = NULL;
        if (!gtk_window_set_default_icon_from_file("khos-sm-logo.svg", &error)) {
            g_print("Error setting application icon: %s\n", error->message);
            g_error_free(error);
        } else {
            g_print("Application icon set from local file\n");
        }
    }

    app = gtk_application_new("org.gtk.systemmonitor", G_APPLICATION_DEFAULT_FLAGS);
    g_print("Application created\n");
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    g_print("Signal handlers connected\n");
    g_print("Running application\n");
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_print("Application run complete with status: %d\n", status);
    g_object_unref(app);
    g_print("Application unreferenced\n");
    
    // Final cleanup to ensure all resources are freed
    g_print("Performing final cleanup\n");
    if (process_cpu_times_hash) {
        g_hash_table_destroy(process_cpu_times_hash);
        process_cpu_times_hash = NULL;
    }
    cpu_data_cleanup();
    memory_data_cleanup();
    disk_data_cleanup();
    network_data_cleanup();
    gpu_data_cleanup();
    g_print("Final cleanup complete\n");

    return status;
} 
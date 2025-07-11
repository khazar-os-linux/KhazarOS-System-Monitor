#include "cpu/cpu_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <ctype.h>

static gdouble cpu_usage_history[MAX_POINTS] = {0.0};
static gdouble per_cpu_usage_history[MAX_CPU_CORES][MAX_POINTS] = {{0.0}};
static gdouble current_per_cpu_usage[MAX_CPU_CORES] = {0.0};
static gulong prev_cpu_stats[MAX_CPU_CORES][4] = {{0}}; // user, nice, system, idle
static gint cpu_usage_index = 0;
static gdouble current_cpu_usage = 0.0;
static gulong prev_cpu_total = 0;
static gulong prev_cpu_idle = 0;
static gboolean show_per_cpu_graphs = FALSE;

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

static void parse_cpuinfo() {
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
}

static void get_cpu_freq() {
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

static void get_cache_info() {
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

static void get_architecture() {
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

void cpu_data_init(void) {
    parse_cpuinfo();
    get_cpu_freq();
    get_cache_info();
    get_architecture();
    cpu_data_update();
}

void cpu_data_cleanup(void) {
    g_print("CPU data cleanup called\n");
    g_print("Freeing cpu_model\n");
    g_free(cpu_model); 
    g_print("Freeing cpu_cache_info\n");
    g_free(cpu_cache_info); 
    g_print("Freeing cpu_architecture\n");
    g_free(cpu_architecture);
    g_print("Freeing cpu_stepping\n");
    g_free(cpu_stepping); 
    g_print("Freeing cpu_family\n");
    g_free(cpu_family); 
    g_print("Freeing cpu_vendor_id\n");
    g_free(cpu_vendor_id);
    g_print("Freeing cpu_bogomips\n");
    g_free(cpu_bogomips); 
    g_print("Freeing cpu_address_sizes\n");
    g_free(cpu_address_sizes);
    g_print("CPU data cleanup finished\n");
}

void cpu_data_update(void) {
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
    for (int i = 0; i < cpu_threads && i < MAX_CPU_CORES; i++) {
        char cpu_line[32];
        snprintf(cpu_line, sizeof(cpu_line), "cpu%d ", i);
        
        // Reset file position
        rewind(fp);
        
        // Find the specific CPU line
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, cpu_line, strlen(cpu_line)) == 0) {
                gulong cpu_user = 0, cpu_nice = 0, cpu_system = 0, cpu_idle = 0, cpu_iowait = 0, cpu_irq = 0, cpu_softirq = 0, cpu_steal = 0;
                sscanf(line, "%*s %lu %lu %lu %lu %lu %lu %lu %lu", 
                       &cpu_user, &cpu_nice, &cpu_system, &cpu_idle, 
                       &cpu_iowait, &cpu_irq, &cpu_softirq, &cpu_steal);
                
                gulong cpu_total = cpu_user + cpu_nice + cpu_system + cpu_idle + cpu_iowait + cpu_irq + cpu_softirq + cpu_steal;
                gulong cpu_idle_total = cpu_idle + cpu_iowait;
                
                gulong prev_total = prev_cpu_stats[i][0] + prev_cpu_stats[i][1] + prev_cpu_stats[i][2] + prev_cpu_stats[i][3];
                gulong prev_idle = prev_cpu_stats[i][3];
                
                if (prev_total > 0 && cpu_total > prev_total) {
                    gulong total_diff = cpu_total - prev_total;
                    gulong idle_diff = cpu_idle_total - prev_idle;
                    current_per_cpu_usage[i] = 100.0 * (total_diff - idle_diff) / total_diff;
                    per_cpu_usage_history[i][cpu_usage_index] = current_per_cpu_usage[i] > 0 ? current_per_cpu_usage[i] : 0;
                }
                
                // Store current values for next update
                prev_cpu_stats[i][0] = cpu_user + cpu_nice;
                prev_cpu_stats[i][1] = cpu_system;
                prev_cpu_stats[i][2] = cpu_irq + cpu_softirq + cpu_steal;
                prev_cpu_stats[i][3] = cpu_idle_total;
                
                break;
            }
        }
    }
    
    // Increment the index after all CPUs are updated
    cpu_usage_index = (cpu_usage_index + 1) % MAX_POINTS;
    
    // Simplified CPU frequency update
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
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
}

const gchar* get_cpu_model(void) { return cpu_model ? cpu_model : "N/A"; }
gint get_cpu_cores(void) { return cpu_cores; }
gint get_cpu_threads(void) { return cpu_threads; }
gdouble get_cpu_freq_mhz(void) { return cpu_freq_mhz; }
const gchar* get_cpu_cache_info(void) { return cpu_cache_info ? cpu_cache_info : "N/A"; }
const gchar* get_cpu_architecture(void) { return cpu_architecture ? cpu_architecture : "N/A"; }
const gchar* get_cpu_stepping(void) { return cpu_stepping ? cpu_stepping : "N/A"; }
const gchar* get_cpu_family(void) { return cpu_family ? cpu_family : "N/A"; }
const gchar* get_cpu_vendor_id(void) { return cpu_vendor_id ? cpu_vendor_id : "N/A"; }
const gchar* get_cpu_bogomips(void) { return cpu_bogomips ? cpu_bogomips : "N/A"; }
const gchar* get_cpu_address_sizes(void) { return cpu_address_sizes ? cpu_address_sizes : "N/A"; }
gdouble get_current_cpu_usage(void) { return current_cpu_usage; }
const gdouble* get_cpu_usage_history(void) { return cpu_usage_history; }
gint get_cpu_usage_history_index(void) { return cpu_usage_index; }

// Per-CPU usage getters
gdouble get_cpu_usage_by_core(gint core_id) { 
    if (core_id >= 0 && core_id < cpu_threads && core_id < MAX_CPU_CORES) {
        return current_per_cpu_usage[core_id];
    }
    return 0.0;
}

const gdouble* get_cpu_usage_history_by_core(gint core_id) {
    if (core_id >= 0 && core_id < cpu_threads && core_id < MAX_CPU_CORES) {
        return per_cpu_usage_history[core_id];
    }
    return NULL;
}

gboolean get_show_per_cpu_graphs(void) {
    return show_per_cpu_graphs;
}

void set_show_per_cpu_graphs(gboolean show) {
    show_per_cpu_graphs = show;
}

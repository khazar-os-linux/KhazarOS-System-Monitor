#include "memory/memory_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>

static gdouble memory_usage_history[MAX_POINTS] = {0.0};
static gdouble swap_usage_history[MAX_POINTS] = {0.0};
static gint memory_usage_index = 0;
static gint swap_usage_index = 0;
static gdouble current_memory_usage_percent = 0.0;
static gdouble current_swap_usage_percent = 0.0;

static gulong total_memory = 0;
static gulong used_memory = 0;
static gulong free_memory = 0;
static gulong available_memory = 0;
static gulong buffer_memory = 0;
static gulong cached_memory = 0;
static gulong swap_total = 0;
static gulong swap_used = 0;
static gulong swap_free = 0;

void memory_data_init(void) {
    memory_data_update();
}

void memory_data_cleanup(void) {
    g_print("Memory data cleanup called\n");
    // Reset all memory values to avoid accessing stale data
    total_memory = 0;
    used_memory = 0;
    free_memory = 0;
    available_memory = 0;
    buffer_memory = 0;
    cached_memory = 0;
    swap_total = 0;
    swap_used = 0;
    swap_free = 0;
    
    // Reset memory usage history
    for (int i = 0; i < MAX_POINTS; i++) {
        memory_usage_history[i] = 0.0;
        swap_usage_history[i] = 0.0;
    }
    memory_usage_index = 0;
    swap_usage_index = 0;
    current_memory_usage_percent = 0.0;
    current_swap_usage_percent = 0.0;
    
    g_print("Memory data cleanup complete\n");
}

void memory_data_update(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    
    char line[256];
    gulong mem_total = 0, mem_free = 0, mem_available = 0, buffers = 0, cached = 0;
    gulong swap_total_kb = 0, swap_free_kb = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %lu kB", &mem_total);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line, "MemFree: %lu kB", &mem_free);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %lu kB", &mem_available);
        } else if (strncmp(line, "Buffers:", 8) == 0) {
            sscanf(line, "Buffers: %lu kB", &buffers);
        } else if (strncmp(line, "Cached:", 7) == 0 && !strstr(line, "SwapCached:")) {
            sscanf(line, "Cached: %lu kB", &cached);
        } else if (strncmp(line, "SwapTotal:", 10) == 0) {
            sscanf(line, "SwapTotal: %lu kB", &swap_total_kb);
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            sscanf(line, "SwapFree: %lu kB", &swap_free_kb);
        }
    }
    fclose(fp);
    
    // Convert KB to MB
    total_memory = mem_total / 1024;
    free_memory = mem_free / 1024;
    available_memory = mem_available / 1024;
    buffer_memory = buffers / 1024;
    cached_memory = cached / 1024;
    swap_total = swap_total_kb / 1024;
    swap_free = swap_free_kb / 1024;
    
    // Calculate used memory (total - free - buffers - cached)
    used_memory = total_memory - free_memory - buffer_memory - cached_memory;
    if (used_memory > total_memory) used_memory = total_memory; // Sanity check
    
    // Calculate swap used
    swap_used = swap_total - swap_free;
    
    // Calculate memory usage percentage
    if (total_memory > 0) {
        current_memory_usage_percent = 100.0 * used_memory / total_memory;
        memory_usage_history[memory_usage_index] = current_memory_usage_percent;
        memory_usage_index = (memory_usage_index + 1) % MAX_POINTS;
    }
    
    // Calculate swap usage percentage
    if (swap_total > 0) {
        current_swap_usage_percent = 100.0 * swap_used / swap_total;
        swap_usage_history[swap_usage_index] = current_swap_usage_percent;
        swap_usage_index = (swap_usage_index + 1) % MAX_POINTS;
    } else {
        current_swap_usage_percent = 0.0;
    }
}

// Memory getters
gulong get_total_memory(void) { return total_memory; }
gulong get_used_memory(void) { return used_memory; }
gulong get_free_memory(void) { return free_memory; }
gulong get_available_memory(void) { return available_memory; }
gulong get_buffer_memory(void) { return buffer_memory; }
gulong get_cached_memory(void) { return cached_memory; }
gulong get_swap_total(void) { return swap_total; }
gulong get_swap_used(void) { return swap_used; }
gulong get_swap_free(void) { return swap_free; }

// Memory usage history getters
gdouble get_current_memory_usage_percent(void) { return current_memory_usage_percent; }
const gdouble* get_memory_usage_history(void) { return memory_usage_history; }
gint get_memory_usage_history_index(void) { return memory_usage_index; }

// Swap usage history getters
gdouble get_current_swap_usage_percent(void) { return current_swap_usage_percent; }
const gdouble* get_swap_usage_history(void) { return swap_usage_history; }
gint get_swap_usage_history_index(void) { return swap_usage_index; } 
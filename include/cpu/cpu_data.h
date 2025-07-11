#ifndef CPU_DATA_H
#define CPU_DATA_H

#include <glib.h>

#define MAX_POINTS 60 
#define MAX_CPU_CORES 64

// Initialization and Cleanup
void cpu_data_init(void);
void cpu_data_cleanup(void);

// Update function
void cpu_data_update(void);

// Getter functions for CPU specs
const gchar* get_cpu_model(void);
gint get_cpu_cores(void);
gint get_cpu_threads(void);
gdouble get_cpu_freq_mhz(void);
const gchar* get_cpu_cache_info(void);
const gchar* get_cpu_architecture(void);
const gchar* get_cpu_stepping(void);
const gchar* get_cpu_family(void);
const gchar* get_cpu_vendor_id(void);
const gchar* get_cpu_bogomips(void);
const gchar* get_cpu_address_sizes(void);

// Getter functions for CPU usage
gdouble get_current_cpu_usage(void);
const gdouble* get_cpu_usage_history(void);
gint get_cpu_usage_history_index(void);

// Getter functions for per-CPU usage
gdouble get_cpu_usage_by_core(gint core_id);
const gdouble* get_cpu_usage_history_by_core(gint core_id);
gboolean get_show_per_cpu_graphs(void);
void set_show_per_cpu_graphs(gboolean show);

#endif // CPU_DATA_H

#ifndef GPU_DATA_H
#define GPU_DATA_H

#include <glib.h>

#define GPU_MAX_POINTS 60

typedef struct {
    gchar name[256];
    gchar vendor[64];
    gchar driver_version[64];
    gdouble usage_percent;
    gdouble vram_used_mb;
    gdouble vram_total_mb;
    gdouble vram_usage_percent;
    gdouble usage_history[GPU_MAX_POINTS];
    gdouble vram_history[GPU_MAX_POINTS];
    gint history_index;
    gint gpu_id;  // ID to identify this GPU
} GPUInfo;

void gpu_data_init(void);
void gpu_data_cleanup(void);
void gpu_data_update(void);

// Accessors for the primary GPU
const gchar* gpu_get_name(void);
const gchar* gpu_get_vendor(void);
const gchar* gpu_get_driver_version(void);
gdouble gpu_get_usage(void);
gdouble gpu_get_vram_used(void);
gdouble gpu_get_vram_total(void);
gdouble gpu_get_vram_usage_percent(void);
const gdouble* gpu_get_usage_history(void);
const gdouble* gpu_get_vram_history(void);
gint gpu_get_history_index(void);

// New functions for multiple GPUs
gint gpu_get_count(void);
const GPUInfo* gpu_get_info(gint index);

#endif // GPU_DATA_H 
#ifndef DISK_DATA_H
#define DISK_DATA_H

#include <glib.h>

#define MAX_POINTS 60 
#define MAX_DISKS 8

typedef struct {
    gchar device_name[64];
    gchar mount_point[256];
    gchar fs_type[32];
    guint64 total_space;
    guint64 used_space;
    guint64 free_space;
    gdouble usage_percent;
    gdouble usage_history[MAX_POINTS];
    gint history_index;
    
    // Disk activity monitoring
    guint64 prev_read_bytes;
    guint64 prev_write_bytes;
    guint64 current_read_bytes;
    guint64 current_write_bytes;
    gdouble activity_percent;
    gdouble activity_history[MAX_POINTS];
    gint activity_history_index;
} DiskInfo;

// Initialization and Cleanup
void disk_data_init(void);
void disk_data_cleanup(void);

// Update function
void disk_data_update(void);

// Getter functions
gint get_disk_count(void);
const DiskInfo* get_disk_info(gint index);
guint64 get_disk_size(gint index);
const gchar* get_disk_type(gint index);  // SSD or HDD

// Getter functions for disk usage history
gdouble get_current_disk_usage_percent(gint index);
const gdouble* get_disk_usage_history(gint index);
gint get_disk_usage_history_index(gint index);

// Getter functions for disk activity history
gdouble get_current_disk_activity_percent(gint index);
const gdouble* get_disk_activity_history(gint index);
gint get_disk_activity_history_index(gint index);

#endif // DISK_DATA_H 
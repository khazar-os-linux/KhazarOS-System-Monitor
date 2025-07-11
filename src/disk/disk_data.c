#include "disk/disk_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <ctype.h>
#include <sys/types.h>

// Global array to store disk information
static DiskInfo disks[MAX_DISKS];
static gint disk_count = 0;

// Additional disk specs
static gchar disk_types[MAX_DISKS][16]; // SSD or HDD

// Function to read disk stats from /proc/diskstats
static void read_disk_stats(void) {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) {
        g_print("Failed to open /proc/diskstats\n");
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char device_name[64];
        unsigned int major, minor;
        unsigned long long reads_completed, reads_merged, sectors_read, ms_reading;
        unsigned long long writes_completed, writes_merged, sectors_written, ms_writing;
        unsigned long long io_in_progress, ms_io, weighted_ms_io;
        
        int matched = sscanf(line, "%u %u %s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                           &major, &minor, device_name,
                           &reads_completed, &reads_merged, &sectors_read, &ms_reading,
                           &writes_completed, &writes_merged, &sectors_written, &ms_writing,
                           &io_in_progress, &ms_io, &weighted_ms_io);
        
        if (matched == 14) {
            // Match device name with our disks
            for (int i = 0; i < disk_count; i++) {
                if (strcmp(device_name, disks[i].device_name) == 0) {
                    // Store previous values
                    disks[i].prev_read_bytes = disks[i].current_read_bytes;
                    disks[i].prev_write_bytes = disks[i].current_write_bytes;
                    
                    // Update current values (sectors are usually 512 bytes)
                    disks[i].current_read_bytes = sectors_read * 512;
                    disks[i].current_write_bytes = sectors_written * 512;
                    
                    // Calculate activity percentage based on read/write delta
                    if (disks[i].prev_read_bytes > 0 || disks[i].prev_write_bytes > 0) {
                        guint64 read_delta = 0;
                        guint64 write_delta = 0;
                        
                        if (disks[i].current_read_bytes > disks[i].prev_read_bytes) {
                            read_delta = disks[i].current_read_bytes - disks[i].prev_read_bytes;
                        }
                        
                        if (disks[i].current_write_bytes > disks[i].prev_write_bytes) {
                            write_delta = disks[i].current_write_bytes - disks[i].prev_write_bytes;
                        }
                        
                        // Calculate activity as a percentage (0-100)
                        // We'll use a scale factor to make the percentage more visible
                        // Typical NVMe can do ~3500MB/s, SSD ~550MB/s, HDD ~150MB/s
                        double scale_factor = 1.0;
                        if (strcmp(disk_types[i], "SSD") == 0) {
                            scale_factor = 0.2; // Scale for SSDs
                        } else if (strcmp(disk_types[i], "NVMe") == 0) {
                            scale_factor = 0.05; // Scale for NVMe
                        } else {
                            scale_factor = 0.7; // Scale for HDDs
                        }
                        
                        // Calculate activity percentage (MB/s * scale)
                        double total_mb_per_sec = (read_delta + write_delta) / (1024.0 * 1024.0);
                        disks[i].activity_percent = total_mb_per_sec * scale_factor;
                        
                        // Cap at 100%
                        if (disks[i].activity_percent > 100.0) {
                            disks[i].activity_percent = 100.0;
                        }
                        
                        // Update history
                        disks[i].activity_history[disks[i].activity_history_index] = disks[i].activity_percent;
                        disks[i].activity_history_index = (disks[i].activity_history_index + 1) % MAX_POINTS;
                    }
                    break;
                }
            }
        }
    }
    
    fclose(fp);
}

void disk_data_init(void) {
    g_print("Initializing disk data\n");
    
    // Reset disk count
    disk_count = 0;
    
    // Initialize disk info structures
    for (gint i = 0; i < MAX_DISKS; i++) {
        memset(&disks[i], 0, sizeof(DiskInfo));
        disks[i].history_index = 0;
        disks[i].activity_history_index = 0;
        for (gint j = 0; j < MAX_POINTS; j++) {
            disks[i].usage_history[j] = 0.0;
            disks[i].activity_history[j] = 0.0;
        }
        
        // Initialize additional specs
        strcpy(disk_types[i], "Unknown");
    }
    
    // We'll update disk data after initialization
    // Don't call disk_data_update() here to avoid double initialization
    
    // Try to get disk type information (SSD or HDD)
    FILE *fp;
    char line[256];
    
    // Use lsblk to get disk information
    fp = popen("lsblk -d -o NAME,ROTA --json 2>/dev/null", "r");
    if (fp) {
        char output[4096] = {0};
        while (fgets(line, sizeof(line), fp)) {
            strcat(output, line);
        }
        pclose(fp);
        
        // Basic parsing of JSON output
        for (gint i = 0; i < disk_count; i++) {
            char search_pattern[128];
            char *pos;
            
            // Determine if SSD or HDD based on rotation flag
            snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", disks[i].device_name);
            pos = strstr(output, search_pattern);
            if (pos) {
                // Check if it's removable
                char *rm_pos = strstr(pos, "\"rm\":");
                gboolean is_removable = FALSE;
                
                if (rm_pos && strstr(rm_pos, "\"rm\":true")) {
                    is_removable = TRUE;
                }
                
                pos = strstr(pos, "\"rota\":");
                if (pos) {
                    if (strstr(pos, "\"rota\":true")) {
                        if (is_removable) {
                            strcpy(disk_types[i], "USB HDD");
                        } else {
                            strcpy(disk_types[i], "HDD");
                        }
                    } else if (strstr(pos, "\"rota\":false")) {
                        if (is_removable) {
                            strcpy(disk_types[i], "USB Flash");
                        } else {
                            if (strncmp(disks[i].device_name, "nvme", 4) == 0) {
                                strcpy(disk_types[i], "NVMe");
                            } else {
                                strcpy(disk_types[i], "SSD");
                            }
                        }
                    }
                }
            }
        }
    }
    
    g_print("Disk data initialization complete. Found %d disks.\n", disk_count);
}

void disk_data_cleanup(void) {
    g_print("Cleaning up disk data\n");
    // Nothing to clean up for now
}

void disk_data_update(void) {
    g_print("Updating disk data\n");
    
    // Reset disk count
    disk_count = 0;
    
    // Use lsblk to get physical disks only (not partitions) with more detailed info
    FILE *fp = popen("lsblk -d -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT,LABEL,RM --json 2>/dev/null", "r");
    if (!fp) {
        g_print("Failed to run lsblk command\n");
        return;
    }
    
    // Read the output
    char buffer[4096] = {0};
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        strcat(buffer, line);
    }
    pclose(fp);
    
    // Very basic JSON parsing - in a real app you would use a JSON library
    char *devices_start = strstr(buffer, "\"blockdevices\"");
    if (!devices_start) {
        g_print("No devices found in lsblk output\n");
        return;
    }
    
    // Process each disk
    char *pos = devices_start;
    while ((pos = strstr(pos, "\"name\":")) && disk_count < MAX_DISKS) {
        pos += 8; // Skip "\"name\":"
        
        // Extract disk name
        char disk_name[64] = {0};
        if (*pos == '\"') {
            pos++; // Skip the opening quote
            int i = 0;
            while (*pos && *pos != '\"' && i < sizeof(disk_name) - 1) {
                disk_name[i++] = *pos++;
            }
            disk_name[i] = '\0';
        }
        
        g_print("Found disk: %s\n", disk_name);
        
        // Skip loop devices, zram, and other non-physical devices
        if (strncmp(disk_name, "loop", 4) == 0 || 
            strncmp(disk_name, "zram", 4) == 0 || 
            strncmp(disk_name, "dm-", 3) == 0 ||
            strncmp(disk_name, "sr", 2) == 0) {
            g_print("Skipping non-physical device: %s\n", disk_name);
            continue;
        }
        
        // Include NVMe devices, MMC, and other storage types
        gboolean is_storage = FALSE;
        if (strncmp(disk_name, "sd", 2) == 0 ||      // SATA/SCSI disks
            strncmp(disk_name, "nvme", 4) == 0 ||    // NVMe disks
            strncmp(disk_name, "mmc", 3) == 0 ||     // MMC storage
            strncmp(disk_name, "vd", 2) == 0 ||      // Virtual disks
            strncmp(disk_name, "hd", 2) == 0) {      // IDE disks (legacy)
            is_storage = TRUE;
        }
        
        // Check if it's a disk (not a partition)
        char *type_pos = strstr(pos, "\"type\":");
        char *rm_pos = strstr(pos, "\"rm\":");
        gboolean is_removable = FALSE;
        
        if (rm_pos && strstr(rm_pos, "\"rm\":true")) {
            is_removable = TRUE;
            g_print("Found removable disk: %s\n", disk_name);
        }
        
        if ((type_pos && strstr(type_pos, "\"disk\"")) || is_storage || is_removable) {
            g_print("Found physical disk: %s\n", disk_name);
            // Get disk statistics
            char path[128];
            snprintf(path, sizeof(path), "/dev/%s", disk_name);
            
            // Find a mount point for this disk
            char mount_point[256] = "/";  // Default to root if no specific mount found
            char fs_type[32] = "unknown";
            
            // First try to get filesystem info directly from lsblk output
            char *fstype_pos = strstr(pos, "\"fstype\":");
            char *mountpoint_pos = strstr(pos, "\"mountpoint\":");
            
            if (fstype_pos && mountpoint_pos) {
                // Extract filesystem type
                fstype_pos += 10; // Skip "\"fstype\":"
                if (*fstype_pos == '\"') {
                    fstype_pos++; // Skip the opening quote
                    int i = 0;
                    while (*fstype_pos && *fstype_pos != '\"' && i < sizeof(fs_type) - 1) {
                        fs_type[i++] = *fstype_pos++;
                    }
                    fs_type[i] = '\0';
                }
                
                // Extract mount point
                mountpoint_pos += 14; // Skip "\"mountpoint\":"
                if (*mountpoint_pos == '\"') {
                    mountpoint_pos++; // Skip the opening quote
                    int i = 0;
                    while (*mountpoint_pos && *mountpoint_pos != '\"' && i < sizeof(mount_point) - 1) {
                        mount_point[i++] = *mountpoint_pos++;
                    }
                    mount_point[i] = '\0';
                }
            }
            
            // If we couldn't get info from lsblk, try to find the main mount point for this disk
            FILE *mtab = setmntent("/etc/mtab", "r");
            if (mtab) {
                struct mntent *entry;
                while ((entry = getmntent(mtab))) {
                    // Skip pseudo filesystems
                    if (strcmp(entry->mnt_type, "proc") == 0 ||
                        strcmp(entry->mnt_type, "sysfs") == 0 ||
                        strcmp(entry->mnt_type, "devtmpfs") == 0 ||
                        strcmp(entry->mnt_type, "devpts") == 0 ||
                        strcmp(entry->mnt_type, "tmpfs") == 0 ||
                        strcmp(entry->mnt_type, "debugfs") == 0 ||
                        strcmp(entry->mnt_type, "securityfs") == 0 ||
                        strcmp(entry->mnt_type, "fusectl") == 0 ||
                        strcmp(entry->mnt_type, "cgroup") == 0 ||
                        strcmp(entry->mnt_type, "cgroup2") == 0 ||
                        strcmp(entry->mnt_type, "pstore") == 0 ||
                        strcmp(entry->mnt_type, "efivarfs") == 0 ||
                        strcmp(entry->mnt_type, "autofs") == 0) {
                        continue;
                    }
                    
                    g_print("Checking mount entry: %s on %s (type %s)\n", 
                            entry->mnt_fsname, entry->mnt_dir, entry->mnt_type);
                    
                    // Check if this mount point is on our disk
                    // Extract the base device name from entry->mnt_fsname
                    const char *dev_name = entry->mnt_fsname;
                    if (strncmp(dev_name, "/dev/", 5) == 0) {
                        dev_name += 5;
                    }
                    
                    // Check if the mount point is for a partition on this disk
                    // For example, if disk_name is "sda", check for "sda1", "sda2", etc.
                    // Also handle NVMe naming pattern (nvme0n1p1, etc.)
                    if (strncmp(dev_name, disk_name, strlen(disk_name)) == 0) {
                        g_print("Found mount point %s for disk %s\n", entry->mnt_dir, disk_name);
                        
                        // Prefer root filesystem or home directory if available
                        if (strcmp(entry->mnt_dir, "/") == 0 || 
                            strcmp(entry->mnt_dir, "/home") == 0) {
                            strncpy(mount_point, entry->mnt_dir, sizeof(mount_point) - 1);
                            strncpy(fs_type, entry->mnt_type, sizeof(fs_type) - 1);
                            break;
                        }
                        
                        // Otherwise, use the first valid mount point
                        if (strcmp(mount_point, "/") == 0) {
                            strncpy(mount_point, entry->mnt_dir, sizeof(mount_point) - 1);
                            strncpy(fs_type, entry->mnt_type, sizeof(fs_type) - 1);
                        }
                    }
                }
                endmntent(mtab);
            }
            
            // Get filesystem statistics for the mount point
            struct statvfs stat;
            if (statvfs(mount_point, &stat) == 0) {
                // Fill in disk info
                strncpy(disks[disk_count].device_name, disk_name, sizeof(disks[disk_count].device_name) - 1);
                strncpy(disks[disk_count].mount_point, mount_point, sizeof(disks[disk_count].mount_point) - 1);
                strncpy(disks[disk_count].fs_type, fs_type, sizeof(disks[disk_count].fs_type) - 1);
                
                // Calculate disk space (in MB)
                disks[disk_count].total_space = (guint64)(stat.f_blocks * stat.f_frsize) / (1024 * 1024);
                disks[disk_count].free_space = (guint64)(stat.f_bfree * stat.f_frsize) / (1024 * 1024);
                disks[disk_count].used_space = disks[disk_count].total_space - disks[disk_count].free_space;
                
                g_print("Disk %s stats: total=%lu MB, free=%lu MB, used=%lu MB\n",
                        disk_name,
                        disks[disk_count].total_space,
                        disks[disk_count].free_space,
                        disks[disk_count].used_space);
                
                // Calculate usage percentage
                if (disks[disk_count].total_space > 0) {
                    disks[disk_count].usage_percent = 100.0 * disks[disk_count].used_space / disks[disk_count].total_space;
                } else {
                    disks[disk_count].usage_percent = 0.0;
                }
                
                // Update history
                disks[disk_count].usage_history[disks[disk_count].history_index] = disks[disk_count].usage_percent;
                disks[disk_count].history_index = (disks[disk_count].history_index + 1) % MAX_POINTS;
                
                disk_count++;
            }
        }
    }
    
    // After updating disk info, read disk stats
    read_disk_stats();
    
    g_print("Disk data update complete\n");
}

gint get_disk_count(void) {
    return disk_count;
}

const DiskInfo* get_disk_info(gint index) {
    if (index >= 0 && index < disk_count) {
        return &disks[index];
    }
    return NULL;
}

guint64 get_disk_size(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].total_space;
    }
    return 0;
}

const gchar* get_disk_type(gint index) {
    if (index >= 0 && index < disk_count) {
        return disk_types[index];
    }
    return "Unknown";
}

gdouble get_current_disk_usage_percent(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].usage_percent;
    }
    return 0.0;
}

const gdouble* get_disk_usage_history(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].usage_history;
    }
    return NULL;
}

gint get_disk_usage_history_index(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].history_index;
    }
    return 0;
}

// Getter functions for disk activity
gdouble get_current_disk_activity_percent(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].activity_percent;
    }
    return 0.0;
}

const gdouble* get_disk_activity_history(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].activity_history;
    }
    return NULL;
}

gint get_disk_activity_history_index(gint index) {
    if (index >= 0 && index < disk_count) {
        return disks[index].activity_history_index;
    }
    return 0;
} 
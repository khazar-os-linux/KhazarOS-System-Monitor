#include "gpu/gpu_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
#define MAX_GPUS 8

static gboolean file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static gdouble read_double_from_file(const char *path, gdouble divisor) {
    gdouble value = 0.0;
    FILE *fp = fopen(path, "r");
    if (fp) {
        if (fscanf(fp, "%lf", &value) != 1) {
            value = 0.0;
        }
        fclose(fp);
    }
    return value / divisor;
}

// Trim newlines from a string
static void trim_newline(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

// Extract the basename from a path
static char* get_basename(const char *path, char *buffer, size_t buffer_size) {
    if (!path || !buffer || buffer_size == 0) return NULL;
    
    // Find the last slash
    const char *last_slash = strrchr(path, '/');
    const char *name = last_slash ? last_slash + 1 : path;
    
    // Copy the basename to the buffer
    strncpy(buffer, name, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return buffer;
}

static gchar* trim_newline_str(char *s) {
    if (!s) return s;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static void shorten_gpu_name(char *s) {
    if (!s) return;
    // Remove leading slot id and descriptor upto ':'
    char *colon = strchr(s, ':');
    if (colon && colon[1] == ' ') {
        memmove(s, colon + 2, strlen(colon + 2) + 1);
    }
    // Remove descriptors like "VGA compatible controller" if still present
    const char *ctrl = "VGA compatible controller";
    if (strncmp(s, ctrl, strlen(ctrl)) == 0) {
        char *after = strstr(s, "NVIDIA");
        if (!after) after = strstr(s, "AMD");
        if (!after) after = strstr(s, "Advanced Micro Devices");
        if (after) {
            memmove(s, after, strlen(after) + 1);
        }
    }
    
    // Extract model information from brackets and create a concise name
    char *bracket = strchr(s, '[');
    if (bracket) {
        char *end = strchr(bracket, ']');
        if (end) {
            // Get content inside brackets (typically contains model info)
            char model[128] = {0};
            size_t len = end - bracket - 1;
            if (len < sizeof(model)) {
                strncpy(model, bracket + 1, len);
                model[len] = '\0';
                
                // Determine vendor
                char vendor[32] = {0};
                if (strstr(s, "NVIDIA")) {
                    strcpy(vendor, "NVIDIA");
                } else if (strstr(s, "AMD") || strstr(s, "ATI")) {
                    strcpy(vendor, "AMD");
                } else if (strstr(s, "Intel")) {
                    strcpy(vendor, "Intel");
                }
                
                // Format a concise name based on model number
                if (strlen(vendor) > 0) {
                    // For NVIDIA cards
                    if (strcmp(vendor, "NVIDIA") == 0) {
                        char *geforce = strstr(model, "GeForce");
                        char *quadro = strstr(model, "Quadro");
                        char *tesla = strstr(model, "Tesla");
                        char *rtx = strstr(model, "RTX");
                        char *gtx = strstr(model, "GTX");
                        
                        if (geforce || quadro || tesla || rtx || gtx) {
                            // Already has a good prefix, just use the model as is
                            snprintf(s, 128, "NVIDIA %s", model);
                        } else {
                            // Try to extract model number
                            char *digits = model;
                            while (*digits && !isdigit(*digits)) digits++;
                            if (*digits) {
                                snprintf(s, 128, "NVIDIA GeForce %s", digits);
                            } else {
                                snprintf(s, 128, "NVIDIA %s", model);
                            }
                        }
                    }
                    // For AMD cards
                    else if (strcmp(vendor, "AMD") == 0) {
                        char *radeon = strstr(model, "Radeon");
                        char *rx = strstr(model, "RX");
                        char *vega = strstr(model, "Vega");
                        
                        if (radeon) {
                            snprintf(s, 128, "AMD %s", model);
                        } else if (rx || vega) {
                            snprintf(s, 128, "AMD Radeon %s", model);
                        } else {
                            // Try to extract model number
                            char *digits = model;
                            while (*digits && !isdigit(*digits)) digits++;
                            if (*digits) {
                                snprintf(s, 128, "AMD Radeon %s", digits);
                            } else {
                                snprintf(s, 128, "AMD %s", model);
                            }
                        }
                    }
                    // For Intel cards
                    else if (strcmp(vendor, "Intel") == 0) {
                        snprintf(s, 128, "Intel %s", model);
                    }
                } else {
                    // Unknown vendor, just use the model
                    snprintf(s, 128, "%s", model);
                }
            }
        }
    }
    
    // Remove trailing revision/parentheses
    char *par = strchr(s, '(');
    if (par) *par = '\0';
    
    // Trim whitespace
    trim_newline(s);
    
    // Trim trailing spaces
    size_t len = strlen(s);
    while (len > 0 && s[len-1] == ' ') {
        s[--len] = '\0';
    }
}

//-----------------------------------------------------------------------------
static GPUInfo gpu_infos[MAX_GPUS];
static gint gpu_count = 0;

// Helper function to get Intel GPU information from intel_gpu_top
static void get_intel_gpu_info(GPUInfo *gpu) {
    // Try to get more detailed Intel GPU information
    FILE *fp = popen("lspci -d 8086: -nn | grep -i 'VGA\\|Display' | head -n1", "r");
    if (fp) {
        char line[256] = {0};
        if (fgets(line, sizeof(line), fp)) {
            trim_newline(line);
            
            // Extract the device ID
            char *device_id = strstr(line, "[8086:");
            if (device_id) {
                device_id += 6; // Skip "[8086:"
                char *end = strchr(device_id, ']');
                if (end) *end = '\0';
                
                // Now we have the device ID, we can try to get a better name
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "grep -i '%s' /usr/share/hwdata/pci.ids | head -n1 2>/dev/null", device_id);
                FILE *fp_name = popen(cmd, "r");
                if (fp_name) {
                    char name[256] = {0};
                    if (fgets(name, sizeof(name), fp_name)) {
                        trim_newline(name);
                        // Remove leading tabs/spaces
                        char *start = name;
                        while (*start && isspace(*start)) start++;
                        if (*start) {
                            snprintf(gpu->name, sizeof(gpu->name), "Intel %s", start);
                        }
                    }
                    pclose(fp_name);
                }
            }
            
            // If we still don't have a good name, use the original line
            if (strlen(gpu->name) < 5) { // "Intel" is 5 chars
                char *vga_pos = strstr(line, "VGA");
                if (vga_pos) {
                    *vga_pos = '\0'; // Cut off at VGA
                }
                char *display_pos = strstr(line, "Display");
                if (display_pos) {
                    *display_pos = '\0'; // Cut off at Display
                }
                
                // Extract name after the first colon
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++; // Skip the colon
                    while (*colon && isspace(*colon)) colon++; // Skip spaces
                    if (*colon) {
                        snprintf(gpu->name, sizeof(gpu->name), "Intel %s", colon);
                    } else {
                        strcpy(gpu->name, "Intel GPU");
                    }
                } else {
                    strcpy(gpu->name, "Intel GPU");
                }
            }
        }
        pclose(fp);
    }
    
    // Try to get driver version
    FILE *fp_driver = popen("modinfo i915 | grep -i 'version:' | head -n1", "r");
    if (fp_driver) {
        char line[256] = {0};
        if (fgets(line, sizeof(line), fp_driver)) {
            char *ver = strstr(line, "version:");
            if (ver) {
                ver += 8; // Skip "version:"
                while (*ver && isspace(*ver)) ver++; // Skip spaces
                if (*ver) {
                    strncpy(gpu->driver_version, ver, sizeof(gpu->driver_version) - 1);
                    trim_newline(gpu->driver_version);
                }
            }
        }
        pclose(fp_driver);
    }
    
    // If we still don't have a driver version, try dmesg
    if (strlen(gpu->driver_version) == 0) {
        FILE *fp_dmesg = popen("dmesg | grep -i 'i915.*initialized' | tail -n1", "r");
        if (fp_dmesg) {
            char line[512] = {0};
            if (fgets(line, sizeof(line), fp_dmesg)) {
                char *ver = strstr(line, "i915");
                if (ver) {
                    strncpy(gpu->driver_version, ver, sizeof(gpu->driver_version) - 1);
                    trim_newline(gpu->driver_version);
                }
            }
            pclose(fp_dmesg);
        }
    }
    
    // If we still don't have a driver version, try glxinfo
    if (strlen(gpu->driver_version) == 0) {
        FILE *fp_glx = popen("glxinfo 2>/dev/null | grep -i 'opengl version' | head -n1", "r");
        if (fp_glx) {
            char line[256] = {0};
            if (fgets(line, sizeof(line), fp_glx)) {
                char *ver = strstr(line, "OpenGL version string:");
                if (ver) {
                    ver += 22; // Skip "OpenGL version string:"
                    while (*ver && isspace(*ver)) ver++; // Skip spaces
                    if (*ver) {
                        strncpy(gpu->driver_version, ver, sizeof(gpu->driver_version) - 1);
                        trim_newline(gpu->driver_version);
                    }
                }
            }
            pclose(fp_glx);
        }
    }
    
    // Default if we still don't have a driver version
    if (strlen(gpu->driver_version) == 0) {
        strcpy(gpu->driver_version, "-");
    }
}

void gpu_data_init(void) {
    memset(gpu_infos, 0, sizeof(gpu_infos));
    gpu_count = 0;
    
    // Try NVIDIA-SMI first for NVIDIA GPUs
    FILE *fp_nvidia = popen("nvidia-smi --query-gpu=index,name,driver_version --format=csv,noheader 2>/dev/null", "r");
    if (fp_nvidia) {
        char line[512];
        while (fgets(line, sizeof(line), fp_nvidia) && gpu_count < MAX_GPUS) {
            int index;
            char name[256];
            char driver[64];
            
            if (sscanf(line, "%d, %255[^,], %63[^\n]", &index, name, driver) == 3) {
                GPUInfo *gpu = &gpu_infos[gpu_count];
                
                gpu->gpu_id = index;
                strncpy(gpu->name, name, sizeof(gpu->name) - 1);
                strncpy(gpu->vendor, "NVIDIA", sizeof(gpu->vendor) - 1);
                strncpy(gpu->driver_version, driver, sizeof(gpu->driver_version) - 1);
                
                // Init history
                for (int i = 0; i < GPU_MAX_POINTS; i++) {
                    gpu->usage_history[i] = 0.0;
                    gpu->vram_history[i] = 0.0;
                }
                gpu->history_index = 0;
                
                gpu_count++;
            }
        }
        pclose(fp_nvidia);
    }
    
    // Try AMD/Intel via DRM info
    if (gpu_count < MAX_GPUS) {
        // Check how many GPUs are available
        FILE *fp_count = popen("ls -1 /sys/class/drm/card*/device/driver 2>/dev/null | wc -l", "r");
        if (fp_count) {
            int count;
            if (fscanf(fp_count, "%d", &count) == 1) {
                for (int i = 0; i < count && gpu_count < MAX_GPUS; i++) {
                    char path[256];
                    char driver_path[256];
                    snprintf(driver_path, sizeof(driver_path), "/sys/class/drm/card%d/device/driver", i);
                    
                    // Check if the driver path exists
                    if (!file_exists(driver_path)) {
                        continue;
                    }
                    
                    // Read the symlink target
                    char real_path[512] = {0};
                    ssize_t len = readlink(driver_path, real_path, sizeof(real_path) - 1);
                    if (len < 0) {
                        continue;
                    }
                    real_path[len] = '\0';
                    
                    // Extract driver name from path
                    char driver[64] = {0};
                    get_basename(real_path, driver, sizeof(driver));
                    
                    if (strlen(driver) > 0) {
                        GPUInfo *gpu = &gpu_infos[gpu_count];
                        gpu->gpu_id = i;
                        
                        // Try to get model name
                        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/product", i);
                        FILE *fp_model = fopen(path, "r");
                        if (fp_model) {
                            if (fgets(gpu->name, sizeof(gpu->name), fp_model)) {
                                trim_newline(gpu->name);
                            } else {
                                snprintf(gpu->name, sizeof(gpu->name), "%s GPU %d", driver, i);
                            }
                            fclose(fp_model);
                        } else {
                            snprintf(gpu->name, sizeof(gpu->name), "%s GPU %d", driver, i);
                        }
                        
                        // Set vendor based on driver
                        if (strcmp(driver, "amdgpu") == 0) {
                            strcpy(gpu->vendor, "AMD");
                            
                            // Try to get driver version
                            FILE *fp_ver = fopen("/sys/module/amdgpu/version", "r");
                            if (fp_ver) {
                                if (fgets(gpu->driver_version, sizeof(gpu->driver_version), fp_ver)) {
                                    trim_newline(gpu->driver_version);
                                } else {
                                    strcpy(gpu->driver_version, "-");
                                }
                                fclose(fp_ver);
                            } else {
                                strcpy(gpu->driver_version, "-");
                            }
                        } else if (strcmp(driver, "i915") == 0) {
                            strcpy(gpu->vendor, "Intel");
                            
                            // Get better Intel GPU info
                            get_intel_gpu_info(gpu);
                        } else {
                            strcpy(gpu->vendor, driver);
                            strcpy(gpu->driver_version, "-");
                        }
                        
                        // Init history
                        for (int j = 0; j < GPU_MAX_POINTS; j++) {
                            gpu->usage_history[j] = 0.0;
                            gpu->vram_history[j] = 0.0;
                        }
                        gpu->history_index = 0;
                        
                        gpu_count++;
                    }
                }
            }
            pclose(fp_count);
        }
    }
    
    // Try glxinfo if no GPUs found
    if (gpu_count == 0) {
        FILE *fp_glx = popen("glxinfo 2>/dev/null | grep 'OpenGL renderer string'", "r");
        if (fp_glx) {
            char line[256];
            if (fgets(line, sizeof(line), fp_glx)) {
                char *renderer = strstr(line, "OpenGL renderer string:");
                if (renderer) {
                    renderer += strlen("OpenGL renderer string:");
                    while (*renderer == ' ') renderer++; // Skip spaces
                    
                    GPUInfo *gpu = &gpu_infos[gpu_count];
                    gpu->gpu_id = 0;
                    
                    strncpy(gpu->name, renderer, sizeof(gpu->name) - 1);
                    trim_newline(gpu->name);
                    
                    // Determine vendor from renderer string
                    if (strstr(gpu->name, "NVIDIA")) {
                        strcpy(gpu->vendor, "NVIDIA");
                    } else if (strstr(gpu->name, "AMD") || strstr(gpu->name, "ATI") || strstr(gpu->name, "Radeon")) {
                        strcpy(gpu->vendor, "AMD");
                    } else if (strstr(gpu->name, "Intel")) {
                        strcpy(gpu->vendor, "Intel");
                        // Get better Intel GPU info
                        get_intel_gpu_info(gpu);
                    } else {
                        strcpy(gpu->vendor, "Unknown");
                    }
                    
                    // Try to get driver version
                    FILE *fp_ver = popen("glxinfo 2>/dev/null | grep 'OpenGL version string' | head -n1", "r");
                    if (fp_ver) {
                        if (fgets(line, sizeof(line), fp_ver)) {
                            char *version = strstr(line, "OpenGL version string:");
                            if (version) {
                                version += strlen("OpenGL version string:");
                                while (*version == ' ') version++; // Skip spaces
                                strncpy(gpu->driver_version, version, sizeof(gpu->driver_version) - 1);
                                trim_newline(gpu->driver_version);
                            } else {
                                strcpy(gpu->driver_version, "-");
                            }
                        } else {
                            strcpy(gpu->driver_version, "-");
                        }
                        pclose(fp_ver);
                    } else {
                        strcpy(gpu->driver_version, "-");
                    }
                    
                    // Init history
                    for (int i = 0; i < GPU_MAX_POINTS; i++) {
                        gpu->usage_history[i] = 0.0;
                        gpu->vram_history[i] = 0.0;
                    }
                    gpu->history_index = 0;
                    
                    gpu_count++;
                }
            }
            pclose(fp_glx);
        }
    }
    
    // Check for Intel GPUs specifically if none found yet
    if (gpu_count == 0) {
        FILE *fp = popen("lspci -d 8086: -nn | grep -i 'VGA\\|Display' | head -n1", "r");
        if (fp) {
            if (fgets(gpu_infos[0].name, sizeof(gpu_infos[0].name), fp)) {
                trim_newline(gpu_infos[0].name);
                strcpy(gpu_infos[0].vendor, "Intel");
                gpu_infos[0].gpu_id = 0;
                
                // Get better Intel GPU info
                get_intel_gpu_info(&gpu_infos[0]);
                
                // Init history
                for (int i = 0; i < GPU_MAX_POINTS; i++) {
                    gpu_infos[0].usage_history[i] = 0.0;
                    gpu_infos[0].vram_history[i] = 0.0;
                }
                gpu_infos[0].history_index = 0;
                
                gpu_count = 1;
            }
            pclose(fp);
        }
    }
    
    // Fallback to lspci if no GPUs found
    if (gpu_count == 0) {
        FILE *fp = popen("lspci -nn | grep -i ' vga ' | head -n1", "r");
        if (fp) {
            if (fgets(gpu_infos[0].name, sizeof(gpu_infos[0].name), fp)) {
                trim_newline(gpu_infos[0].name);
                shorten_gpu_name(gpu_infos[0].name);
                
                // Determine vendor
                if (strstr(gpu_infos[0].name, "NVIDIA")) {
                    strcpy(gpu_infos[0].vendor, "NVIDIA");
                } else if (strstr(gpu_infos[0].name, "AMD") || strstr(gpu_infos[0].name, "Radeon")) {
                    strcpy(gpu_infos[0].vendor, "AMD");
                } else if (strstr(gpu_infos[0].name, "Intel")) {
                    strcpy(gpu_infos[0].vendor, "Intel");
                    // Get better Intel GPU info
                    get_intel_gpu_info(&gpu_infos[0]);
                } else {
                    strcpy(gpu_infos[0].vendor, "Unknown");
                }
                
                strcpy(gpu_infos[0].driver_version, "-");
                gpu_infos[0].gpu_id = 0;
                
                // Init history
                for (int i = 0; i < GPU_MAX_POINTS; i++) {
                    gpu_infos[0].usage_history[i] = 0.0;
                    gpu_infos[0].vram_history[i] = 0.0;
                }
                gpu_infos[0].history_index = 0;
                
                gpu_count = 1;
            }
            pclose(fp);
        }
    }
    
    // Final fallback - create a dummy GPU if none found
    if (gpu_count == 0) {
        strcpy(gpu_infos[0].name, "Unknown GPU");
        strcpy(gpu_infos[0].vendor, "Unknown");
        strcpy(gpu_infos[0].driver_version, "-");
        gpu_infos[0].gpu_id = 0;
        
        // Init history
        for (int i = 0; i < GPU_MAX_POINTS; i++) {
            gpu_infos[0].usage_history[i] = 0.0;
            gpu_infos[0].vram_history[i] = 0.0;
        }
        gpu_infos[0].history_index = 0;
        
        gpu_count = 1;
    }
    
    // Apply name shortening for better display
    for (int i = 0; i < gpu_count; i++) {
        shorten_gpu_name(gpu_infos[i].name);
    }
}

void gpu_data_cleanup(void) {
    // Reset GPU info array
    for (int i = 0; i < MAX_GPUS; i++) {
        memset(&gpu_infos[i], 0, sizeof(GPUInfo));
    }
    gpu_count = 0;
}

static void update_from_amd_sysfs(GPUInfo *gpu, int card_index) {
    char path[256];
    
    // Usage percent
    snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", card_index);
    gpu->usage_percent = read_double_from_file(path, 1.0);
    
    // VRAM bytes
    snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/mem_info_vram_used", card_index);
    guint64 vram_used_kb = (guint64)read_double_from_file(path, 1024.0);
    
    snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/mem_info_vram_total", card_index);
    guint64 vram_total_kb = (guint64)read_double_from_file(path, 1024.0);
    
    gpu->vram_used_mb = vram_used_kb / 1024.0;
    gpu->vram_total_mb = vram_total_kb / 1024.0;
    if (gpu->vram_total_mb > 0)
        gpu->vram_usage_percent = 100.0 * gpu->vram_used_mb / gpu->vram_total_mb;
    else
        gpu->vram_usage_percent = 0.0;
}

static void update_from_nvidia_smi(GPUInfo *gpu) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "nvidia-smi --query-gpu=index,utilization.gpu,memory.total,memory.used --format=csv,noheader,nounits --id=%d 2>/dev/null", gpu->gpu_id);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    
    int index, util, mem_total, mem_used;
    if (fscanf(fp, "%d, %d, %d, %d", &index, &util, &mem_total, &mem_used) == 4) {
        gpu->usage_percent = util;
        gpu->vram_total_mb = mem_total;
        gpu->vram_used_mb = mem_used;
        if (mem_total > 0) gpu->vram_usage_percent = 100.0 * mem_used / mem_total;
        else gpu->vram_usage_percent = 0.0;
    }
    pclose(fp);
}

static void update_from_intel_gpu_top(GPUInfo *gpu) {
    // Try to get Intel GPU usage from intel_gpu_top if available
    FILE *fp = popen("command -v intel_gpu_top >/dev/null && intel_gpu_top -J -s 100 2>/dev/null", "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            // Look for the line with GPU usage data
            if (strstr(line, "\"engines\"")) {
                // Parse the JSON-like output
                char *render = strstr(line, "\"render\"");
                if (render) {
                    char *busy = strstr(render, "\"busy\"");
                    if (busy) {
                        busy += 7; // Skip "\"busy\":"
                        gpu->usage_percent = strtod(busy, NULL);
                    }
                }
                break;
            }
        }
        pclose(fp);
    }
    
    // Try to get memory info
    FILE *fp_mem = popen("free -m | grep Mem:", "r");
    if (fp_mem) {
        char line[256];
        if (fgets(line, sizeof(line), fp_mem)) {
            // Parse the memory info
            unsigned long total, used;
            if (sscanf(line, "Mem: %lu %lu", &total, &used) == 2) {
                // For integrated GPUs, use system memory as an approximation
                gpu->vram_total_mb = total;
                gpu->vram_used_mb = used;
                if (total > 0) {
                    gpu->vram_usage_percent = 100.0 * used / total;
                } else {
                    gpu->vram_usage_percent = 0.0;
                }
            }
        }
        pclose(fp_mem);
    }
    
    // If we couldn't get VRAM info, try to estimate from /proc/meminfo
    if (gpu->vram_total_mb == 0) {
        FILE *fp_meminfo = fopen("/proc/meminfo", "r");
        if (fp_meminfo) {
            char line[256];
            unsigned long total = 0, available = 0;
            
            while (fgets(line, sizeof(line), fp_meminfo)) {
                if (strncmp(line, "MemTotal:", 9) == 0) {
                    sscanf(line, "MemTotal: %lu", &total);
                } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                    sscanf(line, "MemAvailable: %lu", &available);
                }
            }
            fclose(fp_meminfo);
            
            if (total > 0) {
                gpu->vram_total_mb = total / 1024;
                gpu->vram_used_mb = (total - available) / 1024;
                gpu->vram_usage_percent = 100.0 * gpu->vram_used_mb / gpu->vram_total_mb;
            }
        }
    }
}

void gpu_data_update(void) {
    for (int i = 0; i < gpu_count; i++) {
        GPUInfo *gpu = &gpu_infos[i];
        
        // Choose update method based on vendor
        if (strcmp(gpu->vendor, "NVIDIA") == 0) {
            update_from_nvidia_smi(gpu);
        } else if (strcmp(gpu->vendor, "AMD") == 0) {
            update_from_amd_sysfs(gpu, gpu->gpu_id);
        } else if (strcmp(gpu->vendor, "Intel") == 0) {
            update_from_intel_gpu_top(gpu);
        } else {
            // For other vendors, just set some dummy values
            gpu->usage_percent = 0.0;
            gpu->vram_used_mb = 0.0;
            gpu->vram_total_mb = 0.0;
            gpu->vram_usage_percent = 0.0;
        }
        
        // Clamp values
        if (gpu->usage_percent < 0) gpu->usage_percent = 0;
        if (gpu->usage_percent > 100) gpu->usage_percent = 100;
        if (gpu->vram_usage_percent < 0) gpu->vram_usage_percent = 0;
        if (gpu->vram_usage_percent > 100) gpu->vram_usage_percent = 100;
        
        // Update history
        gpu->usage_history[gpu->history_index] = gpu->usage_percent;
        gpu->vram_history[gpu->history_index] = gpu->vram_usage_percent;
        gpu->history_index = (gpu->history_index + 1) % GPU_MAX_POINTS;
    }
}

// Accessors for the primary GPU (index 0)
const gchar* gpu_get_name(void) { return gpu_count > 0 ? gpu_infos[0].name : "Unknown GPU"; }
const gchar* gpu_get_vendor(void) { return gpu_count > 0 ? gpu_infos[0].vendor : "Unknown"; }
const gchar* gpu_get_driver_version(void) { return gpu_count > 0 ? gpu_infos[0].driver_version : "-"; }

gdouble gpu_get_usage(void) { return gpu_count > 0 ? gpu_infos[0].usage_percent : 0.0; }
gdouble gpu_get_vram_used(void) { return gpu_count > 0 ? gpu_infos[0].vram_used_mb : 0.0; }
gdouble gpu_get_vram_total(void) { return gpu_count > 0 ? gpu_infos[0].vram_total_mb : 0.0; }
gdouble gpu_get_vram_usage_percent(void) { return gpu_count > 0 ? gpu_infos[0].vram_usage_percent : 0.0; }

const gdouble* gpu_get_usage_history(void) { return gpu_count > 0 ? gpu_infos[0].usage_history : NULL; }
const gdouble* gpu_get_vram_history(void) { return gpu_count > 0 ? gpu_infos[0].vram_history : NULL; }

gint gpu_get_history_index(void) { return gpu_count > 0 ? gpu_infos[0].history_index : 0; }

// Functions for multiple GPUs
gint gpu_get_count(void) { return gpu_count; }
const GPUInfo* gpu_get_info(gint index) { return (index >= 0 && index < gpu_count) ? &gpu_infos[index] : NULL; } 
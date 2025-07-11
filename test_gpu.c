#include <stdio.h>
#include <stdlib.h>
#include "include/gpu/gpu_data.h"

int main() {
    printf("Initializing GPU data...\n");
    gpu_data_init();
    
    int count = gpu_get_count();
    printf("Found %d GPUs\n", count);
    
    for (int i = 0; i < count; i++) {
        const GPUInfo *info = gpu_get_info(i);
        if (info) {
            printf("GPU %d: %s\n", i, info->name);
            printf("  Vendor: %s\n", info->vendor);
            printf("  Driver: %s\n", info->driver_version);
        }
    }
    
    printf("Updating GPU data...\n");
    gpu_data_update();
    
    for (int i = 0; i < count; i++) {
        const GPUInfo *info = gpu_get_info(i);
        if (info) {
            printf("GPU %d: %s\n", i, info->name);
            printf("  Usage: %.1f%%\n", info->usage_percent);
            printf("  VRAM: %.0f / %.0f MB (%.1f%%)\n", 
                   info->vram_used_mb, info->vram_total_mb, info->vram_usage_percent);
        }
    }
    
    printf("Cleaning up GPU data...\n");
    gpu_data_cleanup();
    
    return 0;
} 
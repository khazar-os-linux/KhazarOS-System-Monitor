#ifndef MEMORY_DATA_H
#define MEMORY_DATA_H

#include <glib.h>

#define MAX_POINTS 60 

// Initialization and Cleanup
void memory_data_init(void);
void memory_data_cleanup(void);

// Update function
void memory_data_update(void);

// Getter functions for memory specs
gulong get_total_memory(void);
gulong get_used_memory(void);
gulong get_free_memory(void);
gulong get_available_memory(void);
gulong get_buffer_memory(void);
gulong get_cached_memory(void);
gulong get_swap_total(void);
gulong get_swap_used(void);
gulong get_swap_free(void);

// Getter functions for memory usage history
gdouble get_current_memory_usage_percent(void);
const gdouble* get_memory_usage_history(void);
gint get_memory_usage_history_index(void);

// Getter functions for swap usage history
gdouble get_current_swap_usage_percent(void);
const gdouble* get_swap_usage_history(void);
gint get_swap_usage_history_index(void);

#endif // MEMORY_DATA_H 
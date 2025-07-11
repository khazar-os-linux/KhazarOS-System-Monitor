#ifndef NETWORK_DATA_H
#define NETWORK_DATA_H

#include <glib.h>

#define MAX_POINTS 60
#define MAX_INTERFACES 8

typedef struct {
    gchar interface_name[64];
    gchar interface_type[32];  // wifi, ethernet, etc.
    gboolean is_active;
    gchar ip_address[64];
    
    // Network traffic data
    guint64 prev_rx_bytes;
    guint64 prev_tx_bytes;
    guint64 current_rx_bytes;
    guint64 current_tx_bytes;
    
    // Speed in KB/s
    gdouble rx_speed;
    gdouble tx_speed;
    
    // Adapter specifications
    gchar mac_address[32];   // MAC address as string
    gint  mtu;               // MTU value
    gint  link_speed_mbps;   // Reported link speed (Mbps), -1 if unavailable
    
    // History for graphs
    gdouble rx_history[MAX_POINTS];
    gdouble tx_history[MAX_POINTS];
    gint history_index;
} NetworkInfo;

// Initialization and Cleanup
void network_data_init(void);
void network_data_cleanup(void);

// Update function
void network_data_update(void);

// Getter functions
gint get_interface_count(void);
const NetworkInfo* get_interface_info(gint index);
const gchar* get_interface_type(gint index);

// Getter functions for network traffic history
gdouble get_current_rx_speed(gint index);
gdouble get_current_tx_speed(gint index);
const gdouble* get_rx_history(gint index);
const gdouble* get_tx_history(gint index);
gint get_history_index(gint index);

// Getter functions for adapter specifications
const gchar* get_mac_address(gint index);
gint get_mtu(gint index);
gint get_link_speed(gint index);

#endif // NETWORK_DATA_H 
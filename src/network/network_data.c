#include "network/network_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

// Global array to store network interface information
static NetworkInfo interfaces[MAX_INTERFACES];
static gint interface_count = 0;

// Function to check if an interface is a physical interface (not loopback, etc.)
static gboolean is_physical_interface(const gchar *name) {
    // Skip loopback and virtual interfaces
    if (strcmp(name, "lo") == 0 || 
        strncmp(name, "veth", 4) == 0 || 
        strncmp(name, "docker", 6) == 0 ||
        strncmp(name, "br-", 3) == 0 ||
        strncmp(name, "virbr", 5) == 0) {
        return FALSE;
    }
    return TRUE;
}

// Function to determine interface type (wifi, ethernet, etc.)
static void determine_interface_type(NetworkInfo *info) {
    // Default to "Unknown"
    strcpy(info->interface_type, "Unknown");
    
    // Check for common naming patterns
    if (strncmp(info->interface_name, "wlan", 4) == 0 ||
        strncmp(info->interface_name, "wlp", 3) == 0 ||
        strncmp(info->interface_name, "wifi", 4) == 0) {
        strcpy(info->interface_type, "Wi-Fi");
    } else if (strncmp(info->interface_name, "eth", 3) == 0 ||
               strncmp(info->interface_name, "enp", 3) == 0 ||
               strncmp(info->interface_name, "eno", 3) == 0) {
        strcpy(info->interface_type, "Ethernet");
    } else if (strncmp(info->interface_name, "usb", 3) == 0) {
        strcpy(info->interface_type, "USB");
    } else if (strncmp(info->interface_name, "ppp", 3) == 0) {
        strcpy(info->interface_type, "PPP");
    } else if (strncmp(info->interface_name, "tun", 3) == 0) {
        strcpy(info->interface_type, "VPN Tunnel");
    }
    
    // Try to get more info using external commands
    char cmd[128];
    char output[256] = {0};
    
    // Check if it's a wireless interface using iw
    snprintf(cmd, sizeof(cmd), "iw dev %s info 2>/dev/null | grep -q 'type managed'", info->interface_name);
    if (system(cmd) == 0) {
        strcpy(info->interface_type, "Wi-Fi");
    }
}

// Function to get IP address for an interface
static void get_ip_address(NetworkInfo *info) {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    strcpy(info->ip_address, "Not connected");
    
    if (getifaddrs(&ifaddr) == -1) {
        return;
    }
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
            
        family = ifa->ifa_addr->sa_family;
        
        // Check if it's the interface we're looking for and it's IPv4
        if (strcmp(ifa->ifa_name, info->interface_name) == 0 && family == AF_INET) {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                           host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s == 0) {
                strcpy(info->ip_address, host);
                info->is_active = TRUE;
            }
            break;
        }
    }
    
    freeifaddrs(ifaddr);
}

void network_data_init(void) {
    g_print("Initializing network data\n");
    
    // Reset interface count
    interface_count = 0;
    
    // Initialize network info structures
    for (gint i = 0; i < MAX_INTERFACES; i++) {
        memset(&interfaces[i], 0, sizeof(NetworkInfo));
        interfaces[i].history_index = 0;
        for (gint j = 0; j < MAX_POINTS; j++) {
            interfaces[i].rx_history[j] = 0.0;
            interfaces[i].tx_history[j] = 0.0;
        }
    }
    
    // We'll update network data after initialization
    // Don't call network_data_update() here to avoid double initialization
    
    g_print("Network data initialization complete\n");
}

void network_data_cleanup(void) {
    g_print("Cleaning up network data\n");
    
    // Reset interface count and clear interface data
    for (gint i = 0; i < MAX_INTERFACES; i++) {
        memset(&interfaces[i], 0, sizeof(NetworkInfo));
    }
    interface_count = 0;
}

void network_data_update(void) {
    g_print("Updating network data\n");
    
    // Open /proc/net/dev to read network interface statistics
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        g_print("Failed to open /proc/net/dev\n");
        return;
    }
    
    // Skip the first two header lines
    char line[512];
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);
    
    // Reset interface count
    interface_count = 0;
    
    // Read each interface line
    while (fgets(line, sizeof(line), fp) && interface_count < MAX_INTERFACES) {
        char *name_end = strchr(line, ':');
        if (!name_end) continue;
        
        // Extract interface name
        *name_end = '\0';
        char *name = line;
        while (*name == ' ') name++; // Skip leading spaces
        
        // Skip non-physical interfaces
        if (!is_physical_interface(name)) continue;
        
        // Parse the line
        guint64 rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        guint64 tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
        
        sscanf(name_end + 1, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
               &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
               &tx_bytes, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed);
        
        // Store the previous values
        interfaces[interface_count].prev_rx_bytes = interfaces[interface_count].current_rx_bytes;
        interfaces[interface_count].prev_tx_bytes = interfaces[interface_count].current_tx_bytes;
        
        // Update current values
        strncpy(interfaces[interface_count].interface_name, name, sizeof(interfaces[interface_count].interface_name) - 1);
        interfaces[interface_count].current_rx_bytes = rx_bytes;
        interfaces[interface_count].current_tx_bytes = tx_bytes;
        
        // Calculate speeds in KB/s
        if (interfaces[interface_count].prev_rx_bytes > 0 || interfaces[interface_count].prev_tx_bytes > 0) {
            guint64 rx_delta = 0;
            guint64 tx_delta = 0;
            
            if (interfaces[interface_count].current_rx_bytes > interfaces[interface_count].prev_rx_bytes) {
                rx_delta = interfaces[interface_count].current_rx_bytes - interfaces[interface_count].prev_rx_bytes;
            }
            
            if (interfaces[interface_count].current_tx_bytes > interfaces[interface_count].prev_tx_bytes) {
                tx_delta = interfaces[interface_count].current_tx_bytes - interfaces[interface_count].prev_tx_bytes;
            }
            
            // Convert to KB/s (assuming 2 second update interval)
            interfaces[interface_count].rx_speed = rx_delta / 1024.0 / 2.0;
            interfaces[interface_count].tx_speed = tx_delta / 1024.0 / 2.0;
            
            // Update history
            interfaces[interface_count].rx_history[interfaces[interface_count].history_index] = interfaces[interface_count].rx_speed;
            interfaces[interface_count].tx_history[interfaces[interface_count].history_index] = interfaces[interface_count].tx_speed;
            interfaces[interface_count].history_index = (interfaces[interface_count].history_index + 1) % MAX_POINTS;
        }
        
        // Gather adapter specifications (MAC, MTU, link speed)
        {
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd >= 0) {
                struct ifreq ifr;
                memset(&ifr, 0, sizeof(struct ifreq));
                strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

                // MAC address
                if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) == 0) {
                    unsigned char *hwaddr = (unsigned char *)ifr.ifr_hwaddr.sa_data;
                    snprintf(interfaces[interface_count].mac_address,
                             sizeof(interfaces[interface_count].mac_address),
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
                } else {
                    strcpy(interfaces[interface_count].mac_address, "N/A");
                }

                // MTU
                if (ioctl(sockfd, SIOCGIFMTU, &ifr) == 0) {
                    interfaces[interface_count].mtu = ifr.ifr_mtu;
                } else {
                    interfaces[interface_count].mtu = 0;
                }
                close(sockfd);
            } else {
                strcpy(interfaces[interface_count].mac_address, "N/A");
                interfaces[interface_count].mtu = 0;
            }
            // Link speed via /sys, default -1 (unknown)
            interfaces[interface_count].link_speed_mbps = -1;
            char speed_path[128];
            snprintf(speed_path, sizeof(speed_path), "/sys/class/net/%s/speed", name);
            FILE *speed_fp = fopen(speed_path, "r");
            if (speed_fp) {
                int spd;
                if (fscanf(speed_fp, "%d", &spd) == 1) {
                    interfaces[interface_count].link_speed_mbps = spd;
                }
                fclose(speed_fp);
            }
        }
        
        // Determine interface type
        determine_interface_type(&interfaces[interface_count]);
        
        // Get IP address
        get_ip_address(&interfaces[interface_count]);
        
        g_print("Found network interface: %s (%s), RX: %.2f KB/s, TX: %.2f KB/s\n", 
                interfaces[interface_count].interface_name,
                interfaces[interface_count].interface_type,
                interfaces[interface_count].rx_speed,
                interfaces[interface_count].tx_speed);
        
        interface_count++;
    }
    
    fclose(fp);
    g_print("Network data update complete. Found %d interfaces.\n", interface_count);
}

gint get_interface_count(void) {
    return interface_count;
}

const NetworkInfo* get_interface_info(gint index) {
    if (index >= 0 && index < interface_count) {
        return &interfaces[index];
    }
    return NULL;
}

const gchar* get_interface_type(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].interface_type;
    }
    return "Unknown";
}

gdouble get_current_rx_speed(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].rx_speed;
    }
    return 0.0;
}

gdouble get_current_tx_speed(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].tx_speed;
    }
    return 0.0;
}

const gdouble* get_rx_history(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].rx_history;
    }
    return NULL;
}

const gdouble* get_tx_history(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].tx_history;
    }
    return NULL;
}

gint get_history_index(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].history_index;
    }
    return 0;
}

const gchar* get_mac_address(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].mac_address;
    }
    return "N/A";
}

gint get_mtu(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].mtu;
    }
    return 0;
}

gint get_link_speed(gint index) {
    if (index >= 0 && index < interface_count) {
        return interfaces[index].link_speed_mbps;
    }
    return -1;
}

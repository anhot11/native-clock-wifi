#ifndef WIFI_STATS_H
#define WIFI_STATS_H

#include <jni.h>
#include <stdint.h>
#include <stdbool.h>

#define HISTORY_LEN 40
#define MAX_NET_IFACES 6

typedef struct {
    char name[16];      // e.g. "wlan0", "rmnet0", "tun0"
    char ip[48];        // e.g. "192.168.7.5"
    bool is_up;
    bool is_loopback;
} NetInterfaceInfo;

typedef struct {
    bool is_connected;
    char ssid[64];
    char bssid[32];
    int link_speed_mbps;       // Current link speed in Mbps
    int tx_link_speed_mbps;    // Tx link speed in Mbps (API 29+)
    int rx_link_speed_mbps;    // Rx link speed in Mbps (API 29+)
    int max_tx_speed_mbps;     // Max supported Tx speed (API 30+)
    int max_rx_speed_mbps;     // Max supported Rx speed (API 30+)
    int rssi_dbm;              // Signal strength in dBm (-100 to -30)
    int signal_level;          // 0 to 4 bars
    int frequency_mhz;         // e.g., 2412, 5180
    char standard_str[32];     // Wi-Fi 4, 5, 6, 7 / 802.11
    
    // Real-time throughput (computed via TrafficStats)
    double rx_rate_kbps;       // Download rate
    double tx_rate_kbps;       // Upload rate
    uint64_t total_rx_bytes;
    uint64_t total_tx_bytes;
    
    // History buffer for sparkline graph
    float rx_history[HISTORY_LEN];
    float tx_history[HISTORY_LEN];
    int history_idx;
    
    // Android Connections & Network Diagnostics
    char conn_type[32];        // "Wi-Fi Activo", "Datos Moviles", "VPN", etc.
    char local_ipv4[32];       // e.g. "192.168.7.5"
    char gateway_ip[32];       // e.g. "192.168.7.1"
    char netmask[32];          // e.g. "255.255.255.0"
    char dns1[32];             // e.g. "8.8.8.8"
    char dns2[32];             // e.g. "8.8.4.4"
    bool has_internet;
    
    // Active interfaces list
    NetInterfaceInfo interfaces[MAX_NET_IFACES];
    int iface_count;
    
    // System & Performance Metrics
    int ping_ms;               // Real-time network latency (ms)
    float ram_used_gb;         // Used RAM in GB
    float ram_total_gb;        // Total RAM in GB
    int ram_percent;           // RAM usage percentage (0-100%)
    int battery_level;         // Battery percentage (0-100%)
    bool is_charging;          // True if charger connected
    int link_quality_score;    // Wi-Fi Quality index (0-100%)

    // Timing
    double last_update_sec;
} WifiStats;

void wifi_stats_init(WifiStats* stats);
void wifi_stats_update(JNIEnv* env, jobject context, WifiStats* stats);

#endif // WIFI_STATS_H

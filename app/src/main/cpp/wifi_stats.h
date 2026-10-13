#ifndef WIFI_STATS_H
#define WIFI_STATS_H

#include <jni.h>
#include <stdint.h>
#include <stdbool.h>

#define HISTORY_LEN 40

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
    
    // Timing
    double last_update_sec;
} WifiStats;

void wifi_stats_init(WifiStats* stats);
void wifi_stats_update(JNIEnv* env, jobject context, WifiStats* stats);

#endif // WIFI_STATS_H

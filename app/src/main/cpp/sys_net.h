#ifndef SYS_NET_H
#define SYS_NET_H

#include <stdint.h>
#include <stdbool.h>

#define NET_HISTORY_LEN 60
#define NUM_PING_TARGETS 3
#define MAX_NET_IFACES 6

typedef enum {
    NET_QUALITY_GOOD = 0,   // Green (< 50ms, 0% loss)
    NET_QUALITY_FAIR = 1,   // Amber (50..150ms, < 10% loss)
    NET_QUALITY_POOR = 2    // Red (> 150ms or packet loss)
} NetQuality;

typedef struct {
    char name[32];          // "Gateway", "1.1.1.1", "8.8.8.8"
    char ip[32];
    int port;
    int last_ping_ms;       // Current ping in ms (-1 if unreachable)
    int pings[10];          // Last 10 samples for jitter
    int ping_count;
    float jitter_ms;        // Standard deviation of ping
    float packet_loss;      // 0.0 to 100.0%
    int sent_count;
    int success_count;
    NetQuality quality;
} PingTarget;

typedef struct {
    char name[16];          // "wlan0", "rmnet0", "tun0"
    char ip[48];            // "192.168.1.100"
    bool is_up;
    bool is_loopback;
} NetInterface;

typedef struct {
    // Live Bandwidth (KB/s and MB/s)
    float rx_rate_kbps;
    float tx_rate_kbps;
    float rx_peak_kbps;
    float tx_peak_kbps;
    
    // Session Accumulator
    uint64_t session_rx_bytes;
    uint64_t session_tx_bytes;

    // Wi-Fi Link Details
    bool is_wifi_connected;
    char ssid[64];
    int link_speed_mbps;
    int signal_level;       // 0 to 4 bars
    int rssi_dbm;           // -100 to -30
    int frequency_mhz;      // 2412, 5180, etc.
    char standard_str[32];  // "Wi-Fi 6 (802.11ax)"

    // Network Diagnostics
    char local_ip[32];
    char gateway_ip[32];
    char netmask[32];
    char dns_primary[32];

    // Interfaces
    NetInterface ifaces[MAX_NET_IFACES];
    int iface_count;

    // Latency Targets (Gateway, 1.1.1.1, 8.8.8.8)
    PingTarget ping_targets[NUM_PING_TARGETS];

    // Rolling 60s Bandwidth History
    float rx_history[NET_HISTORY_LEN];
    float tx_history[NET_HISTORY_LEN];
    int history_idx;
} NetTelemetry;

void sys_net_init(NetTelemetry* net);
void sys_net_update(NetTelemetry* net, void* jni_env_ptr, void* jni_context_ptr);

#endif // SYS_NET_H

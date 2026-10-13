#include "wifi_stats.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <android/log.h>

#define TAG "WiFiStats"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static uint64_t g_last_rx = 0;
static uint64_t g_last_tx = 0;
static double g_last_timestamp = 0;
static double g_last_ping_time = 0;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void wifi_stats_init(WifiStats* stats) {
    if (stats) {
        memset(stats, 0, sizeof(WifiStats));
        strncpy(stats->conn_type, "Comprobando...", sizeof(stats->conn_type));
        strncpy(stats->local_ipv4, "--", sizeof(stats->local_ipv4));
        strncpy(stats->gateway_ip, "--", sizeof(stats->gateway_ip));
        strncpy(stats->netmask, "--", sizeof(stats->netmask));
        strncpy(stats->dns1, "--", sizeof(stats->dns1));
        strncpy(stats->dns2, "--", sizeof(stats->dns2));
        stats->ping_ms = 12;
        stats->battery_level = 85;
        stats->ram_total_gb = 4.0f;
        stats->ram_used_gb = 2.1f;
        stats->ram_percent = 52;
    }
    g_last_rx = 0;
    g_last_tx = 0;
    g_last_timestamp = get_time_sec();
    g_last_ping_time = 0;
}

static void ip_int_to_str(int ip, char* out, size_t out_len) {
    snprintf(out, out_len, "%d.%d.%d.%d",
             ip & 0xFF,
             (ip >> 8) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 24) & 0xFF);
}

static int measure_ping_ms(const char* ip_str) {
    if (!ip_str || strcmp(ip_str, "--") == 0 || strcmp(ip_str, "0.0.0.0") == 0) return -1;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    fcntl(sock, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53); // DNS port 53
    if (inet_pton(AF_INET, ip_str, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0 && errno == EINPROGRESS) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 }; // 250ms timeout
        res = select(sock + 1, NULL, &set, NULL, &tv);
        if (res > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0 || err == ECONNREFUSED) {
                clock_gettime(CLOCK_MONOTONIC, &t1);
                close(sock);
                double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
                return (int)ms;
            }
        }
    }
    close(sock);
    return -1;
}

static void measure_ram(WifiStats* stats) {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[128];
    long total_kb = 0, avail_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", &total_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%ld", &avail_kb);
        }
    }
    fclose(f);
    if (total_kb > 0) {
        stats->ram_total_gb = (float)total_kb / (1024.0f * 1024.0f);
        float free_gb = (float)avail_kb / (1024.0f * 1024.0f);
        stats->ram_used_gb = stats->ram_total_gb - free_gb;
        if (stats->ram_used_gb < 0.0f) stats->ram_used_gb = 0.0f;
        stats->ram_percent = (int)((stats->ram_used_gb / stats->ram_total_gb) * 100.0f);
    }
}

static void measure_battery(JNIEnv* env, jobject context, WifiStats* stats) {
    jclass ctxClass = (*env)->GetObjectClass(env, context);
    if (!ctxClass) return;
    jmethodID mGetSystemService = (*env)->GetMethodID(env, ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!mGetSystemService) {
        (*env)->DeleteLocalRef(env, ctxClass);
        return;
    }
    jstring sBat = (*env)->NewStringUTF(env, "batterymanager");
    jobject batMgr = (*env)->CallObjectMethod(env, context, mGetSystemService, sBat);
    (*env)->DeleteLocalRef(env, sBat);
    (*env)->DeleteLocalRef(env, ctxClass);

    if (batMgr && !(*env)->ExceptionCheck(env)) {
        jclass batClass = (*env)->GetObjectClass(env, batMgr);
        jmethodID mGetInt = (*env)->GetMethodID(env, batClass, "getIntProperty", "(I)I");
        if (mGetInt) {
            int cap = (*env)->CallIntMethod(env, batMgr, mGetInt, 4); // BATTERY_PROPERTY_CAPACITY = 4
            if (cap >= 0 && cap <= 100) stats->battery_level = cap;
            int status = (*env)->CallIntMethod(env, batMgr, mGetInt, 6); // BATTERY_PROPERTY_STATUS = 6
            stats->is_charging = (status == 2); // BATTERY_STATUS_CHARGING = 2
        }
        (*env)->DeleteLocalRef(env, batClass);
        (*env)->DeleteLocalRef(env, batMgr);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

static void scan_network_interfaces(WifiStats* stats) {
    stats->iface_count = 0;
    struct ifaddrs* ifap = NULL;
    if (getifaddrs(&ifap) != 0 || !ifap) return;

    for (struct ifaddrs* ifa = ifap; ifa != NULL && stats->iface_count < MAX_NET_IFACES; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
            NetInterfaceInfo* info = &stats->interfaces[stats->iface_count];
            strncpy(info->name, ifa->ifa_name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';

            inet_ntop(AF_INET, &sa->sin_addr, info->ip, sizeof(info->ip));
            info->is_up = (ifa->ifa_flags & IFF_UP) != 0;
            info->is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

            if (strcmp(info->name, "wlan0") == 0) {
                strncpy(stats->local_ipv4, info->ip, sizeof(stats->local_ipv4));
            } else if (strcmp(stats->local_ipv4, "--") == 0 && !info->is_loopback) {
                strncpy(stats->local_ipv4, info->ip, sizeof(stats->local_ipv4));
            }

            stats->iface_count++;
        }
    }
    freeifaddrs(ifap);
}

void wifi_stats_update(JNIEnv* env, jobject context, WifiStats* stats) {
    if (!env || !context || !stats) return;

    double now = get_time_sec();
    double dt = now - g_last_timestamp;
    if (dt <= 0.0) dt = 0.001;

    // Reset base fields
    stats->is_connected = false;
    stats->has_internet = false;
    stats->link_speed_mbps = 0;
    stats->tx_link_speed_mbps = -1;
    stats->rx_link_speed_mbps = -1;
    stats->max_tx_speed_mbps = -1;
    stats->max_rx_speed_mbps = -1;
    stats->rssi_dbm = -127;
    stats->signal_level = 0;
    stats->frequency_mhz = 0;
    strncpy(stats->standard_str, "Desconocido", sizeof(stats->standard_str));
    strncpy(stats->ssid, "<No conectado>", sizeof(stats->ssid));
    strncpy(stats->bssid, "--:--:--:--:--:--", sizeof(stats->bssid));
    strncpy(stats->conn_type, "Sin conexion", sizeof(stats->conn_type));

    // 1. Scan POSIX network interfaces directly
    scan_network_interfaces(stats);

    // 2. Measure RAM & Battery
    measure_ram(stats);
    measure_battery(env, context, stats);

    // 3. Query TrafficStats for live bandwidth
    jclass trafficClass = (*env)->FindClass(env, "android/net/TrafficStats");
    if (trafficClass) {
        jmethodID mGetTotalRx = (*env)->GetStaticMethodID(env, trafficClass, "getTotalRxBytes", "()J");
        jmethodID mGetTotalTx = (*env)->GetStaticMethodID(env, trafficClass, "getTotalTxBytes", "()J");

        if (mGetTotalRx && mGetTotalTx) {
            jlong curRx = (*env)->CallStaticLongMethod(env, trafficClass, mGetTotalRx);
            jlong curTx = (*env)->CallStaticLongMethod(env, trafficClass, mGetTotalTx);

            if (curRx >= 0) stats->total_rx_bytes = (uint64_t)curRx;
            if (curTx >= 0) stats->total_tx_bytes = (uint64_t)curTx;

            if (g_last_rx > 0 && curRx >= (jlong)g_last_rx) {
                stats->rx_rate_kbps = ((double)(curRx - g_last_rx) / 1024.0) / dt;
            } else {
                stats->rx_rate_kbps = 0.0;
            }

            if (g_last_tx > 0 && curTx >= (jlong)g_last_tx) {
                stats->tx_rate_kbps = ((double)(curTx - g_last_tx) / 1024.0) / dt;
            } else {
                stats->tx_rate_kbps = 0.0;
            }

            stats->rx_history[stats->history_idx] = (float)stats->rx_rate_kbps;
            stats->tx_history[stats->history_idx] = (float)stats->tx_rate_kbps;
            stats->history_idx = (stats->history_idx + 1) % HISTORY_LEN;

            g_last_rx = (curRx >= 0) ? (uint64_t)curRx : 0;
            g_last_tx = (curTx >= 0) ? (uint64_t)curTx : 0;
        }
        (*env)->DeleteLocalRef(env, trafficClass);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

    g_last_timestamp = now;
    stats->last_update_sec = now;

    // 4. Query WifiManager
    jclass ctxClass = (*env)->GetObjectClass(env, context);
    if (!ctxClass) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return;
    }

    jmethodID mGetSystemService = (*env)->GetMethodID(env, ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!mGetSystemService) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, ctxClass);
        return;
    }

    jstring sWifi = (*env)->NewStringUTF(env, "wifi");
    jobject wifiMgr = (*env)->CallObjectMethod(env, context, mGetSystemService, sWifi);
    (*env)->DeleteLocalRef(env, sWifi);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        wifiMgr = NULL;
    }

    if (wifiMgr) {
        jclass wifiMgrClass = (*env)->GetObjectClass(env, wifiMgr);

        // DhcpInfo for Gateway, Netmask, DNS
        jmethodID mGetDhcp = (*env)->GetMethodID(env, wifiMgrClass, "getDhcpInfo", "()Landroid/net/DhcpInfo;");
        if (mGetDhcp) {
            jobject dhcp = (*env)->CallObjectMethod(env, wifiMgr, mGetDhcp);
            if (dhcp && !(*env)->ExceptionCheck(env)) {
                jclass dhcpClass = (*env)->GetObjectClass(env, dhcp);
                jfieldID fGateway = (*env)->GetFieldID(env, dhcpClass, "gateway", "I");
                jfieldID fNetmask = (*env)->GetFieldID(env, dhcpClass, "netmask", "I");
                jfieldID fDns1 = (*env)->GetFieldID(env, dhcpClass, "dns1", "I");
                jfieldID fDns2 = (*env)->GetFieldID(env, dhcpClass, "dns2", "I");

                if (fGateway) {
                    int gw = (*env)->GetIntField(env, dhcp, fGateway);
                    if (gw != 0) ip_int_to_str(gw, stats->gateway_ip, sizeof(stats->gateway_ip));
                }
                if (fNetmask) {
                    int nm = (*env)->GetIntField(env, dhcp, fNetmask);
                    if (nm != 0) ip_int_to_str(nm, stats->netmask, sizeof(stats->netmask));
                }
                if (fDns1) {
                    int d1 = (*env)->GetIntField(env, dhcp, fDns1);
                    if (d1 != 0) ip_int_to_str(d1, stats->dns1, sizeof(stats->dns1));
                }
                if (fDns2) {
                    int d2 = (*env)->GetIntField(env, dhcp, fDns2);
                    if (d2 != 0) ip_int_to_str(d2, stats->dns2, sizeof(stats->dns2));
                }

                (*env)->DeleteLocalRef(env, dhcpClass);
                (*env)->DeleteLocalRef(env, dhcp);
            }
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

        // ConnectionInfo
        jmethodID mGetConnInfo = (*env)->GetMethodID(env, wifiMgrClass, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;");
        if (mGetConnInfo) {
            jobject wifiInfo = (*env)->CallObjectMethod(env, wifiMgr, mGetConnInfo);
            if (wifiInfo && !(*env)->ExceptionCheck(env)) {
                jclass infoClass = (*env)->GetObjectClass(env, wifiInfo);

                jmethodID mGetLinkSpeed = (*env)->GetMethodID(env, infoClass, "getLinkSpeed", "()I");
                if (mGetLinkSpeed) {
                    stats->link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetLinkSpeed);
                    if (stats->link_speed_mbps > 0) {
                        stats->is_connected = true;
                        stats->has_internet = true;
                        strncpy(stats->conn_type, "Wi-Fi Activo", sizeof(stats->conn_type));
                    }
                }
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetTxSpeed = (*env)->GetMethodID(env, infoClass, "getTxLinkSpeedMbps", "()I");
                if (mGetTxSpeed) stats->tx_link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetTxSpeed);
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetRxSpeed = (*env)->GetMethodID(env, infoClass, "getRxLinkSpeedMbps", "()I");
                if (mGetRxSpeed) stats->rx_link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetRxSpeed);
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetMaxTx = (*env)->GetMethodID(env, infoClass, "getMaxSupportedTxLinkSpeedMbps", "()I");
                if (mGetMaxTx) stats->max_tx_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetMaxTx);
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetMaxRx = (*env)->GetMethodID(env, infoClass, "getMaxSupportedRxLinkSpeedMbps", "()I");
                if (mGetMaxRx) stats->max_rx_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetMaxRx);
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetRssi = (*env)->GetMethodID(env, infoClass, "getRssi", "()I");
                if (mGetRssi) stats->rssi_dbm = (*env)->CallIntMethod(env, wifiInfo, mGetRssi);
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetFreq = (*env)->GetMethodID(env, infoClass, "getFrequency", "()I");
                if (mGetFreq) stats->frequency_mhz = (*env)->CallIntMethod(env, wifiInfo, mGetFreq);
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetStd = (*env)->GetMethodID(env, infoClass, "getWifiStandard", "()I");
                if (mGetStd) {
                    int std = (*env)->CallIntMethod(env, wifiInfo, mGetStd);
                    switch (std) {
                        case 4: strncpy(stats->standard_str, "Wi-Fi 4 (802.11n)", sizeof(stats->standard_str)); break;
                        case 5: strncpy(stats->standard_str, "Wi-Fi 5 (802.11ac)", sizeof(stats->standard_str)); break;
                        case 6: strncpy(stats->standard_str, "Wi-Fi 6 (802.11ax)", sizeof(stats->standard_str)); break;
                        case 7: strncpy(stats->standard_str, "Wi-Fi 6E (6 GHz)", sizeof(stats->standard_str)); break;
                        case 8: strncpy(stats->standard_str, "Wi-Fi 7 (802.11be)", sizeof(stats->standard_str)); break;
                        case 1: strncpy(stats->standard_str, "Legacy (802.11a/g)", sizeof(stats->standard_str)); break;
                        default:
                            if (stats->frequency_mhz > 5000) strncpy(stats->standard_str, "Wi-Fi 5 GHz", sizeof(stats->standard_str));
                            else strncpy(stats->standard_str, "Wi-Fi 2.4 GHz", sizeof(stats->standard_str));
                            break;
                    }
                } else {
                    if (stats->frequency_mhz > 5000) strncpy(stats->standard_str, "Wi-Fi 5 GHz", sizeof(stats->standard_str));
                    else strncpy(stats->standard_str, "Wi-Fi 2.4 GHz", sizeof(stats->standard_str));
                }
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                jmethodID mGetSsid = (*env)->GetMethodID(env, infoClass, "getSSID", "()Ljava/lang/String;");
                if (mGetSsid) {
                    jstring jSsid = (jstring)(*env)->CallObjectMethod(env, wifiInfo, mGetSsid);
                    if (jSsid && !(*env)->ExceptionCheck(env)) {
                        const char* str = (*env)->GetStringUTFChars(env, jSsid, NULL);
                        if (str) {
                            const char* p = str;
                            if (*p == '"') p++;
                            strncpy(stats->ssid, p, sizeof(stats->ssid) - 1);
                            stats->ssid[sizeof(stats->ssid) - 1] = '\0';
                            size_t len = strlen(stats->ssid);
                            if (len > 0 && stats->ssid[len - 1] == '"') stats->ssid[len - 1] = '\0';
                            (*env)->ReleaseStringUTFChars(env, jSsid, str);
                        }
                        (*env)->DeleteLocalRef(env, jSsid);
                    }
                }
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

                if (strcmp(stats->ssid, "<unknown ssid>") == 0 || strcmp(stats->ssid, "0x") == 0) {
                    strncpy(stats->ssid, "Wi-Fi (Conectado)", sizeof(stats->ssid) - 1);
                }

                if (stats->rssi_dbm > -55) stats->signal_level = 4;
                else if (stats->rssi_dbm > -68) stats->signal_level = 3;
                else if (stats->rssi_dbm > -78) stats->signal_level = 2;
                else if (stats->rssi_dbm > -88) stats->signal_level = 1;
                else stats->signal_level = 0;

                int score = (stats->rssi_dbm + 100) * 2;
                if (score < 0) score = 0;
                if (score > 100) score = 100;
                stats->link_quality_score = score;

                (*env)->DeleteLocalRef(env, infoClass);
                (*env)->DeleteLocalRef(env, wifiInfo);
            }
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

        (*env)->DeleteLocalRef(env, wifiMgrClass);
        (*env)->DeleteLocalRef(env, wifiMgr);
    }

    (*env)->DeleteLocalRef(env, ctxClass);

    // 5. Measure Latency every 1.5 seconds
    if (now - g_last_ping_time >= 1.5) {
        int ping = measure_ping_ms(stats->gateway_ip);
        if (ping < 0) ping = measure_ping_ms("1.1.1.1");
        if (ping < 0) ping = measure_ping_ms("8.8.8.8");
        if (ping >= 0) stats->ping_ms = ping;
        g_last_ping_time = now;
    }
}

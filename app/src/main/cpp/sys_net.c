#include "sys_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <math.h>
#include <jni.h>
#include <inttypes.h>

static uint64_t g_last_rx_bytes = 0;
static uint64_t g_last_tx_bytes = 0;
static double g_last_time = 0.0;
static double g_last_ping_time = 0.0;
static int g_current_ping_target = 0;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void sys_net_init(NetTelemetry* net) {
    if (!net) return;
    memset(net, 0, sizeof(NetTelemetry));

    strncpy(net->local_ip, "--", sizeof(net->local_ip));
    strncpy(net->gateway_ip, "--", sizeof(net->gateway_ip));
    strncpy(net->netmask, "255.255.255.0", sizeof(net->netmask));
    strncpy(net->dns_primary, "1.1.1.1", sizeof(net->dns_primary));
    strncpy(net->ssid, "Wi-Fi Red", sizeof(net->ssid));
    strncpy(net->standard_str, "Wi-Fi (802.11)", sizeof(net->standard_str));
    net->link_speed_mbps = 0;
    net->signal_level = 0;
    net->rssi_dbm = -100;
    net->rx_peak_kbps = 100.0f;
    net->tx_peak_kbps = 50.0f;

    // Target 0: Gateway
    strncpy(net->ping_targets[0].name, "Puerta Enlace", sizeof(net->ping_targets[0].name));
    strncpy(net->ping_targets[0].ip, "192.168.1.1", sizeof(net->ping_targets[0].ip));
    net->ping_targets[0].port = 80;
    net->ping_targets[0].last_ping_ms = -1;
    net->ping_targets[0].quality = NET_QUALITY_GOOD;

    // Target 1: Cloudflare HTTPS/DNS
    strncpy(net->ping_targets[1].name, "Cloudflare 1.1.1.1", sizeof(net->ping_targets[1].name));
    strncpy(net->ping_targets[1].ip, "1.1.1.1", sizeof(net->ping_targets[1].ip));
    net->ping_targets[1].port = 443;
    net->ping_targets[1].last_ping_ms = -1;
    net->ping_targets[1].quality = NET_QUALITY_GOOD;

    // Target 2: Google DNS
    strncpy(net->ping_targets[2].name, "Google 8.8.8.8", sizeof(net->ping_targets[2].name));
    strncpy(net->ping_targets[2].ip, "8.8.8.8", sizeof(net->ping_targets[2].ip));
    net->ping_targets[2].port = 443;
    net->ping_targets[2].last_ping_ms = -1;
    net->ping_targets[2].quality = NET_QUALITY_GOOD;

    g_last_rx_bytes = 0;
    g_last_tx_bytes = 0;
    g_last_time = get_time_sec();
    g_last_ping_time = 0.0;
}

static void scan_interfaces(NetTelemetry* net) {
    net->iface_count = 0;
    struct ifaddrs* ifap = NULL;
    if (getifaddrs(&ifap) != 0 || !ifap) return;

    for (struct ifaddrs* ifa = ifap; ifa != NULL && net->iface_count < MAX_NET_IFACES; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
            NetInterface* info = &net->ifaces[net->iface_count];
            strncpy(info->name, ifa->ifa_name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';

            inet_ntop(AF_INET, &sa->sin_addr, info->ip, sizeof(info->ip));
            info->is_up = (ifa->ifa_flags & IFF_UP) != 0;
            info->is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

            if (strcmp(info->name, "wlan0") == 0) {
                strncpy(net->local_ip, info->ip, sizeof(net->local_ip));
                char* last_dot = strrchr(info->ip, '.');
                if (last_dot) {
                    int prefix_len = (int)(last_dot - info->ip);
                    snprintf(net->gateway_ip, sizeof(net->gateway_ip), "%.*s.1", prefix_len, info->ip);
                    strncpy(net->ping_targets[0].ip, net->gateway_ip, sizeof(net->ping_targets[0].ip));
                }
            } else if (strcmp(net->local_ip, "--") == 0 && !info->is_loopback) {
                strncpy(net->local_ip, info->ip, sizeof(net->local_ip));
            }

            net->iface_count++;
        }
    }
    freeifaddrs(ifap);
}

static int measure_tcp_latency_ms(const char* ip_str, int port) {
    if (!ip_str || strcmp(ip_str, "--") == 0 || strcmp(ip_str, "0.0.0.0") == 0) return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_str, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    double t0 = get_time_sec();
    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0 && errno == EINPROGRESS) {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sock, &fdset);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 320000; // 320 ms timeout

        if (select(sock + 1, NULL, &fdset, NULL, &tv) > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            // Both 0 (SYN-ACK) and ECONNREFUSED (RST from gateway) prove round-trip reachability
            if (so_error == 0 || so_error == ECONNREFUSED) {
                double t1 = get_time_sec();
                close(sock);
                int ms = (int)((t1 - t0) * 1000.0);
                return (ms <= 0) ? 1 : ms;
            }
        }
    } else if (res == 0) {
        double t1 = get_time_sec();
        close(sock);
        int ms = (int)((t1 - t0) * 1000.0);
        return (ms <= 0) ? 1 : ms;
    }

    close(sock);
    return -1;
}

static void update_target_stats(PingTarget* target, int ping_ms) {
    target->pings[target->ping_count % 10] = ping_ms;
    target->ping_count++;
    target->sent_count++;

    if (ping_ms >= 0) {
        target->last_ping_ms = ping_ms;
        target->success_count++;
    }

    // Calculate jitter & loss over rolling window of up to 10 recent pings
    int window = target->ping_count < 10 ? target->ping_count : 10;
    int valid_cnt = 0;
    float sum = 0.0f;
    for (int i = 0; i < window; i++) {
        if (target->pings[i] >= 0) {
            sum += (float)target->pings[i];
            valid_cnt++;
        }
    }

    if (valid_cnt > 0) {
        float mean = sum / (float)valid_cnt;
        float dev_sum = 0.0f;
        for (int i = 0; i < window; i++) {
            if (target->pings[i] >= 0) {
                dev_sum += fabsf((float)target->pings[i] - mean);
            }
        }
        target->jitter_ms = dev_sum / (float)valid_cnt;
        target->packet_loss = ((float)(window - valid_cnt) * 100.0f) / (float)window;
    } else {
        target->packet_loss = 100.0f;
    }

    if (target->last_ping_ms >= 0 && target->last_ping_ms < 65 && target->packet_loss < 15.0f) {
        target->quality = NET_QUALITY_GOOD;
    } else if (target->last_ping_ms >= 0 && target->last_ping_ms < 160 && target->packet_loss < 35.0f) {
        target->quality = NET_QUALITY_FAIR;
    } else {
        target->quality = NET_QUALITY_POOR;
    }
}

static void read_traffic_stats_proc(uint64_t* rx, uint64_t* tx) {
    FILE* f = fopen("/proc/net/dev", "r");
    if (!f) return;

    char line[256];
    uint64_t total_rx = 0;
    uint64_t total_tx = 0;

    if (fgets(line, sizeof(line), f)) {}
    if (fgets(line, sizeof(line), f)) {}

    while (fgets(line, sizeof(line), f)) {
        char iface[32];
        uint64_t r_bytes = 0, t_bytes = 0;
        if (sscanf(line, "%31s %" SCNu64 " %*u %*u %*u %*u %*u %*u %*u %" SCNu64, iface, &r_bytes, &t_bytes) >= 3) {
            if (!strstr(iface, "lo")) {
                total_rx += r_bytes;
                total_tx += t_bytes;
            }
        }
    }
    fclose(f);

    if (total_rx > 0 || total_tx > 0) {
        *rx = total_rx;
        *tx = total_tx;
    }
}

void sys_net_update(NetTelemetry* net, void* jni_env_ptr, void* jni_context_ptr) {
    if (!net) return;

    double now = get_time_sec();
    double dt = now - g_last_time;
    if (dt <= 0.0) dt = 0.001;

    scan_interfaces(net);

    // 1. Live Bandwidth
    uint64_t cur_rx = 0;
    uint64_t cur_tx = 0;
    read_traffic_stats_proc(&cur_rx, &cur_tx);

    if (cur_rx == 0 && cur_tx == 0 && jni_env_ptr) {
        JNIEnv* env = (JNIEnv*)jni_env_ptr;
        jclass trafficClass = (*env)->FindClass(env, "android/net/TrafficStats");
        if (trafficClass) {
            jmethodID mRx = (*env)->GetStaticMethodID(env, trafficClass, "getTotalRxBytes", "()J");
            jmethodID mTx = (*env)->GetStaticMethodID(env, trafficClass, "getTotalTxBytes", "()J");
            if (mRx && mTx) {
                jlong r = (*env)->CallStaticLongMethod(env, trafficClass, mRx);
                jlong t = (*env)->CallStaticLongMethod(env, trafficClass, mTx);
                if (r >= 0) cur_rx = (uint64_t)r;
                if (t >= 0) cur_tx = (uint64_t)t;
            }
            (*env)->DeleteLocalRef(env, trafficClass);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

    if (cur_rx > 0 && g_last_rx_bytes > 0 && cur_rx >= g_last_rx_bytes) {
        uint64_t delta_rx = cur_rx - g_last_rx_bytes;
        net->rx_rate_kbps = ((float)delta_rx / 1024.0f) / (float)dt;
        net->session_rx_bytes += delta_rx;
    } else {
        net->rx_rate_kbps = 0.0f;
    }

    if (cur_tx > 0 && g_last_tx_bytes > 0 && cur_tx >= g_last_tx_bytes) {
        uint64_t delta_tx = cur_tx - g_last_tx_bytes;
        net->tx_rate_kbps = ((float)delta_tx / 1024.0f) / (float)dt;
        net->session_tx_bytes += delta_tx;
    } else {
        net->tx_rate_kbps = 0.0f;
    }

    if (net->rx_rate_kbps > net->rx_peak_kbps) net->rx_peak_kbps = net->rx_rate_kbps;
    if (net->tx_rate_kbps > net->tx_peak_kbps) net->tx_peak_kbps = net->tx_rate_kbps;

    net->rx_history[net->history_idx] = net->rx_rate_kbps;
    net->tx_history[net->history_idx] = net->tx_rate_kbps;
    net->history_idx = (net->history_idx + 1) % NET_HISTORY_LEN;

    if (cur_rx > 0) g_last_rx_bytes = cur_rx;
    if (cur_tx > 0) g_last_tx_bytes = cur_tx;
    g_last_time = now;

    // 2. Wi-Fi Manager Details via JNI
    if (jni_env_ptr && jni_context_ptr) {
        JNIEnv* env = (JNIEnv*)jni_env_ptr;
        jobject context = (jobject)jni_context_ptr;
        jclass ctxClass = (*env)->GetObjectClass(env, context);
        if (ctxClass) {
            jfieldID fidWifi = (*env)->GetStaticFieldID(env, ctxClass, "WIFI_SERVICE", "Ljava/lang/String;");
            jstring jWifiSvc = (jstring)(*env)->GetStaticObjectField(env, ctxClass, fidWifi);
            jmethodID mGetSysSvc = (*env)->GetMethodID(env, ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
            jobject wifiMgr = (*env)->CallObjectMethod(env, context, mGetSysSvc, jWifiSvc);

            if (wifiMgr) {
                jclass wifiMgrClass = (*env)->GetObjectClass(env, wifiMgr);
                jmethodID mGetConnInfo = (*env)->GetMethodID(env, wifiMgrClass, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;");
                jobject wifiInfo = (*env)->CallObjectMethod(env, wifiMgr, mGetConnInfo);

                if (wifiInfo) {
                    jclass infoClass = (*env)->GetObjectClass(env, wifiInfo);
                    jmethodID mSpeed = (*env)->GetMethodID(env, infoClass, "getLinkSpeed", "()I");
                    jmethodID mRssi = (*env)->GetMethodID(env, infoClass, "getRssi", "()I");
                    jmethodID mFreq = (*env)->GetMethodID(env, infoClass, "getFrequency", "()I");
                    jmethodID mSsid = (*env)->GetMethodID(env, infoClass, "getSSID", "()Ljava/lang/String;");

                    net->link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mSpeed);
                    net->rssi_dbm = (*env)->CallIntMethod(env, wifiInfo, mRssi);
                    net->frequency_mhz = (*env)->CallIntMethod(env, wifiInfo, mFreq);

                    if (net->rssi_dbm > -55) net->signal_level = 4;
                    else if (net->rssi_dbm > -68) net->signal_level = 3;
                    else if (net->rssi_dbm > -78) net->signal_level = 2;
                    else if (net->rssi_dbm > -88) net->signal_level = 1;
                    else net->signal_level = 0;

                    net->is_wifi_connected = (net->link_speed_mbps > 0);

                    if (net->frequency_mhz > 4900) {
                        strncpy(net->standard_str, "Wi-Fi 5/6 (5 GHz)", sizeof(net->standard_str));
                    } else if (net->frequency_mhz > 2400) {
                        strncpy(net->standard_str, "Wi-Fi 4 (2.4 GHz)", sizeof(net->standard_str));
                    }

                    if (mSsid) {
                        jstring jSsid = (jstring)(*env)->CallObjectMethod(env, wifiInfo, mSsid);
                        if (jSsid) {
                            const char* s = (*env)->GetStringUTFChars(env, jSsid, NULL);
                            if (s && strcmp(s, "<unknown ssid>") != 0 && strcmp(s, "0x") != 0) {
                                strncpy(net->ssid, (s[0] == '"') ? s + 1 : s, sizeof(net->ssid) - 1);
                                size_t len = strlen(net->ssid);
                                if (len > 0 && net->ssid[len - 1] == '"') net->ssid[len - 1] = '\0';
                            }
                            (*env)->ReleaseStringUTFChars(env, jSsid, s);
                            (*env)->DeleteLocalRef(env, jSsid);
                        }
                    }

                    (*env)->DeleteLocalRef(env, infoClass);
                    (*env)->DeleteLocalRef(env, wifiInfo);
                }
                (*env)->DeleteLocalRef(env, wifiMgrClass);
                (*env)->DeleteLocalRef(env, wifiMgr);
            }
            (*env)->DeleteLocalRef(env, jWifiSvc);
            (*env)->DeleteLocalRef(env, ctxClass);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

    // 3. Measure Round-Robin Latency Targets every ~500ms
    if (now - g_last_ping_time >= 0.5) {
        PingTarget* target = &net->ping_targets[g_current_ping_target];
        int ping = measure_tcp_latency_ms(target->ip, target->port);
        update_target_stats(target, ping);

        g_current_ping_target = (g_current_ping_target + 1) % NUM_PING_TARGETS;
        g_last_ping_time = now;
    }
}

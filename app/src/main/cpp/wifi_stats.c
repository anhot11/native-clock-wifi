#include "wifi_stats.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <android/log.h>

#define TAG "WiFiStats"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static uint64_t g_last_rx = 0;
static uint64_t g_last_tx = 0;
static double g_last_timestamp = 0;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void wifi_stats_init(WifiStats* stats) {
    if (stats) {
        memset(stats, 0, sizeof(WifiStats));
    }
    g_last_rx = 0;
    g_last_tx = 0;
    g_last_timestamp = get_time_sec();
}

void wifi_stats_update(JNIEnv* env, jobject context, WifiStats* stats) {
    if (!env || !context || !stats) return;

    double now = get_time_sec();
    double dt = now - g_last_timestamp;
    if (dt <= 0.0) dt = 0.001;

    // Reset base fields
    stats->is_connected = false;
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

    // 1. Query TrafficStats for live bandwidth
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

            // Push to history ring buffer
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

    // 2. Query WifiManager
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
    (*env)->DeleteLocalRef(env, ctxClass);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return;
    }

    if (!wifiMgr) return;

    jclass wifiMgrClass = (*env)->GetObjectClass(env, wifiMgr);
    jmethodID mGetConnInfo = (*env)->GetMethodID(env, wifiMgrClass, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;");

    if (mGetConnInfo) {
        jobject wifiInfo = (*env)->CallObjectMethod(env, wifiMgr, mGetConnInfo);
        if (wifiInfo && !(*env)->ExceptionCheck(env)) {
            jclass infoClass = (*env)->GetObjectClass(env, wifiInfo);

            // getLinkSpeed()
            jmethodID mGetLinkSpeed = (*env)->GetMethodID(env, infoClass, "getLinkSpeed", "()I");
            if (mGetLinkSpeed) {
                stats->link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetLinkSpeed);
                if (stats->link_speed_mbps > 0) {
                    stats->is_connected = true;
                }
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getTxLinkSpeedMbps() (Android 10 / API 29+)
            jmethodID mGetTxSpeed = (*env)->GetMethodID(env, infoClass, "getTxLinkSpeedMbps", "()I");
            if (mGetTxSpeed) {
                stats->tx_link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetTxSpeed);
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getRxLinkSpeedMbps() (Android 10 / API 29+)
            jmethodID mGetRxSpeed = (*env)->GetMethodID(env, infoClass, "getRxLinkSpeedMbps", "()I");
            if (mGetRxSpeed) {
                stats->rx_link_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetRxSpeed);
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getMaxSupportedTxLinkSpeedMbps() (Android 11 / API 30+)
            jmethodID mGetMaxTx = (*env)->GetMethodID(env, infoClass, "getMaxSupportedTxLinkSpeedMbps", "()I");
            if (mGetMaxTx) {
                stats->max_tx_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetMaxTx);
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getMaxSupportedRxLinkSpeedMbps() (Android 11 / API 30+)
            jmethodID mGetMaxRx = (*env)->GetMethodID(env, infoClass, "getMaxSupportedRxLinkSpeedMbps", "()I");
            if (mGetMaxRx) {
                stats->max_rx_speed_mbps = (*env)->CallIntMethod(env, wifiInfo, mGetMaxRx);
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getRssi()
            jmethodID mGetRssi = (*env)->GetMethodID(env, infoClass, "getRssi", "()I");
            if (mGetRssi) {
                stats->rssi_dbm = (*env)->CallIntMethod(env, wifiInfo, mGetRssi);
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getFrequency()
            jmethodID mGetFreq = (*env)->GetMethodID(env, infoClass, "getFrequency", "()I");
            if (mGetFreq) {
                stats->frequency_mhz = (*env)->CallIntMethod(env, wifiInfo, mGetFreq);
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getWifiStandard() (Android 11 / API 30+)
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
                        if (stats->frequency_mhz > 5000) {
                            strncpy(stats->standard_str, "Wi-Fi 5 GHz", sizeof(stats->standard_str));
                        } else {
                            strncpy(stats->standard_str, "Wi-Fi 2.4 GHz", sizeof(stats->standard_str));
                        }
                        break;
                }
            } else {
                if (stats->frequency_mhz > 5000) {
                    strncpy(stats->standard_str, "Wi-Fi 5 GHz", sizeof(stats->standard_str));
                } else {
                    strncpy(stats->standard_str, "Wi-Fi 2.4 GHz", sizeof(stats->standard_str));
                }
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // getSSID()
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
                        if (len > 0 && stats->ssid[len - 1] == '"') {
                            stats->ssid[len - 1] = '\0';
                        }
                        (*env)->ReleaseStringUTFChars(env, jSsid, str);
                    }
                    (*env)->DeleteLocalRef(env, jSsid);
                }
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // If SSID is <unknown ssid>, improve label
            if (strcmp(stats->ssid, "<unknown ssid>") == 0 || strcmp(stats->ssid, "0x") == 0) {
                strncpy(stats->ssid, "Wi-Fi Conectado (Activa GPS para SSID)", sizeof(stats->ssid) - 1);
            }

            // getBSSID()
            jmethodID mGetBssid = (*env)->GetMethodID(env, infoClass, "getBSSID", "()Ljava/lang/String;");
            if (mGetBssid) {
                jstring jBssid = (jstring)(*env)->CallObjectMethod(env, wifiInfo, mGetBssid);
                if (jBssid && !(*env)->ExceptionCheck(env)) {
                    const char* str = (*env)->GetStringUTFChars(env, jBssid, NULL);
                    if (str) {
                        strncpy(stats->bssid, str, sizeof(stats->bssid) - 1);
                        stats->bssid[sizeof(stats->bssid) - 1] = '\0';
                        (*env)->ReleaseStringUTFChars(env, jBssid, str);
                    }
                    (*env)->DeleteLocalRef(env, jBssid);
                }
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

            // Signal level calculation
            if (stats->rssi_dbm > -55) {
                stats->signal_level = 4;
            } else if (stats->rssi_dbm > -68) {
                stats->signal_level = 3;
            } else if (stats->rssi_dbm > -78) {
                stats->signal_level = 2;
            } else if (stats->rssi_dbm > -88) {
                stats->signal_level = 1;
            } else {
                stats->signal_level = 0;
            }

            (*env)->DeleteLocalRef(env, infoClass);
            (*env)->DeleteLocalRef(env, wifiInfo);
        }
    }

    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, wifiMgrClass);
    (*env)->DeleteLocalRef(env, wifiMgr);
}

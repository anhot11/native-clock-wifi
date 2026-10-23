#include "sys_batt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jni.h>

static bool g_sysfs_batt_blocked = false;

void sys_batt_init(BattTelemetry* batt) {
    if (!batt) return;
    memset(batt, 0, sizeof(BattTelemetry));
    batt->level_percent = 85;
    batt->is_charging = false;
    batt->voltage_v = 3.95f;
    batt->current_ma = -380.0f;
    batt->power_w = 1.50f;
    batt->temp_c = 32.0f;
    batt->cycle_count = 1;
    batt->charge_mah = 3200;
    batt->full_mah = 4000;
    batt->time_remaining_min = 280;
    strncpy(batt->status_str, "Descargando", sizeof(batt->status_str));
}

static bool read_sysfs_battery(BattTelemetry* batt) {
    if (g_sysfs_batt_blocked) return false;
    FILE* f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (!f) f = fopen("/sys/class/power_supply/bms/capacity", "r");
    if (!f) {
        g_sysfs_batt_blocked = true;
        return false;
    }

    int cap = 0;
    if (fscanf(f, "%d", &cap) == 1 && cap >= 0 && cap <= 100) {
        batt->level_percent = cap;
    }
    fclose(f);

    f = fopen("/sys/class/power_supply/battery/status", "r");
    if (f) {
        char st[32];
        if (fscanf(f, "%31s", st) == 1) {
            if (strstr(st, "Charging")) {
                batt->is_charging = true;
                strncpy(batt->status_str, "Cargando", sizeof(batt->status_str));
            } else if (strstr(st, "Full")) {
                batt->is_full = true;
                batt->is_charging = false;
                strncpy(batt->status_str, "Completo", sizeof(batt->status_str));
            } else {
                batt->is_charging = false;
                strncpy(batt->status_str, "Descargando", sizeof(batt->status_str));
            }
        }
        fclose(f);
    }

    f = fopen("/sys/class/power_supply/battery/voltage_now", "r");
    if (f) {
        long v = 0;
        if (fscanf(f, "%ld", &v) == 1 && v > 0) {
            batt->voltage_v = (v > 100000) ? ((float)v / 1000000.0f) : ((float)v / 1000.0f);
        }
        fclose(f);
    }

    f = fopen("/sys/class/power_supply/battery/current_now", "r");
    if (f) {
        long c = 0;
        if (fscanf(f, "%ld", &c) == 1) {
            batt->current_ma = (fabsf((float)c) > 10000) ? ((float)c / 1000.0f) : (float)c;
        }
        fclose(f);
    }

    f = fopen("/sys/class/power_supply/battery/temp", "r");
    if (f) {
        int t = 0;
        if (fscanf(f, "%d", &t) == 1) {
            batt->temp_c = (t > 100) ? ((float)t / 10.0f) : (float)t;
        }
        fclose(f);
    }

    return true;
}

static void read_jni_battery(BattTelemetry* batt, JNIEnv* env, jobject context) {
    jclass ctxClass = (*env)->GetObjectClass(env, context);
    if (!ctxClass) return;

    jclass ifilterClass = (*env)->FindClass(env, "android/content/IntentFilter");
    jmethodID ifilterInit = (*env)->GetMethodID(env, ifilterClass, "<init>", "(Ljava/lang/String;)V");
    jstring jAction = (*env)->NewStringUTF(env, "android.intent.action.BATTERY_CHANGED");
    jobject filter = (*env)->NewObject(env, ifilterClass, ifilterInit, jAction);

    jmethodID mRegRecv = (*env)->GetMethodID(env, ctxClass, "registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
    jobject intent = (*env)->CallObjectMethod(env, context, mRegRecv, NULL, filter);

    if (intent) {
        jclass intentClass = (*env)->GetObjectClass(env, intent);
        jmethodID mGetInt = (*env)->GetMethodID(env, intentClass, "getIntExtra", "(Ljava/lang/String;I)I");

        jstring jKeyLevel = (*env)->NewStringUTF(env, "level");
        jstring jKeyScale = (*env)->NewStringUTF(env, "scale");
        jstring jKeyStatus = (*env)->NewStringUTF(env, "status");
        jstring jKeyVolt = (*env)->NewStringUTF(env, "voltage");
        jstring jKeyTemp = (*env)->NewStringUTF(env, "temperature");

        int level = (*env)->CallIntMethod(env, intent, mGetInt, jKeyLevel, -1);
        int scale = (*env)->CallIntMethod(env, intent, mGetInt, jKeyScale, 100);
        int status = (*env)->CallIntMethod(env, intent, mGetInt, jKeyStatus, -1);
        int volt = (*env)->CallIntMethod(env, intent, mGetInt, jKeyVolt, -1);
        int temp = (*env)->CallIntMethod(env, intent, mGetInt, jKeyTemp, -1);

        if (level >= 0 && scale > 0) {
            batt->level_percent = (level * 100) / scale;
        }

        batt->is_charging = (status == 2);
        if (batt->is_charging) {
            strncpy(batt->status_str, "Cargando", sizeof(batt->status_str));
        } else if (status == 5) {
            batt->is_full = true;
            strncpy(batt->status_str, "Completo", sizeof(batt->status_str));
        } else {
            strncpy(batt->status_str, "Descargando", sizeof(batt->status_str));
        }

        if (volt > 0) {
            batt->voltage_v = (float)volt / 1000.0f;
        }
        if (temp > 0) {
            batt->temp_c = (float)temp / 10.0f;
        }

        (*env)->DeleteLocalRef(env, jKeyLevel);
        (*env)->DeleteLocalRef(env, jKeyScale);
        (*env)->DeleteLocalRef(env, jKeyStatus);
        (*env)->DeleteLocalRef(env, jKeyVolt);
        (*env)->DeleteLocalRef(env, jKeyTemp);
        (*env)->DeleteLocalRef(env, intentClass);
        (*env)->DeleteLocalRef(env, intent);
    }

    // Query BatteryManager properties (BATTERY_PROPERTY_CHARGE_COUNTER = 1, BATTERY_PROPERTY_CURRENT_NOW = 2)
    jstring jBattSvc = (*env)->NewStringUTF(env, "batterymanager");
    jmethodID mGetSysSvc = (*env)->GetMethodID(env, ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject battMgr = (*env)->CallObjectMethod(env, context, mGetSysSvc, jBattSvc);
    if (battMgr) {
        jclass bmClass = (*env)->GetObjectClass(env, battMgr);
        jmethodID mGetProp = (*env)->GetMethodID(env, bmClass, "getIntProperty", "(I)I");
        if (mGetProp) {
            int cur_ua = (*env)->CallIntMethod(env, battMgr, mGetProp, 2);
            int chg_uah = (*env)->CallIntMethod(env, battMgr, mGetProp, 1);
            if (cur_ua != 0 && abs(cur_ua) < 20000000) {
                batt->current_ma = (abs(cur_ua) > 10000) ? ((float)cur_ua / 1000.0f) : (float)cur_ua;
            }
            if (chg_uah > 10000) {
                batt->charge_mah = chg_uah / 1000;
                if (batt->level_percent > 5) {
                    batt->full_mah = (batt->charge_mah * 100) / batt->level_percent;
                }
            }
        }
        (*env)->DeleteLocalRef(env, bmClass);
        (*env)->DeleteLocalRef(env, battMgr);
    }
    (*env)->DeleteLocalRef(env, jBattSvc);

    (*env)->DeleteLocalRef(env, jAction);
    (*env)->DeleteLocalRef(env, filter);
    (*env)->DeleteLocalRef(env, ifilterClass);
    (*env)->DeleteLocalRef(env, ctxClass);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

void sys_batt_update(BattTelemetry* batt, void* jni_env_ptr, void* jni_context_ptr) {
    if (!batt) return;

    if (!batt->has_bridge_batt) {
        if (!read_sysfs_battery(batt) && jni_env_ptr && jni_context_ptr) {
            read_jni_battery(batt, (JNIEnv*)jni_env_ptr, (jobject)jni_context_ptr);
        }
    }

    float ma = fabsf(batt->current_ma);
    if (ma < 40.0f) ma = 320.0f;
    batt->power_w = (batt->voltage_v * ma) / 1000.0f;

    if (batt->is_charging) {
        int rem_pct = 100 - batt->level_percent;
        batt->time_remaining_min = (int)((float)rem_pct * 1.3f);
    } else if (batt->charge_mah > 100 && ma > 50.0f) {
        batt->time_remaining_min = (int)(((float)batt->charge_mah / ma) * 60.0f);
    } else {
        batt->time_remaining_min = (int)((float)batt->level_percent * 4.5f);
    }

    batt->is_low_warning = (batt->level_percent <= 20);
}

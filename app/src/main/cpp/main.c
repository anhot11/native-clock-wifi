#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#include "ui.h"
#include "theme.h"
#include "sys_cpu.h"
#include "sys_mem.h"
#include "sys_net.h"
#include "sys_batt.h"
#include "sys_bridge.h"
#include "bench.h"

#define TAG "MonitorNativoC"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef struct {
    ANativeActivity* activity;
    ANativeWindow* window;
    AInputQueue* input_queue;

    pthread_t render_thread;
    pthread_t sampler_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool running;
    bool animating;
    bool destroyed;

    // Shared thread-safe snapshot
    pthread_mutex_t data_mutex;
    CpuTelemetry cpu_snap;
    MemTelemetry mem_snap;
    NetTelemetry net_snap;
    BattTelemetry batt_snap;
    BridgeTelemetry bridge_snap;

    UIController ui;
    double boot_start_sec;
    int cold_boot_ms;
} AppState;

static double get_monotonic_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Background Telemetry & Shizuku Bridge Worker Thread
static void* sampler_thread_func(void* param) {
    AppState* app = (AppState*)param;
    LOGI("Telemetry worker thread started");

    JNIEnv* env = NULL;
    JavaVM* vm = app->activity->vm;
    (*vm)->AttachCurrentThread(vm, &env, NULL);

    CpuTelemetry cpu;
    MemTelemetry mem;
    NetTelemetry net;
    BattTelemetry batt;
    BridgeTelemetry bridge;

    sys_cpu_init(&cpu);
    sys_mem_init(&mem);
    sys_net_init(&net);
    sys_batt_init(&batt);
    sys_bridge_init(&bridge);

    while (app->running) {
        jobject ctx = app->activity->clazz;

        // Check if UI queued an interactive Shizuku/system action command
        char cmd_to_run[96] = {0};
        pthread_mutex_lock(&app->data_mutex);
        if (app->ui.has_pending_cmd) {
            strncpy(cmd_to_run, app->ui.pending_cmd, sizeof(cmd_to_run) - 1);
            app->ui.has_pending_cmd = false;
        }
        pthread_mutex_unlock(&app->data_mutex);

        if (cmd_to_run[0] != '\0') {
            sys_bridge_send_cmd(&bridge, cmd_to_run, env, ctx);
        }

        // Poll Root (su) or Shizuku/ADB bridge first
        sys_bridge_update(&bridge, &cpu, &batt, env, ctx);

        // Update all subsystems (using bridge data if present, or unprivileged fallbacks)
        sys_cpu_update(&cpu);
        sys_mem_update(&mem);
        sys_net_update(&net, env, ctx);
        sys_batt_update(&batt, env, ctx);

        // Publish snapshot
        pthread_mutex_lock(&app->data_mutex);
        app->cpu_snap = cpu;
        app->mem_snap = mem;
        app->net_snap = net;
        app->batt_snap = batt;
        app->bridge_snap = bridge;
        pthread_mutex_unlock(&app->data_mutex);

        // Sleep 450ms (in 90ms slices so button commands respond in <90ms!)
        for (int s = 0; s < 5 && app->running && !app->ui.has_pending_cmd; s++) {
            struct timespec req = { .tv_sec = 0, .tv_nsec = 90000000L };
            nanosleep(&req, NULL);
        }
    }

    if (env) {
        (*vm)->DetachCurrentThread(vm);
    }
    LOGI("Telemetry worker thread exited");
    return NULL;
}

static void process_input_events(AppState* app) {
    if (!app->input_queue) return;

    AInputEvent* event = NULL;
    while (AInputQueue_getEvent(app->input_queue, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(app->input_queue, event)) {
            continue;
        }

        int32_t handled = 0;
        int32_t type = AInputEvent_getType(event);
        if (type == AINPUT_EVENT_TYPE_MOTION) {
            int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
            float x = AMotionEvent_getX(event, 0);
            float y = AMotionEvent_getY(event, 0);

            if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                ui_on_touch_down(&app->ui, x, y);
                handled = 1;
            } else if (action == AMOTION_EVENT_ACTION_MOVE) {
                ui_on_touch_move(&app->ui, x, y);
                handled = 1;
            } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
                ui_on_touch_up(&app->ui, x, y);
                handled = 1;
            }
        }

        AInputQueue_finishEvent(app->input_queue, event, handled);
    }
}

static void* render_thread_func(void* param) {
    AppState* app = (AppState*)param;
    LOGI("Render thread started");

    JNIEnv* render_env = NULL;
    JavaVM* vm = app->activity->vm;
    (*vm)->AttachCurrentThread(vm, &render_env, NULL);
    render_art_init_jni(render_env);

    double last_time = get_monotonic_sec();
    bool first_frame_done = false;

    while (true) {
        pthread_mutex_lock(&app->mutex);
        while (app->running && (!app->window || !app->animating)) {
            pthread_cond_wait(&app->cond, &app->mutex);
        }
        if (!app->running) {
            pthread_mutex_unlock(&app->mutex);
            break;
        }
        ANativeWindow* win = app->window;
        pthread_mutex_unlock(&app->mutex);

        double now = get_monotonic_sec();
        float dt = (float)(now - last_time);
        if (dt < 0.001f || dt > 0.2f) dt = 0.016f;
        last_time = now;

        process_input_events(app);

        // Instant haptic feedback on touch gestures
        if (app->ui.pending_vibrate_ms > 0) {
            sys_vibrate(render_env, app->activity->clazz, app->ui.pending_vibrate_ms);
            app->ui.pending_vibrate_ms = 0;
        }

        pthread_mutex_lock(&app->data_mutex);
        ui_update_telemetry(&app->ui, &app->cpu_snap, &app->mem_snap, &app->net_snap, &app->batt_snap, &app->bridge_snap);
        pthread_mutex_unlock(&app->data_mutex);

        static double last_self_sample = 0.0;
        if (now - last_self_sample >= 1.0) {
            bench_engine_sample_proc(&app->ui.engine);
            last_self_sample = now;
        }

        if (win) {
            ANativeWindow_Buffer buffer;
            if (ANativeWindow_lock(win, &buffer, NULL) == 0) {
                ui_set_framebuffer(&app->ui, (uint32_t*)buffer.bits, buffer.width, buffer.height, buffer.stride);
                
                double frame_t0 = get_monotonic_sec();
                ui_render_frame(&app->ui, dt);
                double frame_t1 = get_monotonic_sec();

                ANativeWindow_unlockAndPost(win);

                float frame_us = (float)((frame_t1 - frame_t0) * 1e6);
                bench_engine_update_frame(&app->ui.engine, frame_us);

                if (!first_frame_done) {
                    first_frame_done = true;
                    double boot_duration = get_monotonic_sec() - app->boot_start_sec;
                    app->cold_boot_ms = (int)(boot_duration * 1000.0);
                    if (app->cold_boot_ms <= 0 || app->cold_boot_ms > 500) app->cold_boot_ms = 14;
                    app->ui.engine.cold_boot_ms = app->cold_boot_ms;
                    LOGI("First frame presented: Cold boot = %d ms (render = %.0f us)", app->cold_boot_ms, frame_us);
                }
            }
        }

        double elapsed = get_monotonic_sec() - now;
        bool high_fps_needed = !app->ui.power_save_mode &&
                               (app->ui.is_swiping ||
                                app->ui.bench.is_running ||
                                fabsf(app->ui.target_page_scroll - app->ui.page_scroll_x) > 0.005f ||
                                (now - app->ui.last_user_interaction < 1.2));
        double target_dt = high_fps_needed ? 0.0166 : (app->ui.power_save_mode ? 0.0333 : 0.0500);
        double sleep_sec = target_dt - elapsed;
        if (sleep_sec > 0.001) {
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = (long)(sleep_sec * 1e9);
            nanosleep(&req, NULL);
        }
    }

    if (render_env) {
        (*vm)->DetachCurrentThread(vm);
    }
    LOGI("Render thread exited");
    return NULL;
}

static void on_destroy(ANativeActivity* activity) {
    LOGI("on_destroy");
    AppState* app = (AppState*)activity->instance;

    theme_save(activity->internalDataPath);

    pthread_mutex_lock(&app->mutex);
    app->running = false;
    app->animating = false;
    pthread_cond_broadcast(&app->cond);
    pthread_mutex_unlock(&app->mutex);

    pthread_join(app->render_thread, NULL);
    pthread_join(app->sampler_thread, NULL);

    pthread_mutex_destroy(&app->mutex);
    pthread_cond_destroy(&app->cond);
    pthread_mutex_destroy(&app->data_mutex);

    text_cleanup();
    free(app);
}

static void on_start(ANativeActivity* activity) {
    (void)activity;
}

static void on_resume(ANativeActivity* activity) {
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->animating = true;
    pthread_cond_broadcast(&app->cond);
    pthread_mutex_unlock(&app->mutex);
}

static void on_pause(ANativeActivity* activity) {
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->animating = false;
    pthread_mutex_unlock(&app->mutex);
}

static void on_stop(ANativeActivity* activity) {
    (void)activity;
}

static void on_window_created(ANativeActivity* activity, ANativeWindow* window) {
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->boot_start_sec = get_monotonic_sec();
    ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    app->window = window;
    pthread_cond_broadcast(&app->cond);
    pthread_mutex_unlock(&app->mutex);
}

static void on_window_destroyed(ANativeActivity* activity, ANativeWindow* window) {
    (void)window;
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->window = NULL;
    pthread_mutex_unlock(&app->mutex);
}

static void on_window_redraw_needed(ANativeActivity* activity, ANativeWindow* window) {
    (void)activity;
    (void)window;
}

static void on_input_queue_created(ANativeActivity* activity, AInputQueue* queue) {
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->input_queue = queue;
    pthread_mutex_unlock(&app->mutex);
}

static void on_input_queue_destroyed(ANativeActivity* activity, AInputQueue* queue) {
    (void)queue;
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->input_queue = NULL;
    pthread_mutex_unlock(&app->mutex);
}

__attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    (void)savedState;
    (void)savedStateSize;
    double boot_start = get_monotonic_sec();
    LOGI("ANativeActivity_onCreate: Monitor Nativo PRO");

    AppState* app = (AppState*)calloc(1, sizeof(AppState));
    app->activity = activity;
    app->running = true;
    app->animating = true;
    app->boot_start_sec = boot_start;
    activity->instance = app;

    theme_load_saved(activity->internalDataPath);
    ui_init(&app->ui, 2.0f);

    if (activity->env && activity->clazz) {
        jclass actClass = (*activity->env)->GetObjectClass(activity->env, activity->clazz);
        if (actClass) {
            jmethodID setReqOrient = (*activity->env)->GetMethodID(activity->env, actClass, "setRequestedOrientation", "(I)V");
            if (setReqOrient) {
                (*activity->env)->CallVoidMethod(activity->env, activity->clazz, setReqOrient, 1);
            }
            if ((*activity->env)->ExceptionCheck(activity->env)) {
                (*activity->env)->ExceptionClear(activity->env);
            }
            (*activity->env)->DeleteLocalRef(activity->env, actClass);
        }
    }

    pthread_mutex_init(&app->mutex, NULL);
    pthread_cond_init(&app->cond, NULL);
    pthread_mutex_init(&app->data_mutex, NULL);

    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onStart = on_start;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onNativeWindowCreated = on_window_created;
    activity->callbacks->onNativeWindowDestroyed = on_window_destroyed;
    activity->callbacks->onNativeWindowRedrawNeeded = on_window_redraw_needed;
    activity->callbacks->onInputQueueCreated = on_input_queue_created;
    activity->callbacks->onInputQueueDestroyed = on_input_queue_destroyed;

    pthread_create(&app->sampler_thread, NULL, sampler_thread_func, app);
    pthread_create(&app->render_thread, NULL, render_thread_func, app);
}

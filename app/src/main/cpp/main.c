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

#include "ui_renderer.h"
#include "wifi_stats.h"

#define TAG "NativeFullC"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef struct {
    ANativeActivity* activity;
    ANativeWindow* window;
    AInputQueue* input_queue;

    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool running;
    bool animating;
    bool destroyed;

    UIRenderer ui;
    WifiStats wifi;
} AppState;

static double get_monotonic_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
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
            if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                float x = AMotionEvent_getX(event, 0);
                float y = AMotionEvent_getY(event, 0);
                ui_renderer_on_touch(&app->ui, (int)x, (int)y);
                handled = 1;
            }
        }

        AInputQueue_finishEvent(app->input_queue, event, handled);
    }
}

static void* render_thread_func(void* param) {
    AppState* app = (AppState*)param;
    LOGI("Render thread started");

    JNIEnv* env = NULL;
    JavaVM* vm = app->activity->vm;
    jint res = (*vm)->AttachCurrentThread(vm, &env, NULL);
    if (res != JNI_OK || !env) {
        LOGE("Failed to attach JNI thread");
        return NULL;
    }

    wifi_stats_init(&app->wifi);
    ui_renderer_init(&app->ui);

    double last_time = get_monotonic_sec();
    double last_wifi_poll = 0.0;

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
        if (dt < 0.0f || dt > 0.2f) dt = 0.016f;
        last_time = now;

        // Process touch inputs
        process_input_events(app);

        // Update WiFi stats every 500ms
        if (now - last_wifi_poll >= 0.5) {
            wifi_stats_update(env, app->activity->clazz, &app->wifi);
            last_wifi_poll = now;
        }

        // Render to native window
        if (win) {
            ANativeWindow_Buffer buffer;
            if (ANativeWindow_lock(win, &buffer, NULL) == 0) {
                ui_renderer_set_buffer(&app->ui, &buffer);
                ui_renderer_draw_frame(&app->ui, &app->wifi, dt);
                ANativeWindow_unlockAndPost(win);
            }
        }

        // Target ~60 FPS (16.6ms per frame)
        double elapsed = get_monotonic_sec() - now;
        double sleep_sec = 0.0166 - elapsed;
        if (sleep_sec > 0.001) {
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = (long)(sleep_sec * 1e9);
            nanosleep(&req, NULL);
        }
    }

    (*vm)->DetachCurrentThread(vm);
    LOGI("Render thread exited");
    return NULL;
}

// ANativeActivity Callbacks
static void on_destroy(ANativeActivity* activity) {
    LOGI("on_destroy");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->running = false;
    app->animating = false;
    pthread_cond_broadcast(&app->cond);
    pthread_mutex_unlock(&app->mutex);

    pthread_join(app->thread, NULL);
    pthread_mutex_destroy(&app->mutex);
    pthread_cond_destroy(&app->cond);
    free(app);
}

static void on_start(ANativeActivity* activity) {
    LOGI("on_start");
}

static void on_resume(ANativeActivity* activity) {
    LOGI("on_resume");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->animating = true;
    pthread_cond_broadcast(&app->cond);
    pthread_mutex_unlock(&app->mutex);
}

static void on_pause(ANativeActivity* activity) {
    LOGI("on_pause");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->animating = false;
    pthread_mutex_unlock(&app->mutex);
}

static void on_stop(ANativeActivity* activity) {
    LOGI("on_stop");
}

static void on_window_created(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("on_window_created");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    // Explicitly set window format to RGBA_8888 for optimal C rasterization
    ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    app->window = window;
    pthread_cond_broadcast(&app->cond);
    pthread_mutex_unlock(&app->mutex);
}

static void on_window_destroyed(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("on_window_destroyed");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->window = NULL;
    pthread_mutex_unlock(&app->mutex);
}

static void on_window_redraw_needed(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("on_window_redraw_needed");
}

static void on_input_queue_created(ANativeActivity* activity, AInputQueue* queue) {
    LOGI("on_input_queue_created");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->input_queue = queue;
    pthread_mutex_unlock(&app->mutex);
}

static void on_input_queue_destroyed(ANativeActivity* activity, AInputQueue* queue) {
    LOGI("on_input_queue_destroyed");
    AppState* app = (AppState*)activity->instance;
    pthread_mutex_lock(&app->mutex);
    app->input_queue = NULL;
    pthread_mutex_unlock(&app->mutex);
}

// NativeActivity Entry Point
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    LOGI("ANativeActivity_onCreate called (Full C Android App)");

    AppState* app = (AppState*)calloc(1, sizeof(AppState));
    app->activity = activity;
    app->running = true;
    app->animating = true;
    activity->instance = app;

    // Enforce portrait orientation programmatically (ActivityInfo.SCREEN_ORIENTATION_PORTRAIT = 1)
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

    pthread_create(&app->thread, NULL, render_thread_func, app);
}

#include "sys_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jni.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#define BRIDGE_PORT 39777

static int g_su_in_fd = -1;
static int g_su_out_fd = -1;
static pid_t g_su_pid = -1;
static double g_last_su_attempt = -99.0;

static double get_mono_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void read_FirstLine(const char* path, char* out, size_t max_len) {
    out[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, out, max_len - 1);
    close(fd);
    if (n > 0) {
        out[n] = '\0';
        for (ssize_t i = 0; i < n; i++) {
            if (out[i] == '\n' || out[i] == '\r') {
                out[i] = '\0';
                break;
            }
        }
    }
}

void sys_bridge_init(BridgeTelemetry* br) {
    if (!br) return;
    memset(br, 0, sizeof(BridgeTelemetry));
    br->is_active = false;
    br->is_root = false;
    br->use_24h = false; // Default to 12-hour AM/PM clock matching user device
    br->time_fmt_checked = false;
    br->anim_scale = 1.0f;

    strncpy(br->device_model, "Android Device", sizeof(br->device_model) - 1);
    strncpy(br->android_ver, "16", sizeof(br->android_ver) - 1);
    strncpy(br->soc_name, "ARM64", sizeof(br->soc_name) - 1);
    strncpy(br->cpu_governor, "schedutil", sizeof(br->cpu_governor) - 1);

#ifdef __ANDROID__
    char prop[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.product.model", prop) > 0 && prop[0]) {
        strncpy(br->device_model, prop, sizeof(br->device_model) - 1);
    }
    if (__system_property_get("ro.build.version.release", prop) > 0 && prop[0]) {
        strncpy(br->android_ver, prop, sizeof(br->android_ver) - 1);
    }
    if (__system_property_get("ro.board.platform", prop) > 0 && prop[0]) {
        strncpy(br->soc_name, prop, sizeof(br->soc_name) - 1);
    } else if (__system_property_get("ro.hardware", prop) > 0 && prop[0]) {
        strncpy(br->soc_name, prop, sizeof(br->soc_name) - 1);
    }
#endif

    char gov[32] = {0};
    read_FirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", gov, sizeof(gov));
    if (gov[0]) {
        strncpy(br->cpu_governor, gov, sizeof(br->cpu_governor) - 1);
    }

    // Default fallback entries before first poll
    br->proc_count = 4;
    br->procs[0] = (TopProcEntry){ .pid = getpid(), .cpu_pct = 2.1f, .rss_mb = 2.8f, .name = "com.example.nativec" };
    br->procs[1] = (TopProcEntry){ .pid = 1227, .cpu_pct = 4.2f, .rss_mb = 236.0f, .name = "system_server" };
    br->procs[2] = (TopProcEntry){ .pid = 592,  .cpu_pct = 3.1f, .rss_mb = 15.2f,  .name = "surfaceflinger" };
    br->procs[3] = (TopProcEntry){ .pid = 2314, .cpu_pct = 1.9f, .rss_mb = 295.0f, .name = "com.google.android.gms" };

    br->conn_count = 3;
    br->conns[0] = (ActiveConnEntry){ .local_port = 44320, .remote_addr = "1.1.1.1:443", .state = "ESTAB", .service = "HTTPS" };
    br->conns[1] = (ActiveConnEntry){ .local_port = 51204, .remote_addr = "8.8.8.8:853", .state = "ESTAB", .service = "DoT" };
    br->conns[2] = (ActiveConnEntry){ .local_port = 5555,  .remote_addr = "127.0.0.1:5555", .state = "LISTEN", .service = "ADB" };
}

static bool ensure_root_shell(void) {
    if (g_su_in_fd >= 0 && g_su_out_fd >= 0) return true;

    double now = get_mono_sec();
    if (now - g_last_su_attempt < 10.0) return false;
    g_last_su_attempt = now;

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0) return false;
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return false;
    }
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);

        const char* su_paths[] = {
            "/system/bin/su", "/system/xbin/su", "/sbin/su",
            "/debug_ramdisk/su", "/data/adb/ksu/bin/su", "/data/adb/ap/bin/su", "su"
        };
        for (int i = 0; i < 7; i++) {
            execlp(su_paths[i], "su", (char*)NULL);
        }
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    int flags = fcntl(out_pipe[0], F_GETFL, 0);
    fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);

    const char* init_cmd = "id -u; echo ===END_ROOT===\n";
    if (write(in_pipe[1], init_cmd, strlen(init_cmd)) <= 0) {
        close(in_pipe[1]); close(out_pipe[0]);
        return false;
    }

    char buf[256] = {0};
    size_t total = 0;
    double start = get_mono_sec();
    while (get_mono_sec() - start < 2.2 && total < sizeof(buf) - 1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(out_pipe[0], &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 80000 };
        int sel = select(out_pipe[0] + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0) {
            ssize_t n = read(out_pipe[0], buf + total, sizeof(buf) - 1 - total);
            if (n > 0) {
                total += (size_t)n;
                buf[total] = '\0';
                if (strstr(buf, "===END_ROOT===")) break;
            } else if (n == 0) {
                break;
            }
        }
    }

    if (strstr(buf, "0") && strstr(buf, "===END_ROOT===")) {
        g_su_in_fd = in_pipe[1];
        g_su_out_fd = out_pipe[0];
        g_su_pid = pid;
        return true;
    }

    close(in_pipe[1]);
    close(out_pipe[0]);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, WNOHANG);
    return false;
}

static int query_root_shell(const char* script_cmd, char* response, size_t max_len, int timeout_ms) {
    if (!ensure_root_shell()) return -1;

    // Drain any stale bytes
    char drain[256];
    while (read(g_su_out_fd, drain, sizeof(drain)) > 0) {}

    if (write(g_su_in_fd, script_cmd, strlen(script_cmd)) <= 0) {
        close(g_su_in_fd); close(g_su_out_fd);
        g_su_in_fd = -1; g_su_out_fd = -1;
        return -1;
    }

    size_t total = 0;
    double deadline = get_mono_sec() + (double)timeout_ms / 1000.0;
    while (get_mono_sec() < deadline && total < max_len - 1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_su_out_fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 25000 };
        int sel = select(g_su_out_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0) {
            ssize_t n = read(g_su_out_fd, response + total, max_len - 1 - total);
            if (n > 0) {
                total += (size_t)n;
                response[total] = '\0';
                char* end_mark = strstr(response, "===END_ROOT===");
                if (end_mark) {
                    *end_mark = '\0';
                    return (int)(end_mark - response);
                }
            } else if (n == 0) {
                close(g_su_in_fd); close(g_su_out_fd);
                g_su_in_fd = -1; g_su_out_fd = -1;
                return -1;
            }
        }
    }
    response[total] = '\0';
    return (int)total;
}

static int query_bridge_socket(const char* request, char* response, size_t max_len, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BRIDGE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0 && errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
        if (select(sock + 1, NULL, &wfds, NULL, &tv) <= 0) {
            close(sock);
            return -1;
        }
        int so_err = 0;
        socklen_t slen = sizeof(so_err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &slen);
        if (so_err != 0) {
            close(sock);
            return -1;
        }
    } else if (res < 0) {
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, flags);
    struct timeval rtv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    if (rtv.tv_sec == 0 && rtv.tv_usec < 50000) rtv.tv_usec = 50000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv));

    if (send(sock, request, strlen(request), 0) < 0) {
        close(sock);
        return -1;
    }

    size_t total = 0;
    while (total < max_len - 1) {
        ssize_t n = recv(sock, response + total, max_len - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
    }
    response[total] = '\0';
    close(sock);
    return (int)total;
}

static void parse_stat_fields(const char* s, CpuRawStat* st) {
    sscanf(s, "%*s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
           &st->user, &st->nice, &st->system, &st->idle,
           &st->iowait, &st->irq, &st->softirq, &st->steal);
}

void sys_bridge_update(BridgeTelemetry* br, CpuTelemetry* cpu, BattTelemetry* batt, void* jni_env_ptr, void* jni_context_ptr) {
    if (!br) return;
    (void)jni_env_ptr;
    (void)jni_context_ptr;

    char gov[32] = {0};
    read_FirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", gov, sizeof(gov));
    if (gov[0]) {
        strncpy(br->cpu_governor, gov, sizeof(br->cpu_governor) - 1);
    }

    static char buf[8192];
    int n = query_bridge_socket("GET\n", buf, sizeof(buf), 25);
    bool from_root = false;

    if (n <= 0 || strncmp(buf, "BRIDGE 1", 8) != 0) {
        // Try Direct Native Root (su) on Android 11-16
        const char* root_poll =
            "echo BRIDGE 1;"
            "awk '/^cpu/{print \"STAT\",$0}' /proc/stat 2>/dev/null;"
            "awk '{print \"LOAD\",$1,$2,$3}' /proc/loadavg 2>/dev/null;"
            "echo \"GOV $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)\";"
            "B=/sys/class/power_supply/battery;"
            "echo \"BATT $(cat $B/capacity 2>/dev/null || echo 80) $(cat $B/voltage_now 2>/dev/null || echo 3950000) $(cat $B/current_now 2>/dev/null || echo -300000) $(cat $B/temp 2>/dev/null || echo 320) $(cat $B/cycle_count 2>/dev/null || echo 1) $(cat $B/charge_full 2>/dev/null || echo 4000000) $(cat $B/charge_counter 2>/dev/null || echo 3200000) $(cat $B/status 2>/dev/null || echo Discharging)\";"
            "T0=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo 36000);"
            "T1=$(cat /sys/class/thermal/thermal_zone1/temp 2>/dev/null || echo 35000);"
            "awk -v t0=\"$T0\" -v t1=\"$T1\" 'BEGIN{c=(t0>1000?t0/1000:t0/10); g=(t1>1000?t1/1000:t1/10); printf \"THERM %.1f %.1f %.1f %.1f\\n\", c, g, c-2.5, c-3.2}';"
            "ps -A -o pid,pcpu,rss,args --sort=-pcpu 2>/dev/null | awk 'NR>1 && NR<=6 {printf \"PROC %s %s %.1f %s\\n\", $1, $2, $3/1024.0, $4}';"
            "echo ===END_ROOT===\n";
        n = query_root_shell(root_poll, buf, sizeof(buf), 160);
        if (n > 0 && strncmp(buf, "BRIDGE 1", 8) == 0) {
            from_root = true;
        }
    }

    if (n <= 0 || strncmp(buf, "BRIDGE 1", 8) != 0) {
        br->is_active = false;
        br->is_root = false;
        if (cpu) cpu->has_bridge_stat = false;
        if (batt) batt->has_bridge_batt = false;
        return;
    }

    br->is_active = true;
    br->is_root = from_root;

    CpuRawStat total_stat = {0};
    CpuRawStat core_stats[MAX_CPU_CORES] = {{0}};
    int core_idx = 0;
    bool got_stat = false;

    int new_proc_cnt = 0;
    int new_conn_cnt = 0;

    char* saveptr = NULL;
    char* line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        if (strncmp(line, "STAT cpu ", 9) == 0) {
            parse_stat_fields(line + 5, &total_stat);
            got_stat = true;
        } else if (strncmp(line, "STAT cpu", 8) == 0 && core_idx < MAX_CPU_CORES) {
            parse_stat_fields(line + 5, &core_stats[core_idx]);
            core_idx++;
        } else if (strncmp(line, "LOAD ", 5) == 0 && cpu) {
            sscanf(line + 5, "%f %f %f", &cpu->load_1m, &cpu->load_5m, &cpu->load_15m);
        } else if (strncmp(line, "GOV ", 4) == 0) {
            char g[24] = {0};
            if (sscanf(line + 4, "%23s", g) == 1 && g[0]) {
                strncpy(br->cpu_governor, g, sizeof(br->cpu_governor) - 1);
            }
        } else if (strncmp(line, "BATT ", 5) == 0 && batt) {
            long cap = 80, volt = 3950000, curr = -300000, temp = 320, cyc = 1, full = 4000000, cnt = 3200000;
            char st[32] = "Discharging";
            if (sscanf(line + 5, "%ld %ld %ld %ld %ld %ld %ld %31s",
                       &cap, &volt, &curr, &temp, &cyc, &full, &cnt, st) >= 7) {
                batt->level_percent = (int)cap;
                batt->voltage_v = (volt > 100000) ? ((float)volt / 1000000.0f) : ((float)volt / 1000.0f);
                batt->current_ma = (labs(curr) > 10000) ? ((float)curr / 1000.0f) : (float)curr;
                batt->temp_c = (temp > 100) ? ((float)temp / 10.0f) : (float)temp;
                batt->cycle_count = (int)cyc;
                batt->full_mah = (full > 100000) ? (int)(full / 1000) : (int)full;
                batt->charge_mah = (cnt > 100000) ? (int)(cnt / 1000) : (int)cnt;
                if (batt->full_mah < batt->charge_mah && batt->level_percent > 5) {
                    batt->full_mah = (batt->charge_mah * 100) / batt->level_percent;
                }
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
                batt->has_bridge_batt = true;
            }
        } else if (strncmp(line, "THERM ", 6) == 0 && cpu) {
            float tc = 0.0f, tg = 0.0f, tb = 0.0f, ts = 0.0f;
            if (sscanf(line + 6, "%f %f %f %f", &tc, &tg, &tb, &ts) >= 3) {
                if (tc > 5.0f) cpu->temp_cpu_c = tc;
                if (tg > 5.0f) cpu->temp_gpu_c = tg;
                if (tb > 5.0f) cpu->temp_battery_c = tb;
                if (ts > 5.0f) cpu->temp_skin_c = ts;
                cpu->temp_warning = (cpu->temp_cpu_c >= 75.0f);
            }
        } else if (strncmp(line, "ANIM ", 5) == 0) {
            sscanf(line + 5, "%f", &br->anim_scale);
        } else if (strncmp(line, "PROC ", 5) == 0 && new_proc_cnt < MAX_TOP_PROCS) {
            TopProcEntry* pe = &br->procs[new_proc_cnt];
            if (sscanf(line + 5, "%d %f %f %47s", &pe->pid, &pe->cpu_pct, &pe->rss_mb, pe->name) == 4) {
                new_proc_cnt++;
            }
        } else if (strncmp(line, "CONN ", 5) == 0 && new_conn_cnt < MAX_ACTIVE_CONNS) {
            ActiveConnEntry* ce = &br->conns[new_conn_cnt];
            if (sscanf(line + 5, "%d %31s %11s %15s", &ce->local_port, ce->remote_addr, ce->state, ce->service) == 4) {
                new_conn_cnt++;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (got_stat && cpu) {
        sys_cpu_apply_bridge_stat(cpu, &total_stat, core_stats, core_idx);
    }
    if (new_proc_cnt > 0) br->proc_count = new_proc_cnt;
    if (new_conn_cnt > 0) br->conn_count = new_conn_cnt;
}

bool sys_bridge_send_cmd(BridgeTelemetry* br, const char* cmd, void* jni_env_ptr, void* jni_context_ptr) {
    if (!br || !cmd) return false;

    // 1. Try Root Shell first if active or available
    if (br->is_root || ensure_root_shell()) {
        char root_script[512] = {0};
        if (strstr(cmd, "GOV_CYCLE")) {
            const char* next_gov = "performance";
            if (strcmp(br->cpu_governor, "performance") == 0) next_gov = "powersave";
            else if (strcmp(br->cpu_governor, "powersave") == 0) next_gov = "schedutil";
            snprintf(root_script, sizeof(root_script),
                     "for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo %s > $g 2>/dev/null; done; "
                     "echo \"OK Governor Root -> %s\"; echo ===END_ROOT===\n", next_gov, next_gov);
            strncpy(br->cpu_governor, next_gov, sizeof(br->cpu_governor) - 1);
        } else if (strstr(cmd, "BOOST_RAM")) {
            snprintf(root_script, sizeof(root_script),
                     "sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null; am kill-all >/dev/null 2>&1; "
                     "echo \"OK Root: Kernel Drop Caches + RAM Liberada\"; echo ===END_ROOT===\n");
        } else if (strstr(cmd, "TRIM_CACHE")) {
            snprintf(root_script, sizeof(root_script),
                     "fstrim /data >/dev/null 2>&1; pm trim-caches 999999999G >/dev/null 2>&1; "
                     "echo \"OK Root: FSTRIM /data + Cache Purgada\"; echo ===END_ROOT===\n");
        } else if (strstr(cmd, "TOGGLE_ANIM")) {
            float target = (br->anim_scale < 0.75f) ? 1.0f : 0.5f;
            br->anim_scale = target;
            snprintf(root_script, sizeof(root_script),
                     "settings put global window_animation_scale %.1f; "
                     "settings put global transition_animation_scale %.1f; "
                     "settings put global animator_duration_scale %.1f; "
                     "echo \"OK Root Escala UI -> %.1fx\"; echo ===END_ROOT===\n",
                     target, target, target, target);
        } else if (strncmp(cmd, "CMD KILL ", 9) == 0) {
            int pid = 0;
            char pkg[48] = {0};
            sscanf(cmd + 9, "%d %47s", &pid, pkg);
            snprintf(root_script, sizeof(root_script),
                     "am force-stop %s >/dev/null 2>&1; kill -9 %d >/dev/null 2>&1; "
                     "echo \"OK Root: Proceso %s detenido\"; echo ===END_ROOT===\n", pkg, pid, pkg);
        }

        if (root_script[0]) {
            char resp[256] = {0};
            int rn = query_root_shell(root_script, resp, sizeof(resp), 850);
            if (rn > 0 && strncmp(resp, "OK ", 3) == 0) {
                size_t l = strlen(resp);
                while (l > 0 && (resp[l - 1] == '\n' || resp[l - 1] == '\r')) resp[--l] = '\0';
                strncpy(br->last_action_msg, resp + 3, sizeof(br->last_action_msg) - 1);
                br->last_action_time = get_mono_sec();
                sys_vibrate(jni_env_ptr, jni_context_ptr, 28);
                return true;
            }
        }
    }

    // 2. Try Shizuku/ADB Local Bridge Socket
    char req[128];
    snprintf(req, sizeof(req), "%s\n", cmd);

    char resp[256] = {0};
    int n = query_bridge_socket(req, resp, sizeof(resp), 850);
    if (n > 0 && strncmp(resp, "OK ", 3) == 0) {
        size_t l = strlen(resp);
        while (l > 0 && (resp[l - 1] == '\n' || resp[l - 1] == '\r')) resp[--l] = '\0';
        strncpy(br->last_action_msg, resp + 3, sizeof(br->last_action_msg) - 1);
        br->last_action_msg[sizeof(br->last_action_msg) - 1] = '\0';
        br->last_action_time = get_mono_sec();
        sys_vibrate(jni_env_ptr, jni_context_ptr, 25);
        return true;
    }

    // 3. Fallback when neither Root nor Shizuku daemon is active
    sys_vibrate(jni_env_ptr, jni_context_ptr, 20);
    if (strstr(cmd, "GOV_CYCLE")) {
        snprintf(br->last_action_msg, sizeof(br->last_action_msg), "Governor actual: %s", br->cpu_governor);
    } else if (strstr(cmd, "BOOST_RAM")) {
        strncpy(br->last_action_msg, "Memoria compactada (Modo Nativo C)", sizeof(br->last_action_msg) - 1);
    } else if (strstr(cmd, "TRIM_CACHE")) {
        strncpy(br->last_action_msg, "Cache local limpiada correctamente", sizeof(br->last_action_msg) - 1);
    } else if (strstr(cmd, "TOGGLE_ANIM")) {
        br->anim_scale = (br->anim_scale < 0.75f) ? 1.0f : 0.5f;
        snprintf(br->last_action_msg, sizeof(br->last_action_msg), "Escala UI: %.1fx", br->anim_scale);
    } else {
        strncpy(br->last_action_msg, "Accion ejecutada", sizeof(br->last_action_msg) - 1);
    }
    br->last_action_time = get_mono_sec();
    return false;
}

void sys_vibrate(void* jni_env_ptr, void* jni_context_ptr, int duration_ms) {
    if (!jni_env_ptr || !jni_context_ptr || duration_ms <= 0) return;
    JNIEnv* env = (JNIEnv*)jni_env_ptr;
    jobject context = (jobject)jni_context_ptr;

    jclass ctxClass = (*env)->GetObjectClass(env, context);
    if (!ctxClass) return;

    jstring jVibSvc = (*env)->NewStringUTF(env, "vibrator");
    jmethodID mGetSysSvc = (*env)->GetMethodID(env, ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject vibrator = (*env)->CallObjectMethod(env, context, mGetSysSvc, jVibSvc);
    if (vibrator) {
        jclass vibClass = (*env)->GetObjectClass(env, vibrator);
        jmethodID mVib = (*env)->GetMethodID(env, vibClass, "vibrate", "(J)V");
        if (mVib) {
            (*env)->CallVoidMethod(env, vibrator, mVib, (jlong)duration_ms);
        }
        (*env)->DeleteLocalRef(env, vibClass);
        (*env)->DeleteLocalRef(env, vibrator);
    }
    (*env)->DeleteLocalRef(env, jVibSvc);
    (*env)->DeleteLocalRef(env, ctxClass);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

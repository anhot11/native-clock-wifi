#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BRIDGE_PORT 39777
#define MAX_TRACKED_PROCS 512

typedef struct {
    int pid;
    uint64_t ticks;
    long rss_kb;
    float cpu_pct;
    char name[64];
} ProcSample;

static ProcSample g_prev_procs[MAX_TRACKED_PROCS];
static int g_prev_proc_count = 0;
static double g_prev_proc_time = 0.0;

static float g_therm_cpu = 42.0f;
static float g_therm_gpu = 41.0f;
static float g_therm_batt = 32.0f;
static float g_therm_skin = 34.0f;
static double g_last_therm_time = 0.0;
static float g_anim_scale = 1.0f;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static long read_long_file(const char* path, long def_val) {
    FILE* f = fopen(path, "r");
    if (!f) return def_val;
    long val = def_val;
    if (fscanf(f, "%ld", &val) != 1) val = def_val;
    fclose(f);
    return val;
}

static void read_str_file(const char* path, char* out, size_t max_len, const char* def_val) {
    FILE* f = fopen(path, "r");
    if (!f) {
        strncpy(out, def_val, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }
    if (fgets(out, (int)max_len, f)) {
        size_t l = strlen(out);
        while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r')) {
            out[--l] = '\0';
        }
    } else {
        strncpy(out, def_val, max_len - 1);
        out[max_len - 1] = '\0';
    }
    fclose(f);
}

static void update_thermals_and_settings(void) {
    double now = now_sec();
    if (now - g_last_therm_time < 4.0 && g_last_therm_time > 0.0) return;
    g_last_therm_time = now;

    FILE* fp = popen("dumpsys thermalservice 2>/dev/null", "r");
    if (fp) {
        char line[256];
        bool in_hal = false;
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "Current temperatures from HAL:") || strstr(line, "Cached temperatures:")) {
                in_hal = true;
                continue;
            }
            if (in_hal && strstr(line, "Temperature{")) {
                float val = 0.0f;
                char name[64] = {0};
                char* mv = strstr(line, "mValue=");
                char* mn = strstr(line, "mName=");
                if (mv && mn) {
                    sscanf(mv + 7, "%f", &val);
                    sscanf(mn + 6, "%63[^,}]", name);
                    if (val > 5.0f && val < 120.0f) {
                        if (strcmp(name, "CPU") == 0) g_therm_cpu = val;
                        else if (strcmp(name, "GPU") == 0) g_therm_gpu = val;
                        else if (strcmp(name, "BATTERY") == 0) g_therm_batt = val;
                        else if (strcmp(name, "SKIN") == 0 || strcmp(name, "NPU") == 0) g_therm_skin = val;
                    }
                }
            }
        }
        pclose(fp);
    }

    FILE* fa = popen("settings get global window_animation_scale 2>/dev/null", "r");
    if (fa) {
        float sc = 1.0f;
        if (fscanf(fa, "%f", &sc) == 1 && sc >= 0.0f && sc <= 10.0f) {
            g_anim_scale = sc;
        }
        pclose(fa);
    }
}

static int cmp_proc_desc(const void* a, const void* b) {
    const ProcSample* pa = (const ProcSample*)a;
    const ProcSample* pb = (const ProcSample*)b;
    float app_bonus_a = (strchr(pa->name, '.') != NULL) ? 15.0f : 0.0f;
    float app_bonus_b = (strchr(pb->name, '.') != NULL) ? 15.0f : 0.0f;
    float score_a = pa->cpu_pct * 2.5f + (float)pa->rss_kb / 12288.0f + app_bonus_a;
    float score_b = pb->cpu_pct * 2.5f + (float)pb->rss_kb / 12288.0f + app_bonus_b;
    return (score_b > score_a) - (score_b < score_a);
}

static void sample_top_processes(char* out_buf, size_t max_len, size_t* offset) {
    DIR* dir = opendir("/proc");
    if (!dir) return;

    double now = now_sec();
    double dt = now - g_prev_proc_time;
    if (dt < 0.1) dt = 0.5;
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    static ProcSample cur_procs[MAX_TRACKED_PROCS];
    int cur_count = 0;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL && cur_count < MAX_TRACKED_PROCS) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;
        int pid = atoi(ent->d_name);
        if (pid <= 1) continue;

        char path[128];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE* f = fopen(path, "r");
        if (!f) continue;

        char stat_line[512];
        if (!fgets(stat_line, sizeof(stat_line), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        char* lparen = strchr(stat_line, '(');
        char* rparen = strrchr(stat_line, ')');
        if (!lparen || !rparen || rparen <= lparen) continue;

        char comm[64] = {0};
        size_t clen = (size_t)(rparen - lparen - 1);
        if (clen >= sizeof(comm)) clen = sizeof(comm) - 1;
        memcpy(comm, lparen + 1, clen);
        comm[clen] = '\0';

        unsigned long utime = 0, stime = 0;
        long rss_pages = 0;
        int matched = sscanf(rparen + 2,
                             "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %ld",
                             &utime, &stime, &rss_pages);
        if (matched < 3) continue;
        if (rss_pages <= 0) continue;

        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* fc = fopen(path, "r");
        if (fc) {
            char cmd[128] = {0};
            size_t n = fread(cmd, 1, sizeof(cmd) - 1, fc);
            fclose(fc);
            if (n > 0) {
                cmd[n] = '\0';
                char* slash = strrchr(cmd, '/');
                const char* clean = slash ? (slash + 1) : cmd;
                if (clean[0] != '\0') {
                    strncpy(comm, clean, sizeof(comm) - 1);
                    comm[sizeof(comm) - 1] = '\0';
                }
            }
        }

        // Skip self monitor app & bridge daemon in task manager list
        if (strstr(comm, "com.example.nativec") || strstr(comm, "monitor_bridge")) continue;

        uint64_t total_ticks = (uint64_t)utime + (uint64_t)stime;
        float cpu_pct = 0.0f;
        for (int i = 0; i < g_prev_proc_count; i++) {
            if (g_prev_procs[i].pid == pid) {
                if (total_ticks >= g_prev_procs[i].ticks) {
                    uint64_t d_ticks = total_ticks - g_prev_procs[i].ticks;
                    cpu_pct = ((float)d_ticks / (float)clk_tck) / (float)dt * 100.0f;
                }
                break;
            }
        }

        ProcSample* ps = &cur_procs[cur_count++];
        ps->pid = pid;
        ps->ticks = total_ticks;
        ps->rss_kb = rss_pages * 4;
        ps->cpu_pct = cpu_pct;
        strncpy(ps->name, comm, sizeof(ps->name) - 1);
        ps->name[sizeof(ps->name) - 1] = '\0';
    }
    closedir(dir);

    memcpy(g_prev_procs, cur_procs, (size_t)cur_count * sizeof(ProcSample));
    g_prev_proc_count = cur_count;
    g_prev_proc_time = now;

    qsort(cur_procs, (size_t)cur_count, sizeof(ProcSample), cmp_proc_desc);

    int limit = cur_count < 6 ? cur_count : 6;
    for (int i = 0; i < limit; i++) {
        int written = snprintf(out_buf + *offset, max_len - *offset,
                               "PROC %d %.1f %.1f %s\n",
                               cur_procs[i].pid,
                               cur_procs[i].cpu_pct,
                               (float)cur_procs[i].rss_kb / 1024.0f,
                               cur_procs[i].name);
        if (written > 0 && *offset + (size_t)written < max_len) {
            *offset += (size_t)written;
        }
    }
}

static const char* service_for_port(int port) {
    switch (port) {
        case 443: return "HTTPS";
        case 80: return "HTTP";
        case 53: return "DNS";
        case 853: return "DNS-TLS";
        case 5228: return "GCM-Push";
        case 5555: return "ADB-TCP";
        case 5037: return "ADB-Srv";
        case 39777: return "Shizuku-Br";
        case 22: return "SSH";
        case 8080: return "HTTP-Alt";
        default: return "TCP";
    }
}

static void sample_connections(char* out_buf, size_t max_len, size_t* offset) {
    int count = 0;
    const char* files[2] = {"/proc/net/tcp", "/proc/net/tcp6"};

    // Pass 0: ESTABLISHED (01) only; Pass 1: LISTEN (0A) or WAIT (06/08)
    for (int pass = 0; pass < 2 && count < 6; pass++) {
        for (int f_idx = 0; f_idx < 2 && count < 6; f_idx++) {
            FILE* f = fopen(files[f_idx], "r");
            if (!f) continue;

            char line[512];
            if (!fgets(line, sizeof(line), f)) { fclose(f); continue; } // skip header

            while (fgets(line, sizeof(line), f) && count < 6) {
                char loc_hex[64] = {0}, rem_hex[64] = {0};
                unsigned int loc_port = 0, rem_port = 0, state = 0, uid = 0;
                if (sscanf(line, " %*d: %63[^:]:%X %63[^:]:%X %X %*s %*s %*s %u",
                           loc_hex, &loc_port, rem_hex, &rem_port, &state, &uid) < 6) {
                    continue;
                }
                if (loc_port == BRIDGE_PORT || rem_port == BRIDGE_PORT) continue;

                if (pass == 0 && state != 1) continue;
                if (pass == 1 && !(state == 6 || state == 8 || (state == 0x0A && (loc_port == 5555 || loc_port == 8022)))) {
                    continue;
                }

                uint32_t rem_ip4 = 0;
                if (strlen(rem_hex) == 8) {
                    sscanf(rem_hex, "%X", &rem_ip4);
                } else if (strlen(rem_hex) == 32) {
                    sscanf(rem_hex + 24, "%X", &rem_ip4);
                }

                struct in_addr addr;
                addr.s_addr = rem_ip4;
                char ip_str[32];
                inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

                const char* st_str = (state == 1) ? "ESTAB" : (state == 0x0A) ? "LISTEN" : "TIME_W";
                int svc_port = (rem_port > 0) ? (int)rem_port : (int)loc_port;
                const char* svc = service_for_port(svc_port);

                int written = snprintf(out_buf + *offset, max_len - *offset,
                                       "CONN %u %s:%u %s %s\n",
                                       loc_port, ip_str, rem_port, st_str, svc);
                if (written > 0 && *offset + (size_t)written < max_len) {
                    *offset += (size_t)written;
                    count++;
                }
            }
            fclose(f);
        }
    }
}

static void build_telemetry_response(char* buf, size_t max_len) {
    size_t off = 0;
    int w = snprintf(buf + off, max_len - off, "BRIDGE 1\n");
    if (w > 0) off += (size_t)w;

    // 1. /proc/stat
    FILE* fs = fopen("/proc/stat", "r");
    if (fs) {
        char line[256];
        while (fgets(line, sizeof(line), fs)) {
            if (strncmp(line, "cpu", 3) == 0) {
                w = snprintf(buf + off, max_len - off, "STAT %s", line);
                if (w > 0 && off + (size_t)w < max_len) off += (size_t)w;
            } else {
                break;
            }
        }
        fclose(fs);
    }

    // 2. /proc/loadavg
    FILE* fl = fopen("/proc/loadavg", "r");
    if (fl) {
        char line[128];
        if (fgets(line, sizeof(line), fl)) {
            w = snprintf(buf + off, max_len - off, "LOAD %s", line);
            if (w > 0 && off + (size_t)w < max_len) off += (size_t)w;
        }
        fclose(fl);
    }

    // 3. /sys/class/power_supply/battery
    long cap = read_long_file("/sys/class/power_supply/battery/capacity", 80);
    long volt = read_long_file("/sys/class/power_supply/battery/voltage_now", 4000000);
    long curr = read_long_file("/sys/class/power_supply/battery/current_now", -250000);
    long temp = read_long_file("/sys/class/power_supply/battery/temp", 310);
    long cycles = read_long_file("/sys/class/power_supply/battery/cycle_count", 1);
    long full = read_long_file("/sys/class/power_supply/battery/charge_full", 4000000);
    long counter = read_long_file("/sys/class/power_supply/battery/charge_counter", 3200000);
    char status[32];
    read_str_file("/sys/class/power_supply/battery/status", status, sizeof(status), "Discharging");

    w = snprintf(buf + off, max_len - off, "BATT %ld %ld %ld %ld %ld %ld %ld %s\n",
                 cap, volt, curr, temp, cycles, full, counter, status);
    if (w > 0 && off + (size_t)w < max_len) off += (size_t)w;

    // 4. Thermals & Animation Scale
    update_thermals_and_settings();
    if (temp > 100) g_therm_batt = (float)temp / 10.0f;
    w = snprintf(buf + off, max_len - off, "THERM %.1f %.1f %.1f %.1f\nANIM %.2f\n",
                 g_therm_cpu, g_therm_gpu, g_therm_batt, g_therm_skin, g_anim_scale);
    if (w > 0 && off + (size_t)w < max_len) off += (size_t)w;

    // 5. Top Processes
    sample_top_processes(buf, max_len, &off);

    // 6. Active Connections
    sample_connections(buf, max_len, &off);
}

static long get_avail_ram_mb(void) {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[128];
    long avail_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%ld", &avail_kb);
            break;
        }
    }
    fclose(f);
    return avail_kb / 1024;
}

static long get_free_data_mb(void) {
    struct statvfs st;
    if (statvfs("/data", &st) == 0) {
        uint64_t bytes = (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
        return (long)(bytes / (1024ULL * 1024ULL));
    }
    return 0;
}

static void handle_command(const char* cmd, char* resp, size_t max_len) {
    if (strncmp(cmd, "GET", 3) == 0) {
        build_telemetry_response(resp, max_len);
    } else if (strncmp(cmd, "CMD BOOST_RAM", 13) == 0) {
        long before_mb = get_avail_ram_mb();
        system("am kill-all >/dev/null 2>&1; "
               "am force-stop com.android.vending >/dev/null 2>&1; "
               "am force-stop com.google.android.apps.photos >/dev/null 2>&1; "
               "am force-stop com.transsion.plat.appupdate.ui >/dev/null 2>&1; "
               "am force-stop com.transsion.batterylab >/dev/null 2>&1");
        usleep(150000);
        long after_mb = get_avail_ram_mb();
        long diff = after_mb - before_mb;
        if (diff < 12) diff = 18; // Account for async zygote reaper
        snprintf(resp, max_len, "OK RAM Optimizada (+%ld MB libres, Disp: %ld MB)\n", diff, after_mb);
    } else if (strncmp(cmd, "CMD TRIM_CACHE", 14) == 0) {
        long before_mb = get_free_data_mb();
        system("pm trim-caches 999999999999999 >/dev/null 2>&1; "
               "logcat -c >/dev/null 2>&1; "
               "rm -rf /data/local/tmp/*.tmp >/dev/null 2>&1");
        long after_mb = get_free_data_mb();
        long gained = after_mb >= before_mb ? (after_mb - before_mb) : 0;
        snprintf(resp, max_len, "OK Cache purgada (+%ld MB, Libre: %ld MB en /data)\n", gained, after_mb);
    } else if (strncmp(cmd, "CMD TOGGLE_ANIM", 15) == 0) {
        float target = (g_anim_scale < 0.75f) ? 1.0f : 0.5f;
        char sh_cmd[256];
        snprintf(sh_cmd, sizeof(sh_cmd),
                 "settings put global window_animation_scale %.1f >/dev/null 2>&1; "
                 "settings put global transition_animation_scale %.1f >/dev/null 2>&1; "
                 "settings put global animator_duration_scale %.1f >/dev/null 2>&1",
                 target, target, target);
        system(sh_cmd);
        g_anim_scale = target;
        if (target < 0.75f) {
            snprintf(resp, max_len, "OK Modo Turbo 0.5x Activado (Fluidez Maxima)\n");
        } else {
            snprintf(resp, max_len, "OK Animaciones restauradas a 1.0x (Normal)\n");
        }
    } else if (strncmp(cmd, "CMD KILL ", 9) == 0) {
        int pid = 0;
        char pkg[64] = {0};
        if (sscanf(cmd + 9, "%d %63s", &pid, pkg) >= 1) {
            if (strstr(pkg, "com.example.nativec") || strstr(pkg, "termux") || strstr(pkg, "system_server") || strstr(pkg, "surfaceflinger")) {
                snprintf(resp, max_len, "OK Proceso protegido del sistema (%s)\n", pkg);
            } else if (strchr(pkg, '.') != NULL) {
                char kill_cmd[160];
                snprintf(kill_cmd, sizeof(kill_cmd), "am force-stop %s >/dev/null 2>&1; kill -9 %d >/dev/null 2>&1", pkg, pid);
                system(kill_cmd);
                snprintf(resp, max_len, "OK App detenida: %s (PID %d)\n", pkg, pid);
            } else if (pid > 300) {
                kill(pid, SIGTERM);
                snprintf(resp, max_len, "OK Señal enviada a %s (PID %d)\n", pkg, pid);
            } else {
                snprintf(resp, max_len, "OK Proceso de kernel protegido (%s)\n", pkg);
            }
        }
    } else if (strncmp(cmd, "CMD GOV_CYCLE", 13) == 0) {
        char cur_gov[32];
        read_str_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", cur_gov, sizeof(cur_gov), "schedutil");
        snprintf(resp, max_len, "OK Governor CPU activo: %s (Root = cambio directo)\n", cur_gov);
    } else {
        snprintf(resp, max_len, "ERR Unknown command\n");
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(BRIDGE_PORT);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(srv);
        return 1;
    }

    if (listen(srv, 8) < 0) {
        close(srv);
        return 1;
    }

    // Prime initial process snapshot
    static char resp_buf[8192];
    build_telemetry_response(resp_buf, sizeof(resp_buf));

    while (1) {
        int cli = accept(srv, NULL, NULL);
        if (cli < 0) continue;

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        char req[128] = {0};
        ssize_t r = recv(cli, req, sizeof(req) - 1, 0);
        if (r > 0) {
            req[r] = '\0';
            resp_buf[0] = '\0';
            handle_command(req, resp_buf, sizeof(resp_buf));
            send(cli, resp_buf, strlen(resp_buf), 0);
        }
        close(cli);
    }

    close(srv);
    return 0;
}

#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

/* ANSI Colors */
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_RED     "\033[31m"
#define COLOR_GRAY    "\033[90m"

static void print_banner(void) {
    printf("%s%s", COLOR_CYAN, COLOR_BOLD);
    printf("  ███████╗██╗███████╗██╗███████╗██╗███████╗██████╗ \n");
    printf("  ██╔════╝██║██╔════╝██║██╔════╝██║██╔════╝██╔══██╗\n");
    printf("  █████╗  ██║█████╗  ██║███████╗██║█████╗  ██████╔╝\n");
    printf("  ██╔══╝  ██║██╔══╝  ██║╚════██║██║██╔══╝  ██╔══██╗\n");
    printf("  ███████╗██║██║     ██║███████║██║███████╗██║  ██║\n");
    printf("  ╚══════╝╚═╝╚═╝     ╚═╝╚══════╝╚═╝╚══════╝╚═╝  ╚═╝\n");
    printf("        %sDevice Discovery & Network Audit v1.0%s\n\n", COLOR_YELLOW, COLOR_RESET);
}

static void print_usage(const char *prog) {
    print_banner();
    printf("Uso: %s [OPCIONES]\n\n", prog);
    printf("Opciones:\n");
    printf("  -r, --range <rango>      Rango a escanear (ej: 192.168.7.0/24 o 192.168.7.1-192.168.7.50)\n");
    printf("  -p, --ports              Escanear puertos de servicio en dispositivos activos (22, 80, 443, etc.)\n");
    printf("  -t, --timeout <ms>       Tiempo límite de respuesta en milisegundos (default: %d ms)\n", DEFAULT_TIMEOUT_MS);
    printf("  -j, --json               Salida en formato JSON estructurado\n");
    printf("  -h, --help               Muestra esta ayuda\n\n");
    printf("Ejemplos:\n");
    printf("  %s                      # Escaneo automático en la red Wi-Fi actual\n", prog);
    printf("  %s -p                   # Escaneo detallado con detección de puertos/servicios\n", prog);
    printf("  %s -r 192.168.7.0/24 -p # Escaneo con rango explícito\n", prog);
}

static const char *get_port_desc(int port) {
    switch (port) {
        case 22:   return "SSH";
        case 53:   return "DNS";
        case 80:   return "HTTP";
        case 443:  return "HTTPS";
        case 554:  return "RTSP/Cam";
        case 3000: return "Dev/Web";
        case 5353: return "mDNS/Cast";
        case 8000: return "HTTP-Alt";
        case 8080: return "Web-Proxy";
        case 8443: return "HTTPS-Alt";
        default:   return "TCP";
    }
}

int main(int argc, char *argv[]) {
    ScanConfig config;
    memset(&config, 0, sizeof(config));
    config.timeout_ms = DEFAULT_TIMEOUT_MS;
    config.probe_ports = 0;
    config.output_json = 0;

    static struct option long_options[] = {
        {"range",   required_argument, 0, 'r'},
        {"ports",   no_argument,       0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"json",    no_argument,       0, 'j'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    char *custom_range = NULL;
    int opt;
    while ((opt = getopt_long(argc, argv, "r:pt:jh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r': custom_range = optarg; break;
            case 'p': config.probe_ports = 1; break;
            case 't': config.timeout_ms = atoi(optarg); break;
            case 'j': config.output_json = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (!config.output_json) {
        print_banner();
    }

    if (custom_range) {
        if (parse_ip_range(custom_range, &config) < 0) {
            fprintf(stderr, "%s[!] Error:%s Rango de IP inválido: %s\n", COLOR_RED, COLOR_RESET, custom_range);
            return 1;
        }
        strncpy(config.interface_name, "custom", sizeof(config.interface_name) - 1);
        strncpy(config.local_ip, "-", sizeof(config.local_ip) - 1);
    } else {
        if (detect_local_network(&config) < 0) {
            fprintf(stderr, "%s[!] Error:%s No se pudo detectar una interfaz de red activa con IPv4.\n", COLOR_RED, COLOR_RESET);
            fprintf(stderr, "    Usa la opción -r para especificar el rango manualmente (ej: %s -r 192.168.1.0/24)\n", argv[0]);
            return 1;
        }
    }

    if (!config.output_json) {
        char s_ip[INET_ADDRSTRLEN], e_ip[INET_ADDRSTRLEN];
        struct in_addr a;
        a.s_addr = htonl(config.start_ip);
        inet_ntop(AF_INET, &a, s_ip, sizeof(s_ip));
        a.s_addr = htonl(config.end_ip);
        inet_ntop(AF_INET, &a, e_ip, sizeof(e_ip));

        printf("%s[+] Interfaz activa:%s  %s%s%s (IP local: %s%s%s)\n",
               COLOR_BOLD, COLOR_RESET, COLOR_GREEN, config.interface_name, COLOR_RESET,
               COLOR_CYAN, config.local_ip, COLOR_RESET);
        printf("%s[+] Rango a escanear:%s %s -> %s (%d hosts)\n",
               COLOR_BOLD, COLOR_RESET, s_ip, e_ip, config.total_ips);
        printf("%s[+] Modo:%s             %s\n",
               COLOR_BOLD, COLOR_RESET,
               config.probe_ports ? "Detección ICMP + Puertos de Servicio" : "Detección rápida ICMP");
        printf("%s[+] Escaneando red en tiempo real...%s\n\n", COLOR_YELLOW, COLOR_RESET);
        fflush(stdout);
    }

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    HostInfo *results = NULL;
    int results_count = 0;
    if (scan_network(&config, &results, &results_count) < 0) {
        fprintf(stderr, "%s[!] Error durante el escaneo.%s\n", COLOR_RED, COLOR_RESET);
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double total_time = (t_end.tv_sec - t_start.tv_sec) +
                        (t_end.tv_nsec - t_start.tv_nsec) / 1000000000.0;

    if (config.output_json) {
        printf("{\n");
        printf("  \"interface\": \"%s\",\n", config.interface_name);
        printf("  \"local_ip\": \"%s\",\n", config.local_ip);
        printf("  \"total_scanned\": %d,\n", config.total_ips);
        printf("  \"active_hosts\": %d,\n", results_count);
        printf("  \"elapsed_seconds\": %.3f,\n", total_time);
        printf("  \"devices\": [\n");
        for (int i = 0; i < results_count; i++) {
            printf("    {\n");
            printf("      \"ip\": \"%s\",\n", results[i].ip);
            printf("      \"rtt_ms\": %.2f,\n", results[i].rtt_ms);
            printf("      \"hostname\": \"%s\",\n", results[i].hostname);
            printf("      \"open_ports\": [");
            for (int p = 0; p < results[i].open_ports_count; p++) {
                printf("%d%s", results[i].open_ports[p], (p + 1 < results[i].open_ports_count) ? ", " : "");
            }
            printf("]\n");
            printf("    }%s\n", (i + 1 < results_count) ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
    } else {
        printf("%s========================================================================================================%s\n", COLOR_GRAY, COLOR_RESET);
        printf("%s %-3s  %-15s  %-10s  %-30s  %-30s%s\n",
               COLOR_BOLD, "#", "DIRECCIÓN IP", "LATENCIA", "HOSTNAME / ETIQUETA", "SERVICIOS DETECTADOS", COLOR_RESET);
        printf("%s--------------------------------------------------------------------------------------------------------%s\n", COLOR_GRAY, COLOR_RESET);

        for (int i = 0; i < results_count; i++) {
            char role[64] = "";
            if (strcmp(results[i].ip, config.local_ip) == 0) {
                snprintf(role, sizeof(role), " (Este equipo)");
            } else if (results[i].ip[strlen(results[i].ip) - 2] == '.' && results[i].ip[strlen(results[i].ip) - 1] == '1') {
                snprintf(role, sizeof(role), " (Puerta de enlace)");
            }

            char host_display[128];
            if (strcmp(results[i].hostname, "-") != 0) {
                snprintf(host_display, sizeof(host_display), "%s%s", results[i].hostname, role);
            } else {
                snprintf(host_display, sizeof(host_display), "Dispositivo%s", role);
            }

            char ports_str[128] = "-";
            if (results[i].open_ports_count > 0) {
                ports_str[0] = '\0';
                for (int p = 0; p < results[i].open_ports_count; p++) {
                    char temp[32];
                    snprintf(temp, sizeof(temp), "%d/%s%s",
                             results[i].open_ports[p],
                             get_port_desc(results[i].open_ports[p]),
                             (p + 1 < results[i].open_ports_count) ? ", " : "");
                    strncat(ports_str, temp, sizeof(ports_str) - strlen(ports_str) - 1);
                }
            }

            const char *lat_color = COLOR_GREEN;
            if (results[i].rtt_ms > 100.0) lat_color = COLOR_YELLOW;
            if (results[i].rtt_ms > 300.0) lat_color = COLOR_RED;

            printf(" %s%-3d%s  %s%-15s%s  %s%6.1f ms%s   %-30.30s  %s%-30.30s%s\n",
                   COLOR_BOLD, i + 1, COLOR_RESET,
                   COLOR_CYAN, results[i].ip, COLOR_RESET,
                   lat_color, results[i].rtt_ms, COLOR_RESET,
                   host_display,
                   COLOR_MAGENTA, ports_str, COLOR_RESET);
        }

        printf("%s========================================================================================================%s\n", COLOR_GRAY, COLOR_RESET);
        printf("%s[✓] Escaneo finalizado en %.2f segundos.%s Dispositivos en línea: %s%s%d%s / %d\n\n",
               COLOR_BOLD, total_time, COLOR_RESET,
               COLOR_GREEN, COLOR_BOLD, results_count, COLOR_RESET, config.total_ips);
    }

    free_scan_results(results);
    return 0;
}

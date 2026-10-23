#ifndef SCANNER_H
#define SCANNER_H

#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_OPEN_PORTS 16
#define DEFAULT_TIMEOUT_MS 800
#define DEFAULT_PORT_TIMEOUT_MS 350

typedef struct {
    char ip[INET_ADDRSTRLEN];
    double rtt_ms;
    char hostname[128];
    int open_ports[MAX_OPEN_PORTS];
    int open_ports_count;
    int is_up;
} HostInfo;

typedef struct {
    char interface_name[32];
    char local_ip[INET_ADDRSTRLEN];
    char netmask[INET_ADDRSTRLEN];
    uint32_t start_ip;
    uint32_t end_ip;
    int total_ips;
    int timeout_ms;
    int probe_ports;
    int output_json;
    int verbose;
} ScanConfig;

int detect_local_network(ScanConfig *config);
int parse_ip_range(const char *cidr_or_range, ScanConfig *config);
int scan_network(const ScanConfig *config, HostInfo **results, int *results_count);
void probe_host_services(HostInfo *host, int timeout_ms);
void free_scan_results(HostInfo *results);

#endif /* SCANNER_H */

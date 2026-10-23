#define _GNU_SOURCE
#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Common service ports to probe */
static const int COMMON_PORTS[] = { 22, 53, 80, 443, 554, 3000, 5353, 8000, 8080, 8443 };
static const int NUM_COMMON_PORTS = sizeof(COMMON_PORTS) / sizeof(COMMON_PORTS[0]);

/* ICMP echo request header format */
struct icmp_packet {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    char payload[16];
};

static double timespec_diff_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

int detect_local_network(ScanConfig *config) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return -1;
    }

    /* First pass: look specifically for wlan or eth */
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        if (strncmp(ifa->ifa_name, "wlan", 4) == 0 ||
            strncmp(ifa->ifa_name, "eth", 3) == 0) {
            found = 1;
            break;
        }
    }

    /* Second pass: any valid non-loopback IPv4 interface */
    if (!found) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;
            if (!(ifa->ifa_flags & IFF_UP))
                continue;

            found = 1;
            break;
        }
    }

    if (!found || !ifa) {
        freeifaddrs(ifaddr);
        return -1;
    }

    struct sockaddr_in *sa_ip = (struct sockaddr_in *)ifa->ifa_addr;
    struct sockaddr_in *sa_mask = (struct sockaddr_in *)ifa->ifa_netmask;

    strncpy(config->interface_name, ifa->ifa_name, sizeof(config->interface_name) - 1);
    inet_ntop(AF_INET, &sa_ip->sin_addr, config->local_ip, sizeof(config->local_ip));

    if (sa_mask) {
        inet_ntop(AF_INET, &sa_mask->sin_addr, config->netmask, sizeof(config->netmask));
    } else {
        strncpy(config->netmask, "255.255.255.0", sizeof(config->netmask) - 1);
    }

    uint32_t ip_h = ntohl(sa_ip->sin_addr.s_addr);
    uint32_t mask_h = sa_mask ? ntohl(sa_mask->sin_addr.s_addr) : 0xFFFFFF00;

    uint32_t net_h = ip_h & mask_h;
    uint32_t bcast_h = ip_h | (~mask_h);

    config->start_ip = net_h + 1;
    config->end_ip = bcast_h - 1;
    config->total_ips = (config->end_ip >= config->start_ip) ? (config->end_ip - config->start_ip + 1) : 0;

    freeifaddrs(ifaddr);
    return 0;
}

int parse_ip_range(const char *cidr_or_range, ScanConfig *config) {
    char buf[128];
    strncpy(buf, cidr_or_range, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        int prefix = atoi(slash + 1);
        if (prefix < 16 || prefix > 30) return -1;

        struct in_addr base_addr;
        if (inet_pton(AF_INET, buf, &base_addr) <= 0) return -1;

        uint32_t mask = (prefix == 32) ? 0xFFFFFFFF : ~(0xFFFFFFFF >> prefix);
        uint32_t base_h = ntohl(base_addr.s_addr) & mask;
        config->start_ip = base_h + 1;
        config->end_ip = (base_h | ~mask) - 1;
        config->total_ips = config->end_ip - config->start_ip + 1;
        return 0;
    }

    char *dash = strchr(buf, '-');
    if (dash) {
        *dash = '\0';
        struct in_addr start_addr, end_addr;
        if (inet_pton(AF_INET, buf, &start_addr) <= 0) return -1;
        if (inet_pton(AF_INET, dash + 1, &end_addr) <= 0) return -1;

        config->start_ip = ntohl(start_addr.s_addr);
        config->end_ip = ntohl(end_addr.s_addr);
        if (config->end_ip < config->start_ip) return -1;
        config->total_ips = config->end_ip - config->start_ip + 1;
        return 0;
    }

    return -1;
}

void probe_host_services(HostInfo *host, int timeout_ms) {
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    if (inet_pton(AF_INET, host->ip, &target.sin_addr) <= 0) return;

    /* Reverse DNS resolution */
    char hbuf[NI_MAXHOST];
    if (getnameinfo((struct sockaddr*)&target, sizeof(target), hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD) == 0) {
        strncpy(host->hostname, hbuf, sizeof(host->hostname) - 1);
    } else {
        strncpy(host->hostname, "-", sizeof(host->hostname) - 1);
    }

    /* Quick non-blocking port probing */
    host->open_ports_count = 0;
    for (int i = 0; i < NUM_COMMON_PORTS && host->open_ports_count < MAX_OPEN_PORTS; i++) {
        int port = COMMON_PORTS[i];
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) continue;

        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);

        target.sin_port = htons(port);
        int res = connect(s, (struct sockaddr *)&target, sizeof(target));
        if (res == 0) {
            host->open_ports[host->open_ports_count++] = port;
            close(s);
            continue;
        }

        if (errno == EINPROGRESS) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(s, &wset);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            if (select(s + 1, NULL, &wset, NULL, &tv) > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    host->open_ports[host->open_ports_count++] = port;
                }
            }
        }
        close(s);
    }
}

int scan_network(const ScanConfig *config, HostInfo **results, int *results_count) {
    if (config->total_ips <= 0 || config->total_ips > 65536) {
        return -1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (sock < 0) {
        perror("socket(SOCK_DGRAM, IPPROTO_ICMP)");
        return -1;
    }

    /* Set socket non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    typedef struct {
        uint32_t ip;
        struct timespec sent_at;
        int replied;
        double rtt;
    } ProbeItem;

    ProbeItem *probes = calloc(config->total_ips, sizeof(ProbeItem));
    if (!probes) {
        close(sock);
        return -1;
    }

    for (int i = 0; i < config->total_ips; i++) {
        probes[i].ip = config->start_ip + i;
        probes[i].replied = 0;
        probes[i].rtt = 0.0;
    }

    struct icmp_packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = 8; /* Echo request */
    pkt.code = 0;
    pkt.id = htons(1337);
    strncpy(pkt.payload, "EIFISIER_PROBE", sizeof(pkt.payload) - 1);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;

    /* Two sweeps to minimize packet drop on Wi-Fi */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < config->total_ips; i++) {
            if (probes[i].replied) continue;

            dest.sin_addr.s_addr = htonl(probes[i].ip);
            pkt.seq = htons(i & 0xFFFF);

            clock_gettime(CLOCK_MONOTONIC, &probes[i].sent_at);
            sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&dest, sizeof(dest));
            usleep(250); /* micro pause to avoid buffer overload */
        }

        /* Collect replies with select() */
        struct timespec sweep_start, now;
        clock_gettime(CLOCK_MONOTONIC, &sweep_start);

        while (1) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = timespec_diff_ms(&sweep_start, &now);
            if (elapsed >= (config->timeout_ms / 2.0)) break;

            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(sock, &rset);

            struct timeval tv;
            int remain_ms = (int)((config->timeout_ms / 2.0) - elapsed);
            if (remain_ms < 1) remain_ms = 1;
            tv.tv_sec = remain_ms / 1000;
            tv.tv_usec = (remain_ms % 1000) * 1000;

            int sel = select(sock + 1, &rset, NULL, NULL, &tv);
            if (sel > 0 && FD_ISSET(sock, &rset)) {
                char recv_buf[256];
                struct sockaddr_in sender;
                socklen_t slen = sizeof(sender);

                ssize_t n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                                     (struct sockaddr *)&sender, &slen);
                if (n > 0) {
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    uint32_t sender_ip = ntohl(sender.sin_addr.s_addr);
                    if (sender_ip >= config->start_ip && sender_ip <= config->end_ip) {
                        int idx = sender_ip - config->start_ip;
                        if (!probes[idx].replied) {
                            probes[idx].replied = 1;
                            probes[idx].rtt = timespec_diff_ms(&probes[idx].sent_at, &now);
                            if (probes[idx].rtt < 0.1) probes[idx].rtt = 0.1;
                        }
                    }
                }
            } else {
                break;
            }
        }
    }

    close(sock);

    /* Count active hosts */
    int active_count = 0;
    for (int i = 0; i < config->total_ips; i++) {
        if (probes[i].replied) active_count++;
    }

    HostInfo *list = calloc(active_count > 0 ? active_count : 1, sizeof(HostInfo));
    if (!list) {
        free(probes);
        return -1;
    }

    int cur = 0;
    for (int i = 0; i < config->total_ips; i++) {
        if (probes[i].replied) {
            struct in_addr in;
            in.s_addr = htonl(probes[i].ip);
            inet_ntop(AF_INET, &in, list[cur].ip, sizeof(list[cur].ip));
            list[cur].rtt_ms = probes[i].rtt;
            list[cur].is_up = 1;

            if (config->probe_ports) {
                probe_host_services(&list[cur], DEFAULT_PORT_TIMEOUT_MS);
            } else {
                /* Reverse DNS only */
                struct sockaddr_in sa;
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_addr = in;
                char hbuf[NI_MAXHOST];
                if (getnameinfo((struct sockaddr*)&sa, sizeof(sa), hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD) == 0) {
                    strncpy(list[cur].hostname, hbuf, sizeof(list[cur].hostname) - 1);
                } else {
                    strncpy(list[cur].hostname, "-", sizeof(list[cur].hostname) - 1);
                }
            }
            cur++;
        }
    }

    free(probes);
    *results = list;
    *results_count = active_count;
    return 0;
}

void free_scan_results(HostInfo *results) {
    if (results) free(results);
}

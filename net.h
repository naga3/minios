// ネットワーク: インターフェースの設定（ioctl）と ICMP ping
#pragma once
#include "lib.h"

#define SYS_socket        41
#define SYS_sendto        44
#define SYS_recvfrom      45
#define SYS_setsockopt    54
#define SYS_clock_gettime 228

#define AF_INET      2
#define SOCK_DGRAM   2
#define SOCK_RAW     3
#define IPPROTO_ICMP 1
#define SOL_SOCKET   1
#define SO_RCVTIMEO  20

#define SIOCADDRT      0x890B
#define SIOCGIFFLAGS   0x8913
#define SIOCSIFFLAGS   0x8914
#define SIOCGIFADDR    0x8915
#define SIOCSIFADDR    0x8916
#define SIOCGIFNETMASK 0x891b
#define SIOCSIFNETMASK 0x891c
#define SIOCGIFHWADDR  0x8927

#define IFF_UP      0x1
#define IFF_RUNNING 0x40
#define RTF_UP      0x1
#define RTF_GATEWAY 0x2

#define EAGAIN 11

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;  // ネットワークバイトオーダー
    unsigned int sin_addr;    // ネットワークバイトオーダー
    char sin_zero[8];
};

struct ifreq {
    char ifr_name[16];
    union {
        struct sockaddr addr;
        short flags;
        char pad[24];
    };
};

struct rtentry {
    unsigned long rt_pad1;
    struct sockaddr rt_dst, rt_gateway, rt_genmask;
    unsigned short rt_flags;
    short rt_pad2;
    unsigned long rt_pad3;
    void *rt_pad4;
    short rt_metric;
    char *rt_dev;
    unsigned long rt_mtu, rt_window;
    unsigned short rt_irtt;
};

struct timespec { long tv_sec, tv_nsec; };
struct timeval { long tv_sec, tv_usec; };

static unsigned short htons(unsigned short v) { return (v >> 8) | (v << 8); }

static unsigned long now_us(void) {
    struct timespec t;
    syscall3(SYS_clock_gettime, 1 /* CLOCK_MONOTONIC */, &t, 0);
    return t.tv_sec * 1000000UL + t.tv_nsec / 1000;
}

// "a.b.c.d" をネットワークバイトオーダーの 32 ビットに。失敗なら 0 を返し *ok = 0
static unsigned int parse_ip(const char *s, int *ok) {
    unsigned int ip = 0;
    for (int i = 0; i < 4; i++) {
        unsigned int v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'), digits++;
        if (!digits || v > 255 || (i < 3 && *s++ != '.')) { *ok = 0; return 0; }
        ip |= v << (8 * i);
    }
    *ok = (*s == 0);
    return ip;
}

static void print_ip(unsigned int ip) {
    for (int i = 0; i < 4; i++) {
        if (i) print(".");
        print_u((ip >> (8 * i)) & 0xff);
    }
}

static void set_sin(struct sockaddr *sa, unsigned int ip) {
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    *sin = (struct sockaddr_in){.sin_family = AF_INET, .sin_addr = ip};
}

static void ifr_init(struct ifreq *r, const char *name) {
    *r = (struct ifreq){0};
    for (int i = 0; name[i] && i < 15; i++) r->ifr_name[i] = name[i];
}

static long if_up(long sock, const char *name) {
    struct ifreq r;
    ifr_init(&r, name);
    long e = syscall3(SYS_ioctl, sock, SIOCGIFFLAGS, &r);
    if (e < 0) return e;
    r.flags |= IFF_UP | IFF_RUNNING;
    return syscall3(SYS_ioctl, sock, SIOCSIFFLAGS, &r);
}

static long if_set_addr(long sock, const char *name, unsigned int ip, unsigned int mask) {
    struct ifreq r;
    ifr_init(&r, name);
    set_sin(&r.addr, ip);
    long e = syscall3(SYS_ioctl, sock, SIOCSIFADDR, &r);
    if (e < 0) return e;
    set_sin(&r.addr, mask);
    return syscall3(SYS_ioctl, sock, SIOCSIFNETMASK, &r);
}

static long route_add_default(long sock, unsigned int gw) {
    struct rtentry rt = {0};
    set_sin(&rt.rt_dst, 0);
    set_sin(&rt.rt_genmask, 0);
    set_sin(&rt.rt_gateway, gw);
    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    return syscall3(SYS_ioctl, sock, SIOCADDRT, &rt);
}

static unsigned short inet_checksum(const void *data, size_t len) {
    const unsigned char *p = data;
    unsigned long sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) sum += p[i] | (p[i + 1] << 8);
    if (len & 1) sum += p[len - 1];
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return ~sum;
}

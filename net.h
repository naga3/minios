// ネットワーク: インターフェースの設定（ioctl）と ICMP ping
#pragma once
#include "lib.h"

#define SYS_socket        41
#define SYS_sendto        44
#define SYS_recvfrom      45
#define SYS_setsockopt    54

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

// ---- DHCP ----

#define SYS_bind        49
#define SO_REUSEADDR    2
#define SO_BROADCAST    6
#define SO_BINDTODEVICE 25

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5
#define DHCP_NAK      6

#define ETIMEDOUT 110

struct dhcp_msg {
    unsigned char op, htype, hlen, hops;
    unsigned int xid;
    unsigned short secs, flags;
    unsigned int ciaddr, yiaddr, siaddr, giaddr;
    unsigned char chaddr[16];
    char sname[64], file[128];
    unsigned int cookie;
    unsigned char options[312];
} __attribute__((packed));

#define DHCP_COOKIE 0x63538263  // 99.130.83.99 をネットワークバイトオーダーで

struct dhcp_lease {
    unsigned int addr, mask, router, dns, server;
};

static unsigned char *dhcp_opt(unsigned char *p, unsigned char code, unsigned char len, const void *data) {
    *p++ = code;
    *p++ = len;
    for (int i = 0; i < len; i++) *p++ = ((const unsigned char *)data)[i];
    return p;
}

// オプション code の中身を out に最大 max バイト写す。長さか、無ければ -1
static int dhcp_find(const struct dhcp_msg *m, long n, unsigned char code, void *out, int max) {
    const unsigned char *p = m->options, *end = (const unsigned char *)m + n;
    while (p < end && *p != 255) {
        if (*p == 0) { p++; continue; }
        if (p + 2 > end || p + 2 + p[1] > end) break;
        if (*p == code) {
            int len = p[1] < max ? p[1] : max;
            for (int i = 0; i < len; i++) ((unsigned char *)out)[i] = p[2 + i];
            return p[1];
        }
        p += 2 + p[1];
    }
    return -1;
}

static long dhcp_send(long sock, const struct dhcp_msg *m, const unsigned char *end) {
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(67), .sin_addr = 0xffffffff};
    return syscall6(SYS_sendto, sock, (long)m, end - (const unsigned char *)m, 0, (long)&to, sizeof to);
}

// xid が一致し、種類が want か NAK の返事を 1 秒まで待つ。種類を返し、長さを *len に入れる。来なければ 0
static int dhcp_recv(long sock, unsigned int xid, int want, struct dhcp_msg *m, long *len) {
    unsigned long deadline = now_us() + 1000000;
    while (now_us() < deadline) {
        long n = syscall6(SYS_recvfrom, sock, (long)m, sizeof *m, 0, 0, 0);
        if (n < 0) return 0;  // SO_RCVTIMEO で打ち切り
        if (n < 240 || m->op != 2 || m->xid != xid || m->cookie != DHCP_COOKIE) continue;
        unsigned char type = 0;
        dhcp_find(m, n, 53, &type, 1);
        if (type == want || type == DHCP_NAK) {
            *len = n;
            return type;
        }
    }
    return 0;
}

// ifname で DHCP を行い、リースを得る。インターフェースは上げておく（アドレスは不要）。0 か負のエラー番号
static long dhcp(long ctl, const char *ifname, struct dhcp_lease *lease) {
    struct ifreq r;
    ifr_init(&r, ifname);
    long e = syscall3(SYS_ioctl, ctl, SIOCGIFHWADDR, &r);
    if (e < 0) return e;
    unsigned char mac[6];
    for (int i = 0; i < 6; i++) mac[i] = r.addr.sa_data[i];

    long sock = syscall3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return sock;
    int one = 1;
    syscall6(SYS_setsockopt, sock, SOL_SOCKET, SO_REUSEADDR, (long)&one, sizeof one, 0);
    syscall6(SYS_setsockopt, sock, SOL_SOCKET, SO_BROADCAST, (long)&one, sizeof one, 0);
    // まだ IP が無いので、送り出す NIC を名前で指定する
    e = syscall6(SYS_setsockopt, sock, SOL_SOCKET, SO_BINDTODEVICE, (long)ifname, slen(ifname) + 1, 0);
    struct sockaddr_in me = {.sin_family = AF_INET, .sin_port = htons(68)};
    if (e >= 0) e = syscall3(SYS_bind, sock, &me, sizeof me);
    struct timeval tv = {1, 0};
    if (e >= 0) e = syscall6(SYS_setsockopt, sock, SOL_SOCKET, SO_RCVTIMEO, (long)&tv, sizeof tv, 0);
    if (e < 0) { syscall3(SYS_close, sock, 0, 0); return e; }

    unsigned int xid = now_us() ^ (mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5]);
    static const unsigned char params[] = {1, 3, 6};  // サブネットマスク、ルーター、DNS
    e = -ETIMEDOUT;
    for (int attempt = 0; attempt < 4 && e < 0; attempt++) {
        struct dhcp_msg m = {.op = 1, .htype = 1, .hlen = 6, .xid = xid,
                             .flags = htons(0x8000),  // IP が無いので、返事はブロードキャストで送ってもらう
                             .cookie = DHCP_COOKIE};
        for (int i = 0; i < 6; i++) m.chaddr[i] = mac[i];
        unsigned char type = DHCP_DISCOVER;
        unsigned char *p = dhcp_opt(m.options, 53, 1, &type);
        p = dhcp_opt(p, 55, sizeof params, params);
        *p++ = 255;
        dhcp_send(sock, &m, p);

        struct dhcp_msg offer;
        long n;
        if (dhcp_recv(sock, xid, DHCP_OFFER, &offer, &n) != DHCP_OFFER) continue;
        unsigned int server = 0;
        dhcp_find(&offer, n, 54, &server, 4);

        type = DHCP_REQUEST;
        p = dhcp_opt(m.options, 53, 1, &type);
        p = dhcp_opt(p, 50, 4, &offer.yiaddr);
        p = dhcp_opt(p, 54, 4, &server);
        p = dhcp_opt(p, 55, sizeof params, params);
        *p++ = 255;
        dhcp_send(sock, &m, p);

        struct dhcp_msg ack;
        if (dhcp_recv(sock, xid, DHCP_ACK, &ack, &n) != DHCP_ACK) continue;
        *lease = (struct dhcp_lease){.addr = ack.yiaddr, .server = server};
        dhcp_find(&ack, n, 1, &lease->mask, 4);
        dhcp_find(&ack, n, 3, &lease->router, 4);
        dhcp_find(&ack, n, 6, &lease->dns, 4);
        e = 0;
    }
    syscall3(SYS_close, sock, 0, 0);
    return e;
}

// The LAN address nifm reports (ov_net.c ov_ip_config), split from ov_socket.c.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

#include "cfg.h"
#include "host.h"
#include "ov.h"

static int lan_only(struct in_addr a) { return ov_lan_only(ntohl(a.s_addr)); }

// ---- the LAN address nifm reports (ov_net.c): MKW_LAN_IP, else the first interface that is up with a local network
// address (lan_only, not loopback), read from the system's interface list (nothing is sent), with its mask. Pia
// builds its broadcast from both. An address with a /32 mask (a VPN such as Tailscale gives one) comes only after
// every other: Pia refuses it with error 2318-0007 (#148, reproduced with tools/run/lan2.sh at LAN2_PREFIX=32).
static int lan_pick(struct in_addr a, struct in_addr want) {
    return want.s_addr ? a.s_addr == want.s_addr : (ntohl(a.s_addr) >> 24) != 127 && lan_only(a) && a.s_addr;
}
// keeps x/xm in a/m unless a already holds an address with a real network; returns 1 once it does
static int lan_take(struct in_addr *a, struct in_addr *m, struct in_addr x, struct in_addr xm) {
    if (!a->s_addr || (m->s_addr == 0xffffffffu && xm.s_addr != 0xffffffffu)) *a = x, *m = xm;
    return m->s_addr != 0xffffffffu;
}

int ov_lan_address(uint8_t ip[4], uint8_t mask[4]) {
    struct in_addr want = {0}, a = {0}, m = {0};
    const char *e = cfg_get("MKW_LAN_IP");
    if (e && *e && inet_pton(AF_INET, e, &want) != 1) rt_log("nifm: MKW_LAN_IP %s is not an IPv4 address", e);
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return -1;
    ULONG size = 16 << 10;
    IP_ADAPTER_ADDRESSES *list = malloc(size);
    if (list && GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                     NULL, list, &size) == ERROR_BUFFER_OVERFLOW) {
        free(list);
        list = malloc(size);
        if (list && GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                         GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &size) != NO_ERROR)
            free(list), list = NULL;
    }
    int done = 0;
    for (IP_ADAPTER_ADDRESSES *i = list; i && !done; i = i->Next) {
        if (i->OperStatus != IfOperStatusUp || i->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS *u = i->FirstUnicastAddress; u && !done; u = u->Next) {
            struct in_addr x = ((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr, xm;
            if (!lan_pick(x, want)) continue;
            uint8_t bits = u->OnLinkPrefixLength > 32 ? 24 : u->OnLinkPrefixLength;
            xm.s_addr = htonl(bits ? 0xffffffffu << (32 - bits) : 0);
            done = lan_take(&a, &m, x, xm);
        }
    }
    free(list);
#else
    struct ifaddrs *list, *i;
    if (!getifaddrs(&list)) {
        for (i = list; i; i = i->ifa_next)
            if (i->ifa_addr && i->ifa_netmask && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_UP) &&
                lan_pick(((struct sockaddr_in *)i->ifa_addr)->sin_addr, want) &&
                lan_take(&a, &m, ((struct sockaddr_in *)i->ifa_addr)->sin_addr,
                         ((struct sockaddr_in *)i->ifa_netmask)->sin_addr))
                break;
        freeifaddrs(list);
    }
#endif
    if (!a.s_addr && want.s_addr) a = want, m.s_addr = htonl(0xffffff00u);   // MKW_LAN_IP not on an interface: /24
    if (!a.s_addr) {
        rt_log("nifm: no network interface with a local network address: no LAN");
        return -1;
    }
    static int warned;
    if (m.s_addr == 0xffffffffu && !warned++) {
        char t[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a, t, sizeof t);
        rt_log("nifm: %s has a /32 mask and no other interface has a local network address: the game refuses it "
               "(error 2318-0007); LAN play needs a network with a broadcast address", t);
    }
    memcpy(ip, &a, 4);
    memcpy(mask, &m, 4);
    return 0;
}

#include <sys/socket.h>

/* ===================== v12.8 网络侧防护 =====================
 * 目标：防止「iptables / 透明代理 / VPN / 抓包」这条线上的分析。
 *
 * 三条判据，全部 fail-open（读不到就当没有，绝不因为权限不足误杀）：
 *   1) 硬：进程表里出现抓包/代理工具（tcpdump / tshark / mitmproxy / HttpCanary …）
 *      —— 能读到 /proc/<pid>/comm 才有意义，Android 上被 SELinux 挡住就当没有。
 *   2) 硬（需要调用方配合）：nk_net_check_fd() —— 连接建立后查 SO_ORIGINAL_DST，
 *      若真实目的地址和我们想连的不一致，说明被 iptables REDIRECT/DNAT 劫持了。
 *   3) 软（只进 g_tool_score，不参与判死）：VPN / tun / ppp / wg 接口、
 *      http_proxy 环境变量、netfilter 规则表已加载（/proc/net/ip_tables_names）。
 *
 * 之所以把「iptables 有规则」只当软证据：KernelSU / 各家 ROM / 防火墙 App
 * 都会常驻一堆规则，把它当硬证据在真机上必然大面积误杀。 */

/* SO_ORIGINAL_DST 在 bionic 的 <linux/netfilter_ipv4.h> 里，避免多引一个头 */
#ifndef NK_SO_ORIGINAL_DST
#define NK_SO_ORIGINAL_DST 80
#endif

/* 只认「抓包/中间人」工具。代理梯子（clash / v2ray / sing-box …）不在此列，
 * 用户自己挂梯子属于正常使用，绝不能被这里判成攻击。 */
static const char *nk_net_sniff_names[] = {
    "tcpdump", "tshark", "dumpcap", "wireshark", "mitmproxy", "mitmdump",
    "burpsuite", "burp", "fiddler", "charles", "httpcanary", "packetcapture",
    "netcapture", "reqable", "packetflinger", "pcapexporter", NULL
};

/* 梯子/VPN 客户端白名单：即使 comm 里出现这些词也一律放行（连软分都不记）。 */
static const char *nk_net_vpn_names[] = {
    "clash", "mihomo", "clash-meta", "v2ray", "xray", "sing-box", "singbox",
    "shadowsocks", "ss-local", "sslocal", "trojan", "hysteria", "tuic",
    "naive", "surfboard", "netch", "nekobox", "nekoray", "v2rayng",
    "openvpn", "wireguard", "wg-", "tailscale", "zerotier", "drony",
    "proxydroid", "proxy", "vpn", NULL
};

static int nk_net_is_ladder(const char *comm)
{
    for (int i = 0; nk_net_vpn_names[i]; i++) {
        const char *w = nk_net_vpn_names[i];
        if (strstr(comm, w)) return 1;
    }
    return 0;
}

/* 抓包/代理进程（硬证据）。返回 1 表示命中。 */
static int nk_net_sniffers(void)
{
    /* 遍历 /proc 下的数字目录：直接读 comm，读不到就跳过（SELinux 常态） */
    long dfd = nk_sys(56 /* openat */, -100, (long)"/proc", 0x10000 /* O_DIRECTORY|O_RDONLY */);
    if (dfd < 0) return 0;

    char buf[4096];
    int hit = 0;
    for (;;) {
        long n = nk_sys(61 /* getdents64 */, dfd, (long)buf, (long)sizeof buf);
        if (n <= 0) break;
        long off = 0;
        while (off < n) {
            /* struct linux_dirent64: ino(8) off(8) reclen(2) type(1) name[] */
            unsigned short reclen = *(unsigned short *)(buf + off + 16);
            char *nm = buf + off + 19;
            if (reclen < 24) break;
            if (nm[0] >= '1' && nm[0] <= '9') {
                char path[64];
                int k = 0;
                const char *pre = "/proc/";
                while (*pre && k < 56) path[k++] = *pre++;
                const char *q = nm;
                while (*q && k < 56) path[k++] = *q++;
                const char *suf = "/comm";
                while (*suf && k < 62) path[k++] = *suf++;
                path[k] = 0;

                char comm[64];
                int cn = nk_slurp(path, comm, sizeof comm - 1);
                if (cn > 0) {
                    comm[cn] = 0;
                    for (int i = 0; i < cn; i++)
                        if (comm[i] >= 'A' && comm[i] <= 'Z') comm[i] = (char)(comm[i] + 32);
                    if (nk_net_is_ladder(comm)) { off += reclen; continue; }
                    for (int i = 0; nk_net_sniff_names[i]; i++) {
                        const char *w = nk_net_sniff_names[i];
                        if (strstr(comm, w)) {
                            nk_dbg("nk_ad[net] 抓到抓包/代理进程：%s (pid %s)\n", w, nm);
                            hit = 1;
                            break;
                        }
                    }
                    if (hit) break;
                }
            }
            off += reclen;
        }
        if (hit) break;
    }
    nk_sys(57, dfd, 0, 0);
    return hit;
}

/* 接口名扫描：VPN / tun / ppp / wg 一类（软证据）。 */
static void nk_net_ifaces(void)
{
    char buf[8192];
    int n = nk_slurp("/proc/net/dev", buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = 0;

    const char *bad[] = { "tun", "tap", "ppp", "wg", "utun", "ipsec", "xfrm",
                          "tailscale", "zt", "clat", "nordlynx", "vpn", NULL };
    const char *p = buf;
    while ((p = strchr(p, ':')) != NULL) {
        p++;
        /* 回退到行首取接口名 */
        const char *ls = p;
        while (ls > buf && ls[-1] != '\n') ls--;
        char name[32];
        int k = 0;
        for (const char *q = ls; q < p - 1 && k < 31; q++) {
            if (*q == ' ' || *q == '\t') continue;
            name[k++] = *q;
        }
        name[k] = 0;
        if (k >= 2 && !(name[0] == 'l' && name[1] == 'o')) {
            for (int i = 0; bad[i]; i++) {
                if (strncmp(name, bad[i], strlen(bad[i])) == 0) {
                    /* 梯子的 tun/wg 接口是正常使用：只写日志，连软分都不记 */
                    nk_dbg("nk_ad[net] VPN/代理接口：%s（正常使用，不判死）\n", name);
                    return;
                }
            }
        }
    }
}

/* 代理环境变量（软证据）。 */
static void nk_net_proxy_env(void)
{
    const char *keys[] = { "http_proxy", "https_proxy", "all_proxy",
                           "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", NULL };
    for (int i = 0; keys[i]; i++) {
        const char *v = getenv(keys[i]);
        if (v && *v) {
            nk_dbg("nk_ad[net] 代理环境变量 %s=%s（正常使用，不判死）\n", keys[i], v);
            return;
        }
    }
}

/* netfilter 规则表已加载（软证据；KernelSU/ROM 常驻，不代表被分析）。 */
static void nk_net_iptables(void)
{
    char buf[256];
    int n = nk_slurp("/proc/net/ip_tables_names", buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == 'f' || buf[i] == 'n' || buf[i] == 'm') {
            nk_soft_tool("iptables-present");
            nk_dbg("nk_ad[net] netfilter 规则表已加载（不代表被拦截）\n");
            return;
        }
    }
}

/* 硬证据：连接被 iptables REDIRECT/DNAT 改道。
 * 用法：在自己的 socket 连上以后调用 —— 期望的目的地址与实际目的地址不一致就返回 1。
 * want_ip_be：网络字节序的 IPv4；want_port：主机字节序端口。 */
static int nk_net_check_fd(int fd, unsigned int want_ip_be, unsigned short want_port)
{
    struct { unsigned int ip; unsigned short port; unsigned char pad[8]; } dst;
    memset(&dst, 0, sizeof dst);
    unsigned int len = sizeof dst;

    /* getsockopt(SOL_IP, SO_ORIGINAL_DST) —— 没被 NAT 时返回 -1，属正常 */
    if (getsockopt(fd, 0 /* SOL_IP */, NK_SO_ORIGINAL_DST, &dst, &len) != 0) return 0;

    unsigned short port_host = (unsigned short)((dst.port >> 8) | (dst.port << 8));
    if (dst.ip != 0 && (dst.ip != want_ip_be || port_host != want_port)) {
        nk_dbg("nk_ad[net] 连接被改道：want %08x:%u -> real %08x:%u\n",
               want_ip_be, want_port, dst.ip, port_host);
        return 1;
    }
    return 0;
}

/* 汇总入口：软证据 + 抓包进程硬证据。 */
static void nk_net_guard(void)
{
    if (nk_net_sniffers()) nk_die_r("nk_ad: packet sniffer process");
    nk_net_ifaces();
    nk_net_proxy_env();
    nk_net_iptables();
}

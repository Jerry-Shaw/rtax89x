/*
 * Linux network interface code
 *
 * Copyright 2005, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: interface.c,v 1.13 2005/03/07 08:35:32 kanki Exp $
 */

#include <rc.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#ifdef __GLIBC__		//musl doesn't have error.h
#include <error.h>
#endif	/* __GLIBC__ */
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <proto/ethernet.h>

#include <shutils.h>
#include <bcmnvram.h>
#include <bcmutils.h>
#include <bcmparams.h>
#include <bcmdevs.h>
#include <shared.h>
#ifdef RTCONFIG_BCMARM
#include <linux/ethtool.h>
#include <linux/sockios.h>
#endif

#include "interface.h"

/* Default switch configs */
static struct switch_config {
	int lanmask;
	int wanmask;
} sw_config[] = {
#ifdef RTCONFIG_EXT_RTL8365MB
	SWCFG_INIT(SWCFG_DEFAULT, SW_CPU|SW_L1|SW_L2|SW_L3|SW_L4|SW_L5,	 SW_CPU|SW_WAN),
	SWCFG_INIT(SWCFG_STB1,    SW_CPU|      SW_L2|SW_L3|SW_L4|SW_L5,  SW_CPU|SW_WAN|SW_L1),
	SWCFG_INIT(SWCFG_STB2,    SW_CPU|SW_L1|      SW_L3|SW_L4|SW_L5,  SW_CPU|SW_WAN|SW_L2),
	SWCFG_INIT(SWCFG_STB3,    SW_CPU|SW_L1|SW_L2|      SW_L4|SW_L5,  SW_CPU|SW_WAN|SW_L3),
	SWCFG_INIT(SWCFG_STB4,    SW_CPU|SW_L1|SW_L2|SW_L3      |SW_L5,  SW_CPU|SW_WAN|SW_L4),
	SWCFG_INIT(SWCFG_STB12,   SW_CPU|            SW_L3|SW_L4|SW_L5,  SW_CPU|SW_WAN|SW_L1|SW_L2),
	SWCFG_INIT(SWCFG_STB34,   SW_CPU|SW_L1|SW_L2            |SW_L5,  SW_CPU|SW_WAN|SW_L3|SW_L4),
	SWCFG_INIT(SWCFG_BRIDGE,  SW_CPU|SW_L1|SW_L2|SW_L3|SW_L4|SW_L5|SW_WAN, SW_CPU),
	SWCFG_INIT(SWCFG_PSTA,	  SW_CPU|SW_L1|SW_L2|SW_L3|SW_L4|SW_L5,  SW_CPU),
	SWCFG_INIT(WAN1PORT1, SW_CPU|SW_L2|SW_L3|SW_L4|SW_L5, SW_CPU|SW_L1),
	SWCFG_INIT(WAN1PORT2, SW_CPU|SW_L1|SW_L3|SW_L4|SW_L5, SW_CPU|SW_L2),
	SWCFG_INIT(WAN1PORT3, SW_CPU|SW_L1|SW_L2|SW_L4|SW_L5, SW_CPU|SW_L3),
	SWCFG_INIT(WAN1PORT4, SW_CPU|SW_L1|SW_L2|SW_L3|SW_L5, SW_CPU|SW_L4)
#else
	SWCFG_INIT(SWCFG_DEFAULT, SW_CPU|SW_L1|SW_L2|SW_L3|SW_L4,        SW_CPU|SW_WAN),
	SWCFG_INIT(SWCFG_STB1,    SW_CPU|      SW_L2|SW_L3|SW_L4,        SW_CPU|SW_WAN|SW_L1),
	SWCFG_INIT(SWCFG_STB2,    SW_CPU|SW_L1|      SW_L3|SW_L4,        SW_CPU|SW_WAN|SW_L2),
	SWCFG_INIT(SWCFG_STB3,    SW_CPU|SW_L1|SW_L2|      SW_L4,        SW_CPU|SW_WAN|SW_L3),
	SWCFG_INIT(SWCFG_STB4,    SW_CPU|SW_L1|SW_L2|SW_L3,              SW_CPU|SW_WAN|SW_L4),
	SWCFG_INIT(SWCFG_STB12,   SW_CPU|            SW_L3|SW_L4,        SW_CPU|SW_WAN|SW_L1|SW_L2),
	SWCFG_INIT(SWCFG_STB34,   SW_CPU|SW_L1|SW_L2,                    SW_CPU|SW_WAN|SW_L3|SW_L4),
	SWCFG_INIT(SWCFG_BRIDGE,  SW_CPU|SW_L1|SW_L2|SW_L3|SW_L4|SW_WAN, SW_CPU),
	SWCFG_INIT(SWCFG_PSTA,	  SW_CPU|SW_L1|SW_L2|SW_L3|SW_L4, 	 SW_CPU),
	SWCFG_INIT(WAN1PORT1, SW_CPU|SW_L2|SW_L3|SW_L4, SW_CPU|SW_L1),
	SWCFG_INIT(WAN1PORT2, SW_CPU|SW_L1|SW_L3|SW_L4, SW_CPU|SW_L2),
	SWCFG_INIT(WAN1PORT3, SW_CPU|SW_L1|SW_L2|SW_L4, SW_CPU|SW_L3),
	SWCFG_INIT(WAN1PORT4, SW_CPU|SW_L1|SW_L2|SW_L3, SW_CPU|SW_L4)
#endif
};

/* Generates switch ports config string
 * char *buf	- pointer to buffer[SWCFG_BUFSIZE] for result string
 * int *ports	- array of phy port numbers in order of SWPORT_ enum, eg. {W,L1,L2,L3,L4,C}
 * int swmask	- mask of logical ports to return
 * char *cputag	- NULL: return config string excluding CPU port
 *		  PSTR: return config string including CPU port, tagged with PSTR, eg. 8|8t|8*
 * int wan	- config wan port or not
 */
void _switch_gen_config(char *buf, const int ports[SWPORT_COUNT], int swmask, char *cputag, int wan)
{
	struct {
		int port;
		char *tag;
	} res[SWPORT_COUNT];
	int i, n, count, mask = swmask;
	char *ptr;

	if (!cputag)
		mask &= ~SW_CPU;

	if (wan && !cputag) {
            for (n = i = count = 0; i < SWPORT_COUNT && mask; mask >>= 1, i++) {
                if ((mask & 1U) == 0)
                        continue;
                res[n].port = ports[i];
                res[n].tag = (i == SWPORT_CPU) ? cputag : "";
                count++;
                n++; 
            }
	}
	else {
	    for (i = count = 0; i < SWPORT_COUNT && mask; mask >>= 1, i++) {
		if ((mask & 1U) == 0)
			continue;
		for (n = count; n > 0 && ports[i] < res[n - 1].port; n--)
			res[n] = res[n - 1];
		res[n].port = ports[i];
		res[n].tag = (i == SWPORT_CPU) ? cputag : "";
		count++;
	    }
	}
	for (i = 0, ptr = buf; ptr && i < count; i++)
		ptr += sprintf(ptr, i ? " %d%s" : "%d%s", res[i].port, res[i].tag);
}

/* Generates switch ports config string
 * char *buf	- pointer to buffer[SWCFG_BUFSIZE] for result string
 * int *ports	- array of phy port numbers in order of SWPORT_ enum, eg. {W,L1,L2,L3,L4,C}
 * int index	- config index in default sw_config array
 * int wan	- 0: return config string for lan ports
 *		  1: return config string for wan ports
 * char *cputag	- NULL: return config string excluding CPU port
 *		  PSTR: return config string including CPU port, tagged with PSTR, eg. 8|8t|8*
 */
void switch_gen_config(char *buf, const int ports[SWPORT_COUNT], int index, int wan, char *cputag)
{
	int mask;

	if (!buf || index < SWCFG_DEFAULT || index >= SWCFG_COUNT)
		return;

	mask = wan ? sw_config[index].wanmask : sw_config[index].lanmask;
	_switch_gen_config(buf, ports, mask, cputag, wan);
}

void gen_lan_ports(char *buf, const int sample[SWPORT_COUNT], int index, int index1, char *cputag){
	struct {
		int port;
		char *tag;
	} res[SWPORT_COUNT];
	int i, n, count;
	int mask, mask1;
	char *ptr;

	mask = sw_config[index].lanmask;
	if(index1 >= SWCFG_DEFAULT){
		mask1 = sw_config[index1].lanmask;
		mask &= mask1;
	}

	if (!cputag)
		mask &= ~SW_CPU;

	for (i = count = 0; i < SWPORT_COUNT && mask; mask >>= 1, i++) {
		if ((mask & 1U) == 0)
			continue;
		for (n = count; n > 0 && sample[i] < res[n - 1].port; n--)
			res[n] = res[n - 1];
		res[n].port = sample[i];
		res[n].tag = (i == SWPORT_CPU) ? cputag : "";
		count++;
	}

	for (i = 0, ptr = buf; ptr && i < count; i++)
		ptr += sprintf(ptr, i ? " %d%s" : "%d%s", res[i].port, res[i].tag);
}

#define sin_addr(s) (((struct sockaddr_in *)(s))->sin_addr)

int _ifconfig(const char *name, int flags, const char *addr, const char *netmask, const char *dstaddr, int mtu)
{
	int s, err, postup;
	struct ifreq ifr;
	struct in_addr in_addr, in_netmask, in_broadaddr;

#ifdef RTCONFIG_LANTIQ
	/* wlan0-wlan0.2, wlan2-wlan2.2 */
	if(strncmp(name, "wlan", 4) == 0){
		if(flags == 0){
			_dprintf("[%s][%d]skip ifconfig down:[%s]\n",
				__func__, __LINE__, name);
			return -1;
		}
		if(flags & IFUP){
			if(skip_ifconfig_up(name) == -1) return -1;
		}
	}
	if(strcmp(name, "eth0_1") == 0 ||
		strcmp(name, "eth0_2") == 0 ||
		strcmp(name, "eth0_3") == 0 ||
		strcmp(name, "eth0_4") == 0){
		set_hwaddr(name, get_lan_hwaddr());
		if(flags == 0) return -1;
	}
	if(strcmp(name, "eth1") == 0){
		set_hwaddr(name, get_wan_hwaddr());
		if(flags == 0) return -1;
	}
#endif

	_dprintf("%s: name=%s flags=%04x %s addr=%s netmask=%s\n", __FUNCTION__, name ? : "", 
		flags, (flags & IFUP) ? "IFUP" : "", addr ? : "", netmask ? : "");

	if (!name)
		return -1;

	/* Open a raw socket to the kernel */
	if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
		return errno;

	/* Set interface name */
	strlcpy(ifr.ifr_name, name, IFNAMSIZ);
	postup = strchr(name, ':') && (flags & IFUP);

	/* Set interface flags */
	ifr.ifr_flags = flags;
	if (!postup && ioctl(s, SIOCSIFFLAGS, &ifr) < 0)
		goto ERROR;

	/* Set IP address */
	if (addr) {
		inet_aton(addr, &in_addr);
		sin_addr(&ifr.ifr_addr).s_addr = in_addr.s_addr;
		ifr.ifr_addr.sa_family = AF_INET;
		if (ioctl(s, SIOCSIFADDR, &ifr) < 0)
			goto ERROR;
	}

	/* Set IP netmask and broadcast */
	if (addr && netmask) {
		inet_aton(netmask, &in_netmask);
		sin_addr(&ifr.ifr_netmask).s_addr = in_netmask.s_addr;
		ifr.ifr_netmask.sa_family = AF_INET;
		if (ioctl(s, SIOCSIFNETMASK, &ifr) < 0)
			goto ERROR;

		in_broadaddr.s_addr = (in_addr.s_addr & in_netmask.s_addr) | ~in_netmask.s_addr;
		sin_addr(&ifr.ifr_broadaddr).s_addr = in_broadaddr.s_addr;
		ifr.ifr_broadaddr.sa_family = AF_INET;
		if (ioctl(s, SIOCSIFBRDADDR, &ifr) < 0)
			goto ERROR;
	}

	/* Set IP dst or P-to-P peer address */
	if (dstaddr && *dstaddr) {
		inet_aton(dstaddr, &in_addr);
		sin_addr(&ifr.ifr_dstaddr).s_addr = in_addr.s_addr;
		ifr.ifr_dstaddr.sa_family = AF_INET;
		if (ioctl(s, SIOCSIFDSTADDR, &ifr) < 0)
			goto ERROR;
	}

	/* Set interface flags */
	ifr.ifr_flags = flags;
	if (postup && ioctl(s, SIOCSIFFLAGS, &ifr) < 0)
		goto ERROR;

	/* Set MTU */
	if (mtu > 0) {
		ifr.ifr_mtu = mtu;
		if (ioctl(s, SIOCSIFMTU, &ifr) < 0)
			goto ERROR;
	}

	close(s);

//Andrew add
#ifdef RTCONFIG_CONNTRACK 
	if(flags & IFUP)
		conntrack_check(CONNTRACK_START); 
	else if (flags & IFFDOWN)
		conntrack_check(CONNTRACK_STOP); 
#endif
//Andrew end

	return 0;

 ERROR:
	err = errno; 
	perror(name);
	close(s);
	return err;
}

int _ifconfig_get(const char *name, int *flags, char *addr, char *netmask, char *dstaddr, int *mtu)
{
	int s, err;
	struct ifreq ifr;

	/* Open a raw socket to the kernel */
	if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
		return errno;

	/* Set interface name */
	strlcpy(ifr.ifr_name, name, IFNAMSIZ);

	/* Get interface flags */
	if (flags) {
		if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0)
			goto ERROR;
		*flags = ifr.ifr_flags;
	}

	/* Get IP address */
	if (addr) {
		ifr.ifr_addr.sa_family = AF_INET;
		if (ioctl(s, SIOCGIFADDR, &ifr) < 0)
			goto ERROR;
		inet_ntop(AF_INET, &sin_addr(&ifr.ifr_addr), addr, sizeof("255.255.255.255"));
	}

	/* Get IP netmask and broadcast */
	if (netmask) {
		ifr.ifr_addr.sa_family = AF_INET;
		if (ioctl(s, SIOCGIFNETMASK, &ifr) < 0)
			goto ERROR;
		inet_ntop(AF_INET, &sin_addr(&ifr.ifr_netmask), netmask, sizeof("255.255.255.255"));
	}

	/* Get IP dst or P-to-P peer address */
	if (dstaddr) {
		ifr.ifr_dstaddr.sa_family = AF_INET;
		if (ioctl(s, SIOCGIFDSTADDR, &ifr) < 0)
			goto ERROR;
		inet_ntop(AF_INET, &sin_addr(&ifr.ifr_dstaddr), dstaddr, sizeof("255.255.255.255"));
	}

	/* Get MTU */
	if (mtu) {
		if (ioctl(s, SIOCGIFMTU, &ifr) < 0)
			goto ERROR;
		*mtu = ifr.ifr_mtu;
	}

	close(s);
	return 0;

 ERROR:
	err = errno;
	perror(name);
	close(s);
	return err;
}

int ifconfig_mtu(const char *name, int mtu)
{
	int ret, flags, old_mtu;

	ret = _ifconfig_get(name, &flags, NULL, NULL, NULL, &old_mtu);
	if (ret == 0 && mtu > 0 && mtu != old_mtu)
		ret = _ifconfig(name, flags, NULL, NULL, NULL, mtu);

	return ret ? -1 : old_mtu;
}

static int route_manip(int cmd, char *name, int metric, char *dst, char *gateway, char *genmask)
{
	int s, err = 0;
	struct rtentry rt;
	
	_dprintf("%s: cmd=%s name=%s addr=%s netmask=%s gateway=%s metric=%d\n",
		__FUNCTION__, cmd == SIOCADDRT ? "ADD" : "DEL", name, dst, genmask, gateway, metric);

	/* Open a raw socket to the kernel */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return errno;

	/* Fill in rtentry */
	memset(&rt, 0, sizeof(rt));
	if (dst)
		inet_aton(dst, &sin_addr(&rt.rt_dst));
	if (gateway)
		inet_aton(gateway, &sin_addr(&rt.rt_gateway));
	if (genmask)
		inet_aton(genmask, &sin_addr(&rt.rt_genmask));
	rt.rt_metric = metric;
	rt.rt_flags = RTF_UP;
	if (sin_addr(&rt.rt_gateway).s_addr)
		rt.rt_flags |= RTF_GATEWAY;
	if (sin_addr(&rt.rt_genmask).s_addr == INADDR_BROADCAST)
		rt.rt_flags |= RTF_HOST;
	rt.rt_dev = name;

	/* Force address family to AF_INET */
	rt.rt_dst.sa_family = AF_INET;
	rt.rt_gateway.sa_family = AF_INET;
	rt.rt_genmask.sa_family = AF_INET;
		
	if (ioctl(s, cmd, &rt) < 0) {
		err = errno;
		perror(name);
	}

	close(s);
	return err;

}

int route_add(char *name, int metric, char *dst, char *gateway, char *genmask)
{
	return route_manip(SIOCADDRT, name, metric, dst, gateway, genmask);
}

int route_del(char *name, int metric, char *dst, char *gateway, char *genmask)
{
	return route_manip(SIOCDELRT, name, metric, dst, gateway, genmask);
}

int route_exist(const char *name, int metric, const char *dst, const char *gateway, const char *mask)
{
	FILE *fp = fopen("/proc/net/route", "r");
	in_addr_t addr_dst, addr_gate, addr_mask;
	in_addr_t rtr_dst, rtr_gate, rtr_mask;
	int rtr_metric;
	char buf[256], iface[16];
	int i;
	int ret = -1;

	if (!name || !dst || !gateway || !mask)
		return -1;
	if (inet_pton(AF_INET, dst, &addr_dst) < 0
	 || inet_pton(AF_INET, gateway, &addr_gate) < 0
	 || inet_pton(AF_INET, mask, &addr_mask) < 0)
		return -1;

	if(fp) {
		while(fgets(buf, sizeof(buf), fp)) {
			if(!strncmp(buf, "Iface", 5))
				continue;

			i = sscanf(buf, "%s %x %x %*s %*s %*s %d %x",
					iface, &rtr_dst, &rtr_gate, &rtr_metric, &rtr_mask);
			if (i != 4)
				break;

			if( !strcmp(name, iface) && rtr_metric == metric && addr_dst == rtr_dst
			 && addr_gate == rtr_gate && addr_mask == rtr_mask) {
				ret = 1;
			}
		}
		if (ret != 1)
			ret = 0;
		fclose(fp);
	}
	return (ret);
}

/* configure loopback interface */
void config_loopback(void)
{
	/* Bring up loopback interface */
	ifconfig("lo", IFUP, "127.0.0.1", "255.0.0.0");
#ifdef RTCONFIG_DNSPRIVACY
	ifconfig("lo:0", IFUP, "127.0.1.1", "255.0.0.0");
#endif

	/* Add to routing table */
	route_add("lo", 0, "127.0.0.0", "0.0.0.0", "255.0.0.0");
}

#ifdef RTCONFIG_IPV6
static int ipv6_route_manip(int cmd, const char *name, int metric, const char *dst, const char *gateway, int flags)
{
	char addr[INET6_ADDRSTRLEN], *ptr;
	int s, err = 0;
	struct in6_rtmsg rt;
	
	_dprintf("%s: cmd=%s name=%s addr=%s gateway=%s metric=%d\n",
		__FUNCTION__, cmd == SIOCADDRT ? "ADD" : "DEL", name, dst, gateway, metric);

	/* Open a raw socket to the kernel */
	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0)
		return errno;

	/* Fill in rtentry */
	memset(&rt, 0, sizeof(rt));
	if (dst) {
		strlcpy(addr, dst, sizeof(addr));
		ptr = strchr(addr, '/');
		if (ptr) {
			*ptr++ = '\0';
			rt.rtmsg_dst_len = atoi(ptr);
		} else
			rt.rtmsg_dst_len = 128;
		inet_pton(AF_INET6, addr, &rt.rtmsg_dst);
	}
	if (gateway)
		inet_pton(AF_INET6, gateway, &rt.rtmsg_gateway);
	rt.rtmsg_metric = metric;
	rt.rtmsg_flags = RTF_UP;
	if (!IN6_IS_ADDR_UNSPECIFIED(&rt.rtmsg_gateway))
		rt.rtmsg_flags |= RTF_GATEWAY;
	if (rt.rtmsg_dst_len == 128)
		rt.rtmsg_flags |= RTF_HOST;
	rt.rtmsg_flags |= flags;

	if (name) {
		rt.rtmsg_ifindex = if_nametoindex(name);
		if (rt.rtmsg_ifindex == 0) {
			err = errno;
			perror(name);
			goto error;
		}
	}

	if (ioctl(s, cmd, &rt) < 0) {
		err = errno;
		perror(name);
	}

error:
	close(s);
	return err;
}

int _ipv6_route_add(const char *name, int metric, const char *dst, const char *gateway, int flags)
{
	return ipv6_route_manip(SIOCADDRT, name, metric, dst, gateway, flags);
}

int ipv6_route_add(const char *name, int metric, const char *dst, const char *gateway)
{
	return ipv6_route_manip(SIOCADDRT, name, metric, dst, gateway, 0);
}

int ipv6_route_del(const char *name, int metric, const char *dst, const char *gateway)
{
	return ipv6_route_manip(SIOCDELRT, name, metric, dst, gateway, 0);
}

int ipv6_mapaddr4(struct in6_addr *addr6, int ip6len, struct in_addr *addr4, int ip4mask)
{
	int i = ip6len >> 5;
	int m = ip6len & 0x1f;
	int ret = ip6len + 32 - ip4mask;
	u_int32_t addr = 0;
	u_int32_t mask = 0xffffffffUL << ip4mask;

	if (ip6len > 128 || ip4mask > 32 || ret > 128)
		return 0;
	if (ip4mask == 32)
		return ret;

	if (addr4)
		addr = ntohl(addr4->s_addr) << ip4mask;

	addr6->s6_addr32[i] &= ~htonl(mask >> m);
	addr6->s6_addr32[i] |= htonl(addr >> m);
	if (m) {
		i++;
		addr6->s6_addr32[i] &= ~htonl(mask << (32 - m));
		addr6->s6_addr32[i] |= htonl(addr << (32 - m));
	}

	return ret;
}
#endif

#ifdef RTCONFIG_MULTISERVICE_WAN
static void _mswan_stb_add()
{
#ifdef RTCONFIG_DUALWAN
	int wan_type = get_dualwan_primary();
#endif
	int i = 0;
	char wan_prefix[16] = {0};
	int bridge_enable = 0;

#ifdef RTCONFIG_DUALWAN
	if (wan_type != WANS_DUALWAN_IF_WAN
	 && wan_type != WANS_DUALWAN_IF_WAN2
	)
		return;
#endif

	for (i = 1; i < WAN_MULTISRV_MAX; i++) {
		snprintf(wan_prefix, sizeof(wan_prefix), "wan%d_", get_ms_wan_unit(WAN_UNIT_FIRST, i));
		if (nvram_pf_get_int(wan_prefix, "auto_stb"))
			return;
		if (nvram_pf_get_int(wan_prefix, "enable") && nvram_pf_match(wan_prefix, "proto", "bridge"))
			bridge_enable = 1;
	}

	if (bridge_enable == 0) {
		for (i = 1; i < WAN_MULTISRV_MAX; i++) {
			snprintf(wan_prefix, sizeof(wan_prefix), "wan%d_", get_ms_wan_unit(WAN_UNIT_FIRST, i));
			if (nvram_pf_get_int(wan_prefix, "enable"))
				continue;
			nvram_pf_set(wan_prefix, "enable", "1");
			nvram_pf_set(wan_prefix, "proto", "bridge");
			nvram_pf_set(wan_prefix, "auto_stb", "1");
			break;
		}
	}
}
static void _mswan_stb_del()
{
#ifdef RTCONFIG_DUALWAN
	int wan_type = get_dualwan_primary();
#endif
	int i = 0;
	char wan_prefix[16] = {0};

#ifdef RTCONFIG_DUALWAN
	if (wan_type != WANS_DUALWAN_IF_WAN
	 && wan_type != WANS_DUALWAN_IF_WAN2
	)
		return;
#endif

	for (i = 1; i < WAN_MULTISRV_MAX; i++) {
		snprintf(wan_prefix, sizeof(wan_prefix), "wan%d_", get_ms_wan_unit(WAN_UNIT_FIRST, i));
		if (nvram_pf_get_int(wan_prefix, "auto_stb")) {
			nvram_pf_set(wan_prefix, "enable", "0");
			nvram_pf_unset(wan_prefix, "auto_stb");
			break;
		}
	}
}
#endif

/* configure/start vlan interface(s) based on nvram settings */
int start_vlan(void)
{
#if !defined(HND_ROUTER) && !defined(BLUECAVE)
	int s;
	struct ifreq ifr;
	int i, j;
	char ea[ETHER_ADDR_LEN];

	if ((strtoul(nvram_safe_get("boardflags"), NULL, 0) & BFL_ENETVLAN) == 0) return 0;
#endif
#if !defined(BLUECAVE)
	/* set vlan i/f name to style "vlan<ID>" */
	eval("vconfig", "set_name_type", "VLAN_PLUS_VID_NO_PAD");
#endif

#if !defined(HND_ROUTER) && !defined(BLUECAVE)
	/* create vlan interfaces */
	if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
		return errno;

	for (i = 0; i <= VLAN_MAXVID; i ++) {
		char nvvar_name[16];
		char vlan_id[16];
		char *hwname, *hwaddr;
		char prio[8];
#ifdef RTCONFIG_BCMARM
		struct ethtool_drvinfo info;
#endif
		/* get the address of the EMAC on which the VLAN sits */
		snprintf(nvvar_name, sizeof(nvvar_name), "vlan%dhwname", i);
		if (!(hwname = nvram_get(nvvar_name)))
			continue;

		snprintf(nvvar_name, sizeof(nvvar_name), "%smacaddr", hwname);
		if (!(hwaddr = nvram_get(nvvar_name)))
			continue;

		ether_atoe(hwaddr, (unsigned char *) ea);
		/* find the interface name to which the address is assigned */
		for (j = 1; j <= DEV_NUMIFS; j ++) {
			ifr.ifr_ifindex = j;
			if (ioctl(s, SIOCGIFNAME, &ifr))
				continue;		
			if (ioctl(s, SIOCGIFHWADDR, &ifr))
				continue;
			if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER)
				continue;
#ifdef RTCONFIG_BCMARM
			if (bcmp(ifr.ifr_hwaddr.sa_data, ea, ETHER_ADDR_LEN))
				continue;

			/* Get driver info, it can handle both et0 and et2 have same MAC */
			memset(&info, 0, sizeof(info));
			info.cmd = ETHTOOL_GDRVINFO;
			ifr.ifr_data = (caddr_t)&info;
			if (ioctl(s, SIOCETHTOOL, &ifr) < 0)
				continue;

			if (strcmp(info.driver, hwname) == 0)
				break;
#else
			if (!bcmp(ifr.ifr_hwaddr.sa_data, ea, ETHER_ADDR_LEN))
				break;
#endif
		}

		if (j > DEV_NUMIFS)
			continue;
		if (ioctl(s, SIOCGIFFLAGS, &ifr))
			continue;
		if (!(ifr.ifr_flags & IFF_UP))
			ifconfig(ifr.ifr_name, IFUP, 0, 0);
		/* create the VLAN interface */
		snprintf(vlan_id, sizeof(vlan_id), "%d", i);
		eval("vconfig", "add", ifr.ifr_name, vlan_id);

		/* setup ingress map (vlan->priority => skb->priority) */
		snprintf(vlan_id, sizeof(vlan_id), "vlan%d", i);
		for (j = 0; j < VLAN_NUMPRIS; j ++) {
			snprintf(prio, sizeof(prio), "%d", j);
			eval("vconfig", "set_ingress_map", vlan_id, prio, prio);
		}
	}
	close(s);
#endif

#if (defined(RTCONFIG_QCA) || defined(RTCONFIG_RALINK))
	if (!nvram_match("switch_wantag", "none")
	 && !nvram_match("switch_wantag", "")
	 && !nvram_match("switch_wantag", "hinet")
	 && !nvram_match("switch_wantag", "nowtv")) {
		char wan_base_if[IFNAMSIZ] = "";

		strlcpy(wan_base_if, get_wan_base_if(), sizeof(wan_base_if));
		set_wan_tag(wan_base_if);
	}
#endif
#ifdef RTCONFIG_DUPVIF
	if (nvram_match("switch_wantag", "hinet_mesh")) {
		if (!module_loaded("dupvif")) {
			char prefix[] = "wanXXX_", params[16];
			snprintf(prefix, sizeof(prefix), "wan%d_", WAN_UNIT_IPTV);
			snprintf(params, sizeof(params), "nic=%s", nvram_pf_get(prefix, "ifname"));
			modprobe("dupvif", params);
		}
	}
#endif
#ifdef CONFIG_BCMWL5
#ifndef HND_ROUTER
	if(!nvram_match("switch_wantag", "none")&&!nvram_match("switch_wantag", "")&&!nvram_match("switch_wantag", "hinet"))
		set_wan_tag((char *) &ifr.ifr_name);
#endif
#endif
#if !defined(HND_ROUTER) && (defined(RTCONFIG_RGMII_BRCM5301X) || defined(RTCONFIG_BCM_7114))
	switch (get_model()) {
		case MODEL_RTAC88U:
		case MODEL_RTAC3100:
		case MODEL_RTAC5300:
			break;
		default:
			// port 5 ??
			eval("et", "robowr", "0x0", "0x5d", "0xfb", "1");
			// port 4 link down
			eval("et", "robowr", "0x0", "0x5c", "0x4a", "1");
	}
#endif

#ifdef RTCONFIG_PORT_BASED_VLAN
#ifndef HND_ROUTER
	set_port_based_vlan_config(&ifr.ifr_name);
#endif
#endif

#if defined(HND_ROUTER)
	if (!nvram_match("switch_wantag", "")
		&& (nvram_get_int("switch_stb_x") > 0 || (nvram_get_int("switch_stb_x") == 0 && nvram_get_int("switch_wan0tagid") > 0))) {
		/*
			case 1: STB != 0
			case 2: STB == 0 && wan tag != 0
		*/
		ifconfig(wan_if_eth(), IFUP, NULL, NULL);
		set_wan_tag(wan_if_eth());
	}
#ifdef RTCONFIG_MULTISERVICE_WAN
	if (nvram_match("switch_wantag", "none")
#if defined(RTCONFIG_AMAS)
		&& (nvram_get_int("re_mode") != 1)
#endif
	) {
		if (nvram_get_int("switch_stb_x") > 0)
			_mswan_stb_add();
		else
			_mswan_stb_del();
	}
#endif

#elif defined(BLUECAVE)
	if(!nvram_match("switch_wantag", "") && (nvram_get_int("switch_stb_x") > 0 || nvram_match("switch_wantag", "unifi_biz") || 
		nvram_match("switch_wantag", "stuff_fibre") || nvram_match("switch_wantag", "spark") ||
		nvram_match("switch_wantag", "2degrees") || nvram_match("switch_wantag", "slingshot") ||
		nvram_match("switch_wantag", "orcon") || nvram_match("switch_wantag", "voda_nz") ||
		nvram_match("switch_wantag", "tpg") || nvram_match("switch_wantag", "iinet") ||
		nvram_match("switch_wantag", "aapt") || nvram_match("switch_wantag", "intronode") ||
		nvram_match("switch_wantag", "iprimus") || nvram_match("switch_wantag", "manual") ||
		nvram_match("switch_wantag", "kpn_nl") || nvram_match("switch_wantag", "centurylink") ||
		nvram_match("switch_wantag", "actrix") || nvram_match("switch_wantag", "jastel") ||
		nvram_match("switch_wantag", "google_fiber"))) {
		char *wan_base_if = "eth1";
		ifconfig(wan_base_if, IFUP, NULL, NULL);
		set_wan_tag(wan_base_if);
	}
#endif
	return 0;
}

/* stop/rem vlan interface(s) based on nvram settings */
int stop_vlan(void)
{
#if !defined(HND_ROUTER) && !defined(BLUECAVE)
	int i;
	char nvvar_name[16];
	char vlan_id[16];
	char *hwname;

	if ((strtoul(nvram_safe_get("boardflags"), NULL, 0) & BFL_ENETVLAN) == 0) return 0;
	
#ifdef RTCONFIG_DUPVIF
	if (nvram_match("switch_wantag", "hinet_mesh")) {
		if (module_loaded("dupvif")) {
			modprobe_r("dupvif");
		}
	}
#endif

	for (i = 0; i <= VLAN_MAXVID; i ++) {
		/* get the address of the EMAC on which the VLAN sits */
		snprintf(nvvar_name, sizeof(nvvar_name), "vlan%dhwname", i);
		if (!(hwname = nvram_get(nvvar_name)))
			continue;

		/* remove the VLAN interface */
		snprintf(vlan_id, sizeof(vlan_id), "vlan%d", i);
		eval("vconfig", "rem", vlan_id);
	}
#endif

	return 0;
}

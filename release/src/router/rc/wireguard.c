/*
 * Copyright 2021, ASUSTeK Inc.
 * All Rights Reserved.
 *
 */

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "rc.h"

#define WG_DIR_CONF    "/etc/wg"
#define WG_KEY_SIZE    64
#define WG_TAG         "WireGuard"
#define WG_PRG_CRU     "/usr/sbin/cru"
#define WG_WAIT_SYNC   5
#define WGC_CHK_EP     "WGC_CHK_EP"

typedef enum wg_type{
	WG_TYPE_SERVER = 0,
	WG_TYPE_CLIENT
}wg_type_t;

static int _wg_if_exist(char *ifname)
{
#if 1
	return if_nametoindex(ifname) ? 1 : 0;
#else
	char path[128] = {0};

	if(ifname[0] == '\0')
		return 0;

	snprintf(path, sizeof(path), "/sys/class/net/%s/ifindex", ifname);
	return (f_exists(path)) ? 1 : 0;
#endif
}

static void _wg_tunnel_create(char* prefix, char* ifname, char* conf_path)
{
	char addr[64] = {0};
	char buf[64] = {0};
	char *next = NULL;

	eval("ip", "link", "add", "dev", ifname, "type", "wireguard");

	snprintf(addr, sizeof(addr), "%s", nvram_pf_safe_get(prefix, "addr"));
	foreach_44(buf, addr, next)
		eval("ip", "address", "add", buf, "dev", ifname);

	eval("wg", "setconf", ifname, conf_path);

	if (nvram_pf_get_int(prefix, "mtu"))
		eval("ip", "link", "set", ifname, "mtu", nvram_pf_safe_get(prefix, "mtu"));

	eval("ip", "link", "set", "up", "dev", ifname);
}

static void _wg_tunnel_delete(char* ifname)
{
	if (_wg_if_exist(ifname))
		eval("ip", "link", "del", "dev", ifname);
}

static int _wg_resolv_ep(const char* ep_addr, char* buf, size_t len)
{
	struct addrinfo hints, *servinfo, *p;
	int ret;
	const char* addr = NULL;
	char tmp[64] = {0};

	memset(buf, 0 , len);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if( (ret = getaddrinfo(ep_addr, NULL , &hints , &servinfo)) != 0)
	{
		_dprintf("getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next)
	{
		addr = NULL;
		if (p->ai_family == AF_INET6) {
			if (ipv6_enabled())
				addr = inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)(p->ai_addr))->sin6_addr), tmp, INET6_ADDRSTRLEN);
		}
		else
			addr = inet_ntop(AF_INET, &(((struct sockaddr_in *)(p->ai_addr))->sin_addr), tmp, INET_ADDRSTRLEN);
		if (addr)
		{
			strlcat(buf, addr, len);
			strlcat(buf, " ", len);
		}
	}

	if (ret == 0)
		freeaddrinfo(servinfo);

	trim_r(buf);

	return 0;
}

static void _wg_client_ep_route_add(char* prefix, int table)
{
	char table_str[8] = {0};
	char wan_gateway[16] = {0};
	char wan6_gateway[64] = {0};
	char wan_ifname[8] = {0};
	char wan6_ifname[8] = {0};
	char buf[1024] = {0};
	char addr[64] = {0};
	char* p = NULL;
	int v6 = 0;

	snprintf(table_str, sizeof(table_str), "%d", table);

	snprintf(buf, sizeof(buf), "wan%d_gateway", wan_primary_ifunit());
	snprintf(wan_gateway, sizeof(wan_gateway), "%s", nvram_safe_get(buf));
	snprintf(wan6_gateway, sizeof(wan6_gateway), "%s", ipv6_gateway_address());
	snprintf(wan_ifname, sizeof(wan_ifname), "%s", get_wanface());
	snprintf(wan6_ifname, sizeof(wan6_ifname), "%s", get_wan6face());
	snprintf(buf, sizeof(buf), "%s", nvram_pf_safe_get(prefix, "ep_addr_r"));

	foreach(addr, buf, p)
	{
		v6 = is_valid_ip6(addr);
		if (table > 0 && table < 256)
			eval("ip", "route", "add", addr,
				"via", v6 ? wan6_gateway : wan_gateway,
				"dev", v6 ? wan6_ifname : wan_ifname, "table", table_str);
		else
			eval("ip", "route", "add", addr,
				"via", v6 ? wan6_gateway : wan_gateway,
				"dev", v6 ? wan6_ifname : wan_ifname);
	}
}

static void _wg_client_ep_route_del(char* prefix, int table)
{
	char table_str[8] = {0};
	char buf[1024] = {0};
	char addr[64] = {0};
	char* p = NULL;

	snprintf(table_str, sizeof(table_str), "%d", table);
	snprintf(buf, sizeof(buf), "%s", nvram_pf_safe_get(prefix, "ep_addr_r"));

	foreach(addr, buf, p)
	{
		if (table > 0 && table < 256)
			eval("ip", "route", "del", addr, "table", table_str);
		else
			eval("ip", "route", "del", addr);
	}
}

static void _wg_client_check_conf(char* prefix)
{
	char addr[64] = {0};
	char buf[64] = {0};
	char *next = NULL;
	char *p = NULL;

	// addr
	snprintf(addr, sizeof(addr), "%s", nvram_pf_safe_get(prefix, "addr"));
	foreach_44(buf, addr, next)
	{
		if ((p = strchr(buf, '/'))) *p = '\0';
		if (is_ip4_in_use(buf))
			logmessage_normal("WireGuard", "Other interface use %s too.", buf);
	}

	// route
}

static void _wg_config_route(char* prefix, char* ifname, int table)
{
	char aips[4096] = {0};
	char buf[128] = {0};
	char *p = NULL;
	char table_str[8] = {0};
	FILE *fp = NULL;
	char cmd[256] = {0};

	snprintf(table_str, sizeof(table_str), "%d", table);

	if (table > 0 && table < 256)
	{// copy from main
		eval("ip", "route", "flush", "table", table_str);
		system("ip route show table main > /tmp/route_tmp");
		fp = fopen("/tmp/route_tmp", "r");
		if(fp)
		{
			while(fgets(buf, sizeof(buf), fp))
			{
				snprintf(cmd, sizeof(cmd), "ip route add %s table %d ", trim_r(buf), table);
				//_dprintf("[%s]\n", cmd);
				system(cmd);
			}
			fclose(fp);
		}
		unlink("/tmp/route_tmp");
	}

	// Allowed IPs
	snprintf(aips, sizeof(aips), "%s", nvram_pf_safe_get(prefix, "aips"));
	snprintf(buf, sizeof(buf), "%s/%sroute", WG_DIR_CONF, prefix);
	f_write_string(buf, aips, 0, 0);
	foreach_44(buf, aips, p)
	{
		_dprintf("%s: [%s]\n", __FUNCTION__, buf);
		if (strchr(buf, '/') == NULL)
			continue;
		if (!strcmp(buf, "0.0.0.0/0"))
		{
			if (table > 0 && table < 256)
			{
				eval("ip", "route", "add", "0.0.0.0/1", "dev", ifname, "table", table_str);
				eval("ip", "route", "add", "128.0.0.0/1", "dev", ifname, "table", table_str);
			}
			else
			{
				eval("ip", "route", "add", "0.0.0.0/1", "dev", ifname);
				eval("ip", "route", "add", "128.0.0.0/1", "dev", ifname);
			}
		}
		else if (!strcmp(buf, "::/0"))
		{
			char *dst[] = { "::/3", "2000::/4", "3000::/4", "fc00::/7", NULL };
			int i = 0;
			while (dst[i])
			{
				if (table > 0 && table < 256)
					eval("ip", "route", "add", dst[i], "dev", ifname, "table", table_str);
				else
					eval("ip", "route", "add", dst[i], "dev", ifname);
				i++;
			}
		}
		else
		{
			if (table > 0 && table < 256)
				eval("ip", "route", "add", buf, "dev", ifname, "table", table_str);
			else
				eval("ip", "route", "add", buf, "dev", ifname);
		}
	}
}

static void _wg_config_sysdeps(int wg_enable)
{
#ifdef RTCONFIG_HND_ROUTER
	if (wg_enable)
	{
		nvram_set_int("fc_disable", 1);
		fc_fini();
	}
	else
	{
		hnd_nat_ac_init(0);
	}
#endif
}

#ifdef RTCONFIG_MULTILAN_CFG
static void _wg_server_nf_bind_wan(const char* wgs_ifname, int add)
{
	char wg_ifname[32] = {0};
	char wan_ifname[32] = {0};
	strlcpy(wg_ifname, wgs_ifname, sizeof(wg_ifname));
	strlcpy(wan_ifname, get_wan_ifname(wan_primary_ifunit()), sizeof(wan_ifname));
	if (wan_ifname[0] != '\0')
	{
		eval("iptables", (add)?"-I":"-D", "WGSF", "-i", wg_ifname, "-o", wan_ifname, "-j", "ACCEPT");
		eval("iptables", (add)?"-I":"-D", "WGSF", "-o", wg_ifname, "-i", wan_ifname, "-j", "ACCEPT");
	}
	strlcpy(wan_ifname, get_wan6_ifname(wan_primary_ifunit()), sizeof(wan_ifname));
	if (wan_ifname[0] != '\0')
	{
		eval("ip6tables", (add)?"-I":"-D", "WGSF", "-i", wg_ifname, "-o", wan_ifname, "-j", "ACCEPT");
		eval("ip6tables", (add)?"-I":"-D", "WGSF", "-o", wg_ifname, "-i", wan_ifname, "-j", "ACCEPT");
	}
}

static void _wg_server_nf_bind_sdn(FILE* fp, const char* wgs_ifname, const char* sdn_ifname)
{
	if (fp) {
		if (sdn_ifname) {
			fprintf(fp, "iptables -I WGSF -i %s -o %s -j ACCEPT\n", wgs_ifname, sdn_ifname);
			fprintf(fp, "iptables -I WGSF -o %s -i %s -j ACCEPT\n", wgs_ifname, sdn_ifname);
			fprintf(fp, "iptables -A WGSF -o %s -j DROP\n", wgs_ifname);
			fprintf(fp, "ip6tables -I WGSF -i %s -o %s -j ACCEPT\n", wgs_ifname, sdn_ifname);
			fprintf(fp, "ip6tables -I WGSF -o %s -i %s -j ACCEPT\n", wgs_ifname, sdn_ifname);
			fprintf(fp, "ip6tables -A WGSF -o %s -j DROP\n", wgs_ifname);
		}
		else {
			fprintf(fp, "iptables -I WGSF -i %s -j ACCEPT\n", wgs_ifname);
			fprintf(fp, "iptables -I WGSF -o %s -j ACCEPT\n", wgs_ifname);
			fprintf(fp, "ip6tables -I WGSF -i %s -j ACCEPT\n", wgs_ifname);
			fprintf(fp, "ip6tables -I WGSF -o %s -j ACCEPT\n", wgs_ifname);
		}
	}
}
#endif

static void _wg_server_nf_del_nat6(const char* ifname)
{
	char path[128] = {0};

	snprintf(path, sizeof(path), "%s/fw_%s_nat6.sh", WG_DIR_CONF, ifname);
	if(f_exists(path)) {
		eval("sed", "-i", "s/-I/-D/", path);
		eval("sed", "-i", "s/-A/-D/", path);
		eval(path);
		unlink(path);
	}

}
static void _wg_server_nf_add_nat6(const char* prefix, const char* ifname)
{
	FILE* fp;
	char path[128] = {0};
	int c_unit = 0;
	char c_prefix[16] = {0};
	char c_addr[64] = {0};
	char tmp[64] = {0};
	char *next = NULL;
	char *p = NULL;
	char wan6_ifname[IFNAMSIZ] = {0};

	if (nvram_pf_get_int(prefix, "nat6"))
	{
		snprintf(path, sizeof(path), "%s/fw_%s_nat6.sh", WG_DIR_CONF, ifname);
		fp = fopen(path, "w");
		if (fp)
		{
			fprintf(fp, "#!/bin/sh\n\n");
			strlcpy(wan6_ifname, get_wan6_ifname(wan_primary_ifunit()), sizeof(wan6_ifname));

			for (c_unit = 1; c_unit <= WG_SERVER_CLIENT_MAX; c_unit++)
			{
				snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", prefix, c_unit);
				if (nvram_pf_get_int(c_prefix, "enable") == 0)
					continue;
				snprintf(c_addr, sizeof(c_addr), "%s", nvram_pf_safe_get(c_prefix, "addr"));
				foreach_44 (tmp, c_addr, next)
				{
					if ((p = strchr(tmp, '/')) != NULL)
						*p = '\0';
					if (is_valid_ip6(tmp) > 0)
						fprintf(fp, "ip6tables -t nat -I POSTROUTING -s %s -o %s -j MASQUERADE\n"
							, tmp, wan6_ifname);
				}
			}
			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
		}
	}
}

static void _wg_server_nf_add(int unit, const char* prefix, const char* ifname)
{
	FILE* fp;
	char path[128] = {0};
	char wan6_ifname[IFNAMSIZ] = {0};
	char wan_ifname[IFNAMSIZ] = {0};
	int wan_service = get_ipv4_service();
#ifdef RTCONFIG_MULTILAN_CFG
	int vpns_idx = get_vpns_idx_by_proto_unit(VPN_PROTO_WG, unit);
	MTLAN_T *pmtl = (MTLAN_T *)INIT_MTLAN(sizeof(MTLAN_T));
	size_t  mtl_sz = 0;
	int i;
#endif

	strlcpy(wan_ifname, get_wan_ifname(wan_primary_ifunit()), sizeof(wan_ifname));
	strlcpy(wan6_ifname, get_wan6_ifname(wan_primary_ifunit()), sizeof(wan6_ifname));

	snprintf(path, sizeof(path), "%s/fw_%s.sh", WG_DIR_CONF, ifname);
	fp = fopen(path, "w");
	if (fp)
	{
		fprintf(fp, "#!/bin/sh\n\n");
		fprintf(fp, "iptables -t nat -A LOCALSRV -p udp --dport %d -j ACCEPT\n", nvram_pf_get_int(prefix, "port"));
		if (wan_service != WAN_DHCP && wan_service != WAN_STATIC)
		{
			fprintf(fp, "iptables -t nat -A VSERVER -p udp -m udp --dport %d -j DNAT --to-destination %s:%d\n",
				nvram_pf_get_int(prefix, "port"), nvram_safe_get("lan_ipaddr"), nvram_pf_get_int(prefix, "port"));
		}
		fprintf(fp, "iptables -A WGSI -p udp --dport %d -j ACCEPT\n", nvram_pf_get_int(prefix, "port"));
		fprintf(fp, "ip6tables -A WGSI -p udp --dport %d -j ACCEPT\n", nvram_pf_get_int(prefix, "port"));
		fprintf(fp, "iptables -A WGSI -i %s -j ACCEPT\n", ifname);
		fprintf(fp, "ip6tables -A WGSI -i %s -j ACCEPT\n", ifname);
#if !defined(RTCONFIG_MULTILAN_CFG)
		if (nvram_pf_get_int(prefix, "lanaccess") == 0)
		{//wan only currently
			fprintf(fp, "iptables -I WGSF -i %s -o %s -j ACCEPT\n", ifname, wan_ifname);
			fprintf(fp, "iptables -I WGSF -o %s -i %s -j ACCEPT\n", ifname, wan_ifname);
			fprintf(fp, "ip6tables -I WGSF -i %s -o %s -j ACCEPT\n", ifname, wan6_ifname);
			fprintf(fp, "ip6tables -I WGSF -o %s -i %s -j ACCEPT\n", ifname, wan6_ifname);
		}
		else
		{
			fprintf(fp, "iptables -I WGSF -i %s -j ACCEPT\n", ifname);
			fprintf(fp, "iptables -I WGSF -o %s -j ACCEPT\n", ifname);
			fprintf(fp, "ip6tables -I WGSF -i %s -j ACCEPT\n", ifname);
			fprintf(fp, "ip6tables -I WGSF -o %s -j ACCEPT\n", ifname);
		}
#endif
		fclose(fp);
		chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
		eval(path);
	}

	_wg_server_nf_add_nat6(prefix, ifname);

#ifdef RTCONFIG_MULTILAN_CFG
	get_mtlan_by_idx(SDNFT_TYPE_VPNS, vpns_idx, pmtl, &mtl_sz);
	if (mtl_sz) {
		for (i = 0; i < mtl_sz; i++) {
			snprintf(path, sizeof(path), "%s/fw_%s_sdn%d.sh", WG_DIR_CONF, ifname, pmtl[i].nw_t.idx);
			fp = fopen(path, "w");
			if(fp) {
				fprintf(fp, "#!/bin/sh\n\n");
				_wg_server_nf_bind_sdn(fp, ifname, pmtl[i].nw_t.ifname);
				fclose(fp);
				chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
				eval(path);
			}
		}
		_wg_server_nf_bind_wan(ifname, 1);
	}
	else {
		snprintf(path, sizeof(path), "%s/fw_%s_none.sh", WG_DIR_CONF, ifname);
		fp = fopen(path, "w");
		if(fp) {
			fprintf(fp, "#!/bin/sh\n\n");
			if (nvram_pf_get_int(prefix, "lanaccess") == 0) { //wan only currently
				fprintf(fp, "iptables -I WGSF -i %s -o %s -j ACCEPT\n", ifname, wan_ifname);
				fprintf(fp, "iptables -I WGSF -o %s -i %s -j ACCEPT\n", ifname, wan_ifname);
				fprintf(fp, "ip6tables -I WGSF -i %s -o %s -j ACCEPT\n", ifname, wan6_ifname);
				fprintf(fp, "ip6tables -I WGSF -o %s -i %s -j ACCEPT\n", ifname, wan6_ifname);
			}
			else {
				_wg_server_nf_bind_sdn(fp, ifname, NULL);
			}
			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
		}
	}
	FREE_MTLAN((void *)pmtl);
#endif
}

#ifdef RTCONFIG_MULTILAN_CFG
static void _wg_client_nf_bind_sdn(FILE* fp, const char* wgc_ifname, const char* sdn_ifname)
{
	if (fp) {
		if (sdn_ifname) {
			fprintf(fp, "iptables -I WGCF -i %s -o %s -j ACCEPT\n", wgc_ifname, sdn_ifname);
			fprintf(fp, "iptables -I WGCF -o %s -i %s -j ACCEPT\n", wgc_ifname, sdn_ifname);
			fprintf(fp, "iptables -I WGCF -o %s -i %s -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu\n", wgc_ifname, sdn_ifname);
			fprintf(fp, "iptables -A WGCF -o %s -j DROP\n", wgc_ifname);
			fprintf(fp, "ip6tables -I WGCF -i %s -o %s -j ACCEPT\n", wgc_ifname, sdn_ifname);
			fprintf(fp, "ip6tables -I WGCF -o %s -i %s -j ACCEPT\n", wgc_ifname, sdn_ifname);
			fprintf(fp, "ip6tables -I WGCF -o %s -i %s -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu\n", wgc_ifname, sdn_ifname);
			fprintf(fp, "ip6tables -A WGCF -o %s -j DROP\n", wgc_ifname);
		}
		else {
			fprintf(fp, "iptables -I WGCF -i %s -j ACCEPT\n", wgc_ifname);
			fprintf(fp, "iptables -I WGCF -o %s -j ACCEPT\n", wgc_ifname);
			fprintf(fp, "iptables -I WGCF -o %s -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu\n", wgc_ifname);
			fprintf(fp, "ip6tables -I WGCF -i %s -j ACCEPT\n", wgc_ifname);
			fprintf(fp, "ip6tables -I WGCF -o %s -j ACCEPT\n", wgc_ifname);
			fprintf(fp, "ip6tables -I WGCF -o %s -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu\n", wgc_ifname);
		}
	}
}
#endif

static void _wg_client_nf_add(int unit, char* prefix, char* ifname)
{
	FILE* fp;
	char path[128] = {0};
#ifdef RTCONFIG_MULTILAN_CFG
	int vpnc_idx = get_vpnc_idx_by_proto_unit(VPN_PROTO_WG, unit);
	MTLAN_T *pmtl = (MTLAN_T *)INIT_MTLAN(sizeof(MTLAN_T));
	size_t  mtl_sz = 0;
	int i;
#endif

	snprintf(path, sizeof(path), "%s/fw_%s.sh", WG_DIR_CONF, ifname);
	fp = fopen(path, "w");
	if (fp)
	{
		fprintf(fp, "#!/bin/sh\n\n");

		fprintf(fp, "iptables -I WGCI -i %s -j ACCEPT\n", ifname);
		fprintf(fp, "ip6tables -I WGCI -i %s -j ACCEPT\n", ifname);
#if !defined(RTCONFIG_MULTILAN_CFG)
		fprintf(fp, "iptables -I WGCF -i %s -j ACCEPT\n", ifname);
		fprintf(fp, "iptables -I WGCF -o %s -j ACCEPT\n", ifname);
		fprintf(fp, "iptables -I WGCF -o %s -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu\n", ifname);
		fprintf(fp, "ip6tables -I WGCF -i %s -j ACCEPT\n", ifname);
		fprintf(fp, "ip6tables -I WGCF -o %s -j ACCEPT\n", ifname);
		fprintf(fp, "ip6tables -I WGCF -o %s -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu\n", ifname);
#endif

		fclose(fp);
		chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
		eval(path);
	}

	if (nvram_pf_get_int(prefix, "nat"))
	{
		char addr[64] = {0};
		char tmp[64] = {0};
		char *next = NULL;
		char *p = NULL;
		int ret = -1;

		snprintf(path, sizeof(path), "%s/fw_%s_nat.sh", WG_DIR_CONF, ifname);
		fp = fopen(path, "w");
		if (fp)
		{
			fprintf(fp, "#!/bin/sh\n\n");

			snprintf(addr, sizeof(addr), "%s", nvram_pf_safe_get(prefix, "addr"));
			foreach_44 (tmp, addr, next)
			{
				if ((p = strchr(tmp, '/')) != NULL)
					*p = '\0';
				ret = is_valid_ip(tmp);
				if (ret > 0)
					fprintf(fp, "%s -t nat -I POSTROUTING ! -s %s -o %s -j MASQUERADE\n",
						(ret > 1) ? "ip6tables" : "iptables", tmp, ifname);
			}

			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
		}
	}

#ifdef RTCONFIG_MULTILAN_CFG
	get_mtlan_by_idx(SDNFT_TYPE_VPNC, vpnc_idx, pmtl, &mtl_sz);
	if (mtl_sz) {
		for (i = 0; i < mtl_sz; i++) {
			snprintf(path, sizeof(path), "%s/fw_%s_sdn%d.sh", WG_DIR_CONF, ifname, pmtl[i].nw_t.idx);
			fp = fopen(path, "w");
			if(fp) {
				fprintf(fp, "#!/bin/sh\n\n");
				_wg_client_nf_bind_sdn(fp, ifname, pmtl[i].nw_t.ifname);
				fclose(fp);
				chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
				eval(path);
			}
		}
	}
	else {
		snprintf(path, sizeof(path), "%s/fw_%s_none.sh", WG_DIR_CONF, ifname);
		fp = fopen(path, "w");
		if(fp) {
			fprintf(fp, "#!/bin/sh\n\n");
			_wg_client_nf_bind_sdn(fp, ifname, NULL);
			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
		}
	}
	FREE_MTLAN((void *)pmtl);
#endif
}

static void _wg_x_nf_del(const char* ifname)
{
	char path[128] = {0};
#ifdef RTCONFIG_MULTILAN_CFG
	int i;
#endif

	snprintf(path, sizeof(path), "%s/fw_%s.sh", WG_DIR_CONF, ifname);
	if(f_exists(path)) {
		eval("sed", "-i", "s/-I/-D/", path);
		eval("sed", "-i", "s/-A/-D/", path);
		eval(path);
		unlink(path);
	}

	snprintf(path, sizeof(path), "%s/fw_%s_nat.sh", WG_DIR_CONF, ifname);
	if(f_exists(path)) {
		eval("sed", "-i", "s/-I/-D/", path);
		eval("sed", "-i", "s/-A/-D/", path);
		eval(path);
		unlink(path);
	}

	_wg_server_nf_del_nat6(ifname);

#ifdef RTCONFIG_MULTILAN_CFG
	for (i = 0; i <=MTLAN_MAXINUM; i++) {
		snprintf(path, sizeof(path), "%s/fw_%s_sdn%d.sh", WG_DIR_CONF, ifname, i);
		if(f_exists(path)) {
			eval("sed", "-i", "s/-I/-D/", path);
			eval("sed", "-i", "s/-A/-D/", path);
			eval(path);
			unlink(path);
		}
	}
	snprintf(path, sizeof(path), "%s/fw_%s_none.sh", WG_DIR_CONF, ifname);
	if(f_exists(path)) {
		eval("sed", "-i", "s/-I/-D/", path);
		eval(path);
		unlink(path);
	}
#endif
}

#ifdef RTCONFIG_VPN_FUSION
static void _wg_client_dns_setup_vpnc(char* prefix, char* ifname, int vpnc_idx)
{
	char wg_dns[128] = {0};
	char vpnc_dns[128] = {0};
	char table_str[8] = {0};
	snprintf(table_str, sizeof(table_str), "%d", vpnc_idx);

	snprintf(wg_dns, sizeof(wg_dns), "%s", nvram_pf_safe_get(prefix, "dns"));
	if (wg_dns[0] != '\0')
	{
		char buf[64] = {0};
		char *next = NULL;
		char *p = NULL;
		foreach_44 (buf, wg_dns, next)
		{
			if ((p = strchr(buf, '/')) != NULL)
				*p = '\0';
			strlcat(vpnc_dns, buf, sizeof(vpnc_dns));
			strlcat(vpnc_dns, " ", sizeof(vpnc_dns));

			eval("ip", "route", "add", buf, "dev", ifname, "table", table_str);
		}

		snprintf(buf, sizeof(buf), "vpnc%d_dns", vpnc_idx);
		nvram_set(buf, trim_r(vpnc_dns));
	}
}
#else
static void _wg_client_dns_setup(char* prefix, char* ifname)
{
	FILE* fp;
	char path[128] = {0};
	char dns[128] = {0};

	snprintf(dns, sizeof(dns), "%s", nvram_pf_safe_get(prefix, "dns"));
	if (dns[0] != '\0')
	{
		char buf[64] = {0};
		char *next = NULL;
		char *p = NULL;

		snprintf(path, sizeof(path), "%s/resolv_%s.dnsmasq", WG_DIR_CONF, ifname);
		fp = fopen(path, "w");

		foreach_44 (buf, dns, next)
		{
			if ((p = strchr(buf, '/')) != NULL)
				*p = '\0';

			// resolv.dnsmasq
			if (fp)
				fprintf(fp, "server=%s\n", buf);

			// dns route
			eval("ip", "route", "add", buf, "dev", ifname);
		}

		if (fp)
			fclose(fp);
	}
}
#endif

static void _wg_client_gen_conf(char* prefix, char* path)
{
	FILE* fp;
	char priv[WG_KEY_SIZE] = {0};
	char ppub[WG_KEY_SIZE] = {0};
	char psk[WG_KEY_SIZE] = {0};
	char aips[4096] = {0};
	char ep_addr[128] = {0};
	int alive = nvram_pf_get_int(prefix, "alive");
	char *p;

	snprintf(priv, sizeof(priv), "%s", nvram_pf_safe_get(prefix, "priv"));
	snprintf(ppub, sizeof(ppub), "%s", nvram_pf_safe_get(prefix, "ppub"));
	snprintf(psk, sizeof(psk), "%s", nvram_pf_safe_get(prefix, "psk"));
	snprintf(aips, sizeof(aips), "%s", nvram_pf_safe_get(prefix, "aips"));
	snprintf(ep_addr, sizeof(ep_addr), "%s", nvram_pf_safe_get(prefix, "ep_addr_r"));

	if (priv[0] == '\0' || ppub[0] == '\0' || aips[0] == '\0')
		return;
	// TODO: dependent on ip version
	if ((p = strchr(ep_addr, ' ')))
		*p = '\0';

	fp = fopen(path, "w");
	if (fp)
	{
		fprintf(fp, "[Interface]\n"
			"PrivateKey = %s\n\n"
			"[Peer]\n"
			"PublicKey = %s\n"
			"AllowedIPs = %s\n"
			"Endpoint = %s:%d\n"
			, priv, ppub, aips
			, ep_addr, nvram_pf_get_int(prefix, "ep_port")
			);
		if (psk[0] != '\0')
			fprintf(fp, "PresharedKey = %s\n", psk);
		fprintf(fp, "PersistentKeepalive = %d\n", alive ?: 25);

		fclose(fp);
	}
}

static void _wg_server_gen_client_keys(char* s_prefix, char* c_prefix)
{
	char path[128] = {0};
	FILE* fp;

	if (nvram_pf_safe_get(c_prefix, "priv")[0] == '\0' || nvram_pf_safe_get(c_prefix, "pub")[0] == '\0')
	{
		snprintf(path, sizeof(path), "%s/ckey.sh", WG_DIR_CONF);
		fp = fopen(path, "w");
		if (fp)
		{
			fprintf(fp, "#!/bin/sh\n\n"
				"cd %s\n"
				"wg genkey > priv.key\n"
				"wg pubkey < priv.key > pub.key\n"
				"nvram set %spriv=`cat priv.key`\n"
				"nvram set %spub=`cat pub.key`\n"
				"rm *.key\n"
				, WG_DIR_CONF
				, c_prefix, c_prefix
				);
			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
			unlink(path);
		}
	}

	if (nvram_pf_get_int(s_prefix, "psk") && nvram_pf_safe_get(c_prefix, "psk")[0] == '\0')
	{
		snprintf(path, sizeof(path), "%s/pkey.sh", WG_DIR_CONF);
		fp = fopen(path, "w");
		if (fp)
		{
			fprintf(fp, "#!/bin/sh\n\n"
				"cd %s\n"
				"wg genpsk > psk.key\n"
				"nvram set %spsk=`cat psk.key`\n"
				"rm *.key\n"
				, WG_DIR_CONF
				, c_prefix
				);
			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
			unlink(path);
		}
	}
}

static void _wg_server_gen_keys(char* prefix)
{
	char path[128] = {0};
	FILE* fp;

	if (nvram_pf_safe_get(prefix, "priv")[0] == '\0' || nvram_pf_safe_get(prefix, "pub")[0] == '\0')
	{
		snprintf(path, sizeof(path), "%s/key.sh", WG_DIR_CONF);
		fp = fopen(path, "w");
		if (fp)
		{
			fprintf(fp, "#!/bin/sh\n\n"
				"cd %s\n"
				"wg genkey > priv.key\n"
				"wg pubkey < priv.key > pub.key\n"
				"nvram set %spriv=`cat priv.key`\n"
				"nvram set %spub=`cat pub.key`\n"
				"rm *.key\n"
				, WG_DIR_CONF
				, prefix, prefix
				);
			fclose(fp);
			chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
			eval(path);
			unlink(path);
		}
	}
}

static char* _wg_server_get_endpoint(char* buf, size_t len)
{
	/// Currently, Windows, iOS, Android WireGuard APP prefer IPv4
	/// even DDNS include both IPv4 and IPv6.
	if (is_private_subnet(get_wanip()) && ipv6_enabled()) {
		strlcpy(buf, (getifaddr(get_wan6face(), AF_INET6, 0))?:"", len);
		if (buf[0])
			return buf;
	}

	strlcpy(buf, get_ddns_hostname(), len);

	if (!(nvram_get_int("ddns_enable_x") && nvram_get_int("ddns_status") && strlen(buf)))
	{
		strlcpy(buf, get_wanip(), len);
		if (inet_addr_(buf) == INADDR_ANY)
			strlcpy(buf, nvram_safe_get("lan_ipaddr"), len);
	}

	return buf;
}

static void _wg_server_gen_client_conf(char* s_prefix, char* c_prefix, char* c_path)
{
	FILE* fp;
	char cpriv[WG_KEY_SIZE] = {0};
	char spub[WG_KEY_SIZE] = {0};
	char cpsk[WG_KEY_SIZE] = {0};
	char caips[4096] = {0};
	char buf[64] = {0};
	int psk = nvram_pf_get_int(s_prefix, "psk");
	int dns = nvram_pf_get_int(s_prefix, "dns");
	int alive = nvram_pf_get_int(s_prefix, "alive");

	snprintf(cpriv, sizeof(cpriv), "%s", nvram_pf_safe_get(c_prefix, "priv"));
	snprintf(spub, sizeof(spub), "%s", nvram_pf_safe_get(s_prefix, "pub"));
	snprintf(caips, sizeof(caips), "%s", nvram_pf_safe_get(c_prefix, "caips"));
	snprintf(buf, sizeof(buf), "%s", nvram_pf_safe_get(s_prefix, "addr"));

	if (cpriv[0] == '\0' || spub[0] == '\0' || buf[0] == '\0')
		return;

	if (caips[0] == '\0')
	{
		int ret = is_valid_ip(buf);
		if (ret > 0)
			strlcat(caips, (ret > 1) ? "/128" : "/32", sizeof(caips));
		else
			return;
	}

	fp = fopen(c_path, "w");
	if (fp)
	{
		fprintf(fp, "[Interface]\n"
			"PrivateKey = %s\n"
			"Address = %s\n"
			, cpriv, nvram_pf_safe_get(c_prefix, "addr")
			);

		if (dns)
		{
			char dns[64] = {0};
			char tmp[64] = {0};
			char *next = NULL;
			char *p = NULL;

			strlcpy(dns, "DNS = ", sizeof(dns));
			foreach_44 (tmp, buf, next)
			{
				if ((p = strchr(tmp, '/')) != NULL)
					*p = '\0';
				strlcat(dns, tmp, sizeof(dns));
				strlcat(dns, ",", sizeof(dns));
			}
			if (strlen(dns))
				dns[strlen(dns) - 1] = '\0';
			fprintf(fp, "%s\n\n", dns);
		}

		fprintf(fp, "[Peer]\n"
			"PublicKey = %s\n"
			, spub);

		snprintf(cpsk, sizeof(cpsk), "%s", nvram_pf_safe_get(c_prefix, "psk"));
		if (psk && cpsk[0] != '\0')
			fprintf(fp, "PresharedKey = %s\n", cpsk);

		fprintf(fp, "AllowedIPs = %s\n", caips);

		if (is_valid_ip6(_wg_server_get_endpoint(buf, sizeof(buf))))
			fprintf(fp, "Endpoint = [%s]:%d\n", buf, nvram_pf_get_int(s_prefix, "port"));
		else
			fprintf(fp, "Endpoint = %s:%d\n", buf, nvram_pf_get_int(s_prefix, "port"));

		if (alive)
			fprintf(fp, "PersistentKeepalive = %d\n", alive);

		fclose(fp);
	}
}

static void _wg_server_gen_conf(char* prefix, char* path)
{
	FILE* fp;
	char priv[WG_KEY_SIZE] = {0};
	char cpub[WG_KEY_SIZE] = {0};
	char cpsk[WG_KEY_SIZE] = {0};
	char caips[4096] = {0};
	int c;
	char c_prefix[16] = {0};
	int psk = nvram_pf_get_int(prefix, "psk");

	snprintf(priv, sizeof(priv), "%s", nvram_pf_safe_get(prefix, "priv"));
	if (priv[0] == '\0')
		return;

	fp = fopen(path, "w");
	if (fp)
	{
		fprintf(fp, "[Interface]\n"
			"PrivateKey = %s\n"
			"ListenPort = %d\n\n"
			, priv, nvram_pf_get_int(prefix, "port")
			);

		for(c = 1; c <= WG_SERVER_CLIENT_MAX; c++)
		{
			snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", prefix, c);
			snprintf(cpub, sizeof(cpub), "%s", nvram_pf_safe_get(c_prefix, "pub"));
			snprintf(cpsk, sizeof(cpsk), "%s", nvram_pf_safe_get(c_prefix, "psk"));

			if (nvram_pf_get_int(c_prefix, "enable") == 0)
				continue;

			if (cpub[0] == '\0')
				continue;

			snprintf(caips, sizeof(caips), "%s", nvram_pf_safe_get(c_prefix, "aips"));
			if (caips[0] == '\0')
			{
				int ret;
				snprintf(caips, sizeof(caips), "%s", nvram_pf_safe_get(c_prefix, "addr"));
				ret = is_valid_ip(caips);
				if (ret > 0)
					strlcat(caips, (ret > 1) ? "/128" : "/32", sizeof(caips));
				else
					continue;
				nvram_pf_set(c_prefix, "aips", caips);
			}

			fprintf(fp, "[Peer]\n"
				"PublicKey = %s\n"
				"AllowedIPs = %s\n"
				, cpub, caips
				);

			if (psk && cpsk[0] != '\0')
				fprintf(fp, "PresharedKey = %s\n", cpsk);
		}

		fclose(fp);
	}
}

void _wg_server_set_peer(char* ifname, const char* s_prefix, const char* c_prefix)
{
	if (nvram_pf_get_int(c_prefix, "enable"))
	{
		if (nvram_pf_get_int(s_prefix, "psk"))
		{
			char path[128] = {0};
			snprintf(path, sizeof(path), "%s/psk.tmp", WG_DIR_CONF);
			f_write_string(path, nvram_pf_safe_get(c_prefix, "psk"), 0, 0);
			eval("wg", "set", ifname, "peer", nvram_pf_safe_get(c_prefix, "pub"), "preshared-key", path, "allowed-ips", nvram_pf_safe_get(c_prefix, "aips"));
			unlink(path);
		}
		else
		{
			eval("wg", "set", ifname, "peer", nvram_pf_safe_get(c_prefix, "pub"), "allowed-ips", nvram_pf_safe_get(c_prefix, "aips"));
		}
	}
	else
		eval("wg", "set", ifname, "peer", nvram_pf_safe_get(c_prefix, "pub"), "remove");
}

void _wg_server_route_update(char* ifname, const char* c_prefix)
{
	char path[128] = {0};
	char aips[4096] = {0};
	char buf[128] = {0};
	char *p = NULL;

	// delete old rules
	snprintf(path, sizeof(path), "%s/%sroute", WG_DIR_CONF, c_prefix);
	f_read_string(path, aips, sizeof(aips));
	foreach_44(buf, aips, p)
	{
		_dprintf("%s: del [%s]\n", __FUNCTION__, buf);
		if (strchr(buf, '/') == NULL)
			continue;
		if (!strcmp(buf, "0.0.0.0/0"))
			continue;
		else if (!strcmp(buf, "::/0"))
			continue;
		else
			eval("ip", "route", "del", buf, "dev", ifname);
	}

	// add new rules
	memset(aips, 0, sizeof(aips));
	strlcpy(aips, nvram_pf_safe_get(c_prefix, "aips"), sizeof(aips));
	f_write_string(path, aips, 0, 0);
	foreach_44(buf, aips, p)
	{
		_dprintf("%s: add [%s]\n", __FUNCTION__, buf);
		if (strchr(buf, '/') == NULL)
			continue;
		if (!strcmp(buf, "0.0.0.0/0"))
			continue;
		else if (!strcmp(buf, "::/0"))
			continue;
		else
			eval("ip", "route", "add", buf, "dev", ifname);
	}
}

void _wg_server_ip_rule_add(const char* c_prefix)
{
	char aips[4096] = {0};
	char buf[128] = {0};
	char *p = NULL;
	char pref_str[16] = {0};

	snprintf(pref_str, sizeof(pref_str), "%d", IP_RULE_PREF_VPNS);

	memset(aips, 0, sizeof(aips));
	strlcpy(aips, nvram_pf_safe_get(c_prefix, "aips"), sizeof(aips));
	foreach_44(buf, aips, p)
	{
		_dprintf("%s: add [%s]\n", __FUNCTION__, buf);
		if (strchr(buf, '/') == NULL)
			continue;
		if (!strcmp(buf, "0.0.0.0/0"))
			continue;
		else if (!strcmp(buf, "::/0"))
			continue;
		else
		{
			eval("ip", "rule", "add", "to", buf, "lookup", "main", "pref", pref_str);
		}
	}
}

void _wg_server_ip_rule_del(const char* c_prefix)
{
	char path[128] = {0};
	char aips[4096] = {0};
	char buf[128] = {0};
	char *p = NULL;
	char pref_str[16] = {0};

	snprintf(pref_str, sizeof(pref_str), "%d", IP_RULE_PREF_VPNS);
	snprintf(path, sizeof(path), "%s/%sroute", WG_DIR_CONF, c_prefix);

	f_read_string(path, aips, sizeof(aips));
	foreach_44(buf, aips, p)
	{
		_dprintf("%s: del [%s]\n", __FUNCTION__, buf);
		if (strchr(buf, '/') == NULL)
			continue;
		if (!strcmp(buf, "0.0.0.0/0"))
			continue;
		else if (!strcmp(buf, "::/0"))
			continue;
		else
			eval("ip", "rule", "del", "to", buf, "lookup", "main", "pref", pref_str);
	}
}

static int _wait_time_sync(int max)
{
	while (!nvram_match("ntp_ready", "1") && max--)
		sleep(1);
	if (max <= 0)
	{
		logmessage_normal("WireGuard", "Unable to start clients as NTP not synced yet, retrying later");
		return -1;
	}
	else
		return 0;
}

static int _wgc_jobs_install(void)
{
	char *argv[] = {WG_PRG_CRU,
		"a", WG_TAG,
		"*/1 * * * * service restart_wgc",
		NULL };

	return _eval(argv, NULL, 0, NULL);
}

static int _wgc_jobs_remove(void)
{
	char *argv[] = {WG_PRG_CRU,
		"d", WG_TAG,
		NULL };

	return _eval(argv, NULL, 0, NULL);
}

static int _wgc_check_ep_jobs_exist(void)
{
	const char *tmpfile = "/tmp/wgc_chk.tmp";
	char buf[256];
	int ret;

	snprintf(buf, sizeof(buf), "%s l | grep %s > %s", WG_PRG_CRU, WGC_CHK_EP, tmpfile);
	system(buf);
	ret = (f_size(tmpfile) > 0) ? 1 : 0;
	unlink(tmpfile);

	return (ret);
}

static int _wgc_check_ep_jobs_install(void)
{
	char *argv[] = {WG_PRG_CRU,
		"a", WGC_CHK_EP,
		"*/1 * * * * check_wgc_ep",
		NULL };

	return _eval(argv, NULL, 0, NULL);
}

static int _wgc_check_ep_jobs_remove(void)
{
	char *argv[] = {WG_PRG_CRU,
		"d", WGC_CHK_EP,
		NULL };

	return _eval(argv, NULL, 0, NULL);
}

static int _is_any_wgs_enabled()
{
	int unit;
	char nv[16] = {0};

	for (unit = 1; unit <= WG_SERVER_MAX; unit++)
	{
		snprintf(nv, sizeof(nv), "wgs%d_enable", unit);
		if (nvram_get_int(nv))
		{
			return 1;
		}
	}
	return 0;
}

static int _is_any_wgc_enabled()
{
	int unit;
	char nv[16] = {0};

	for (unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(nv, sizeof(nv), "wgc%d_enable", unit);
		if (nvram_get_int(nv))
		{
			return 1;
		}
	}
	return 0;
}

static void _wg_server_update_service(const char* prefix)
{
#ifdef RTCONFIG_SAMBASRV
	if (nvram_pf_get_int(prefix, "lanaccess"))
	{
		stop_samba(0);
		start_samba();
	}
#endif
}

void start_wgsall()
{
	int unit;
	char nv[16] = {0};

	for (unit = 1; unit <= WG_SERVER_MAX; unit++)
	{
		snprintf(nv, sizeof(nv), "wgs%d_enable", unit);
		if (nvram_get_int(nv))
			start_wgs(unit);
	}
}

void stop_wgsall()
{
	int unit;
	char ifname[8] = {0};

	for (unit = 1; unit <= WG_SERVER_MAX; unit++)
	{
		snprintf(ifname, sizeof(ifname), "%s%d", WG_SERVER_IF_PREFIX, unit);
		if (_wg_if_exist(ifname))
			stop_wgs(unit);
	}
}

void start_wgcall()
{
	int unit;
	char nv[16] = {0};
	int timesync = 0;

	_wgc_jobs_remove();

	for (unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(nv, sizeof(nv), "wgc%d_enable", unit);
		if (nvram_get_int(nv)) {
			if (!timesync && _wait_time_sync(WG_WAIT_SYNC)) {
				_wgc_jobs_install();
				return;
			} else {
				timesync = 1;
			}
			start_wgc(unit);
		}
	}
}

void stop_wgcall()
{
	int unit;
	char ifname[8] = {0};

	for (unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(ifname, sizeof(ifname), "%s%d", WG_CLIENT_IF_PREFIX, unit);
		if (_wg_if_exist(ifname))
			stop_wgc(unit);
	}
}

void start_wgs(int unit)
{
	char prefix[16] = {0};
	char path[128] = {0};
	char ifname[8] = {0};
	char c_prefix[16] = {0};
	char c_path[128] = {0};
	char cp_path[128] = {0};
	int c_unit;

	snprintf(prefix, sizeof(prefix), "%s%d_", WG_SERVER_NVRAM_PREFIX, unit);
	snprintf(path, sizeof(path), "%s/server%d.conf", WG_DIR_CONF, unit);
	snprintf(ifname, sizeof(ifname), "%s%d", WG_SERVER_IF_PREFIX, unit);

	/// load module
	eval("modprobe", "wireguard");

	/// generate config
	if (!d_exists(WG_DIR_CONF))
		mkdir(WG_DIR_CONF, 0700);
	_wg_server_gen_keys(prefix);
	for (c_unit = 1; c_unit <= WG_SERVER_CLIENT_MAX; c_unit++)
	{
		snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", prefix, c_unit);
		if (nvram_pf_get_int(c_prefix, "enable") == 0)
			continue;
		_wg_server_gen_client_keys(prefix, c_prefix);

		snprintf(c_path, sizeof(c_path), "%s/server%d_client%d.conf", WG_DIR_CONF, unit, c_unit);
		_wg_server_gen_client_conf(prefix, c_prefix, c_path);

		snprintf(cp_path, sizeof(cp_path), "%s/server%d_client%d.png", WG_DIR_CONF, unit, c_unit);
		eval("qrencode", "-r", c_path, "-o", cp_path);
	}
	_wg_server_gen_conf(prefix, path);

	/// setup tunnel
	_wg_tunnel_create(prefix, ifname, path);

	unlink(path);

	/// netfilter
	_wg_server_nf_add(unit, prefix, ifname);

	/// route
	for (c_unit = 1; c_unit <= WG_SERVER_CLIENT_MAX; c_unit++)
	{
		snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", prefix, c_unit);
		if (nvram_pf_get_int(c_prefix, "enable") == 0)
			continue;
		_wg_config_route(c_prefix, ifname, 0);
		/// add priority rule
		_wg_server_ip_rule_add(c_prefix);
	}

	/// sysdeps
	_wg_config_sysdeps(1);

	/// related services
	_wg_server_update_service(prefix);
}

void stop_wgs(int unit)
{
	char ifname[8] = {0};
	char prefix[16] = {0};
	int wg_enable = is_wg_enabled();
	char c_prefix[16] = {0};
	int c_unit;
#ifdef RTCONFIG_MULTILAN_CFG
	int i;
	int sdn_rule_exist = 0;
	char fpath[128] = {0};
#endif

	snprintf(ifname, sizeof(ifname), "%s%d", WG_SERVER_IF_PREFIX, unit);
	snprintf(prefix, sizeof(prefix), "%s%d_", WG_SERVER_NVRAM_PREFIX, unit);

	/// netfilter
#ifdef RTCONFIG_MULTILAN_CFG
	for (i = 0; i < MTLAN_MAXINUM; i++) {
		snprintf(fpath, sizeof(fpath), "%s/fw_%s_sdn%d.sh", WG_DIR_CONF, ifname, i);
		if (f_exists(fpath))
			sdn_rule_exist = 1;
	}
	if (sdn_rule_exist)
		_wg_server_nf_bind_wan(ifname, 0);
#endif
	_wg_x_nf_del(ifname);

	/// delete priority rule
	for (c_unit = 1; c_unit <= WG_SERVER_CLIENT_MAX; c_unit++)
	{
		snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", prefix, c_unit);
		if (nvram_pf_get_int(c_prefix, "enable") == 0)
			continue;
		_wg_server_ip_rule_del(c_prefix);
	}

	/// delete tunnel
	_wg_tunnel_delete(ifname);

	/// unload module
	if (!wg_enable)
		eval("modprobe", "-r", "wireguard");

	/// sysdeps
	_wg_config_sysdeps(wg_enable);
}

void start_wgc(int unit)
{
	char prefix[16] = {0};
	char path[128] = {0};
	char ifname[8] = {0};
	int table = 0;
	char ep_addr_r[1024] = {0};

	_dprintf("%s %d\n", __FUNCTION__, unit);

	snprintf(prefix, sizeof(prefix), "%s%d_", WG_CLIENT_NVRAM_PREFIX, unit);
	snprintf(path, sizeof(path), "%s/client%d.conf", WG_DIR_CONF, unit);
	snprintf(ifname, sizeof(ifname), "%s%d", WG_CLIENT_IF_PREFIX, unit);

#ifdef RTCONFIG_VPN_FUSION
	table = find_vpnc_idx_by_wgc_unit(unit);
#endif

	/// check configuration
	_wg_client_check_conf(prefix);

	/// load module
	eval("modprobe", "wireguard");

	/// resolv endpoint domain
	if (!_wg_resolv_ep(nvram_pf_safe_get(prefix, "ep_addr"), ep_addr_r, sizeof(ep_addr_r)))
		nvram_pf_set(prefix, "ep_addr_r", ep_addr_r);

	/// generate config
	if (!d_exists(WG_DIR_CONF))
		mkdir(WG_DIR_CONF, 0700);
	_wg_client_gen_conf(prefix, path);

	/// setup tunnel
	_wg_tunnel_create(prefix, ifname, path);

	unlink(path);

	/// netfilter
	_wg_client_nf_add(unit, prefix, ifname);

	/// route
	_wg_config_route(prefix, ifname, table);

	/// set endpoint route
	_wg_client_ep_route_add(prefix, table);

	/// dns
#ifdef RTCONFIG_VPN_FUSION
	_wg_client_dns_setup_vpnc(prefix, ifname, table);
#else
	_wg_client_dns_setup(prefix, ifname);
	update_resolvconf();
#endif

	/// check endpoint jobs
	if (!_wgc_check_ep_jobs_exist())
		_wgc_check_ep_jobs_install();

	/// sysdeps
	_wg_config_sysdeps(1);
}

void stop_wgc(int unit)
{
	char prefix[16] = {0};
	char ifname[8] = {0};
	char path[128] = {0};
	int table = 0;
	int any_wgc_enabled = _is_any_wgc_enabled();
	int wg_enable = _is_any_wgs_enabled() | any_wgc_enabled;

	_dprintf("%s %d\n", __FUNCTION__, unit);

	snprintf(prefix, sizeof(prefix), "%s%d_", WG_CLIENT_NVRAM_PREFIX, unit);
	snprintf(ifname, sizeof(ifname), "%s%d", WG_CLIENT_IF_PREFIX, unit);

	/// check endpoint jobs
	if (!any_wgc_enabled)
		_wgc_check_ep_jobs_remove();

#ifdef RTCONFIG_VPN_FUSION
	table = find_vpnc_idx_by_wgc_unit(unit);
#endif
	_wg_client_ep_route_del(prefix, table);

	/// dns
	snprintf(path, sizeof(path), "%s/resolv_%s.dnsmasq", WG_DIR_CONF, ifname);
	unlink(path);
	update_resolvconf();

	/// netfilter
	_wg_x_nf_del(ifname);

	/// delete tunnel
	_wg_tunnel_delete(ifname);

	/// unload module
	if (!wg_enable)
		eval("modprobe", "-r", "wireguard");

	/// sysdeps
	_wg_config_sysdeps(wg_enable);
}

int write_wgc_resolv_dnsmasq(FILE* fp_servers)
{
	FILE* f_in;
	int unit;
	char path[128] = {0};
	char buf[128] = {0};
	int added = 0;

	for (unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(path, sizeof(path), "%s/resolv_%s%d.dnsmasq", WG_DIR_CONF, WG_CLIENT_IF_PREFIX, unit);
		f_in = fopen(path, "r");
		if (f_in)
		{
			while( !feof(f_in) )
			{
				if(fgets(buf, sizeof(buf), f_in) && strlen(buf) > 7)
				{
					fputs(buf, fp_servers);
					added = 1;
				}
			}
			fclose(f_in);
		}
	}

	return (added);
}

void write_wgs_dnsmasq_config(FILE* fp)
{
	int unit;
	char prefix[16] = {0};

	for(unit = 1; unit <= WG_SERVER_MAX; unit++)
	{
		snprintf(prefix, sizeof(prefix), "%s%d_", WG_SERVER_NVRAM_PREFIX, unit);
		if (nvram_pf_get_int(prefix, "enable") && nvram_pf_get_int(prefix, "dns"))
		{
			fprintf(fp, "interface=%s%d\n", WG_SERVER_IF_PREFIX, unit);
			fprintf(fp, "no-dhcp-interface=%s%d\n", WG_SERVER_IF_PREFIX, unit);
		}
	}
}

void run_wgs_fw_scripts()
{
	int unit;
	char buf[128] = {0};

	for(unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(buf, sizeof(buf), "%s/fw_%s%d.sh", WG_DIR_CONF, WG_SERVER_IF_PREFIX, unit);
		if(f_exists(buf))
			eval(buf);
	}
}

void run_wgs_fw_nat_scripts()
{
	int unit;
	char buf[128] = {0};

	for(unit = 1; unit <= WG_SERVER_MAX; unit++)
	{
		snprintf(buf, sizeof(buf), "%s/fw_%s%d_nat6.sh", WG_DIR_CONF, WG_SERVER_IF_PREFIX, unit);
		if(f_exists(buf))
			eval(buf);
	}
}

void run_wgc_fw_scripts()
{
	int unit;
	char buf[128] = {0};

	for(unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(buf, sizeof(buf), "%s/fw_%s%d.sh", WG_DIR_CONF, WG_CLIENT_IF_PREFIX, unit);
		if(f_exists(buf))
			eval(buf);
	}
}

void run_wgc_fw_nat_scripts()
{
	int unit;
	char buf[128] = {0};

	for(unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(buf, sizeof(buf), "%s/fw_%s%d_nat.sh", WG_DIR_CONF, WG_CLIENT_IF_PREFIX, unit);
		if(f_exists(buf))
			eval(buf);
	}
}

void update_wgs_client(int s_unit, int c_unit)
{
	char s_prefix[16] = {0};
	char c_prefix[16] = {0};
	char c_path[128] = {0};
	char cp_path[128] = {0};
	char ifname[8] = {0};

	snprintf(s_prefix, sizeof(s_prefix), "%s%d_", WG_SERVER_NVRAM_PREFIX, s_unit);
	snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", s_prefix, c_unit);
	snprintf(c_path, sizeof(c_path), "%s/server%d_client%d.conf", WG_DIR_CONF, s_unit, c_unit);
	snprintf(cp_path, sizeof(cp_path), "%s/server%d_client%d.png", WG_DIR_CONF, s_unit, c_unit);
	snprintf(ifname, sizeof(ifname), "%s%d", WG_SERVER_IF_PREFIX, s_unit);

	if (nvram_pf_get_int(c_prefix, "enable"))
	{
		// client key
		_wg_server_gen_client_keys(s_prefix, c_prefix);

		// client conf
		_wg_server_gen_client_conf(s_prefix, c_prefix, c_path);
		eval("qrencode", "-r", c_path, "-o", cp_path);
	}

	// route update
	_wg_server_route_update(ifname, c_prefix);

	// server update
	_wg_server_set_peer(ifname, s_prefix, c_prefix);

	// netfilter reload for nat66
	_wg_server_nf_del_nat6(ifname);
	_wg_server_nf_add_nat6(s_prefix, ifname);

	// related services
	_wg_server_update_service(s_prefix);
}

void update_wgs_client_ep()
{
	int s_unit, c_unit;
	char path[128] = {0};
	char p_path[128] = {0};
	char address[64];
	char cmd[256] = {0};

	for (s_unit = 1; s_unit <= WG_SERVER_MAX; s_unit++)
	{
		for (c_unit = 1; c_unit <= WG_SERVER_CLIENT_MAX; c_unit++)
		{
			snprintf(path, sizeof(path), "%s/server%d_client%d.conf", WG_DIR_CONF, s_unit, c_unit);
			snprintf(p_path, sizeof(p_path), "%s/server%d_client%d.png", WG_DIR_CONF, s_unit, c_unit);
			if(f_exists(path) && f_size(path) > 0)
			{
				_wg_server_get_endpoint(address, sizeof(address));
				snprintf(cmd, sizeof(cmd), "sed -i 's/Endpoint = [^ ]*:/Endpoint = %s:/' %s", address, path);
				system(cmd);
				eval("qrencode", "-r", path, "-o", p_path);
			}
		}
	}
}

void reload_wgs_ip_rule()
{
	int s_unit;
	char s_prefix[16] = {0};
	int c_unit;
	char c_prefix[16] = {0};

	for (s_unit = 1; s_unit <= WG_SERVER_MAX; s_unit++)
	{
		snprintf(s_prefix, sizeof(s_prefix), "%s%d_", WG_SERVER_NVRAM_PREFIX, s_unit);
		for (c_unit = 1; c_unit <= WG_SERVER_CLIENT_MAX; c_unit++)
		{
			snprintf(c_prefix, sizeof(c_prefix), "%sc%d_", s_prefix, c_unit);
			if (nvram_pf_get_int(c_prefix, "enable") == 0)
				continue;
			_wg_server_ip_rule_del(c_prefix);
			_wg_server_ip_rule_add(c_prefix);
		}
	}
}

int is_wg_enabled()
{
	int wg_enable = 0;

	wg_enable += _is_any_wgs_enabled();
	wg_enable += _is_any_wgc_enabled();

	return (wg_enable);
}

void check_wgc_endpoint()
{
	int unit;
	char prefix[8] = {0};
	char ep_addr[64] = {0};
	char ep_addr_r[1024] = {0};
	char buf[1024] = {0};
	char *p;

	if(!is_wan_connect(wan_primary_ifunit()))
		return;

	for (unit = 1; unit <= WG_CLIENT_MAX; unit++)
	{
		snprintf(prefix, sizeof(prefix), "%s%d_", WG_CLIENT_NVRAM_PREFIX, unit);
		if (nvram_pf_get_int(prefix, "enable") == 0)
			continue;

		if (is_wgc_connected(unit))
			continue;

		strlcpy(ep_addr, nvram_pf_safe_get(prefix, "ep_addr"), sizeof(ep_addr));
		if (is_valid_ip(ep_addr) > 0)
			continue;

		if (_wg_resolv_ep(ep_addr, buf, sizeof(buf)))
			continue;

		// TBD:
		strlcpy(ep_addr_r, nvram_pf_safe_get(prefix, "ep_addr_r"), sizeof(ep_addr_r));
		if ((p = strchr(ep_addr_r, ' ')))
			*p = '\0';

		if (!strstr(buf, ep_addr_r))
		{
			_dprintf("%s ip changed to %s\n", ep_addr, buf);
			snprintf(buf, sizeof(buf), "restart_wgc %d", unit);
			notify_rc(buf);
		}
	}
}

#ifdef RTCONFIG_MULTILAN_CFG
static void _update_wg_by_sdn(MTLAN_T *pmtl, size_t mtl_sz, int restart_all_sdn, wg_type_t client)
{
	int unit, i, j;
	char prefix[16] = {0};
	char wg_ifname[8] = {0};
	char fpath[128] = {0};
	VPN_VPNX_T vpnx;
	FILE *fp;
	int sdn_rule_exist = 0;
	int max_unit = (client) ? WG_CLIENT_MAX : WG_SERVER_MAX;

	if (restart_all_sdn) {
		if (client)
			eval("iptables", "-F", "WGCF");
		else
			eval("iptables", "-F", "WGSF");
	}

	for(unit = 1; unit <= max_unit; unit++) {
		snprintf(prefix, sizeof(prefix), "%s%d_", (client) ? WG_CLIENT_NVRAM_PREFIX : WG_SERVER_NVRAM_PREFIX, unit);

		if (!nvram_pf_get_int(prefix, "enable"))
			continue;

		snprintf(wg_ifname, sizeof(wg_ifname), "%s%d", (client) ? WG_CLIENT_IF_PREFIX : WG_SERVER_IF_PREFIX, unit);

		/// iptables rules
		for (i = 0; i < mtl_sz; i++) {
			// delete old rules for specific sdn
			snprintf(fpath, sizeof(fpath), "%s/fw_%s_sdn%d.sh", WG_DIR_CONF, wg_ifname, pmtl[i].nw_t.idx);
			if(f_exists(fpath)) {
				eval("sed", "-i", "s/-I/-D/", fpath);
				eval("sed", "-i", "s/-A/-D/", fpath);
				eval(fpath);
				unlink(fpath);
				if (!client)
					_wg_server_nf_bind_wan(wg_ifname, 0);
			}

			// add new rules for specific sdn
			if (!pmtl[i].enable)
				continue;
			else if (client
					&& pmtl[i].sdn_t.vpnc_idx
					&& get_vpnx_by_vpnc_idx(&vpnx, pmtl[i].sdn_t.vpnc_idx)
					&& vpnx.proto == VPN_PROTO_WG
					&& vpnx.unit == unit) {
				fp = fopen(fpath, "w");
				if (fp) {
					fprintf(fp, "#!/bin/sh\n\n");
					_wg_client_nf_bind_sdn(fp, wg_ifname, pmtl[i].nw_t.ifname);
					fclose(fp);
					chmod(fpath, S_IRUSR|S_IWUSR|S_IXUSR);
					eval(fpath);
				}
			}
			else if (!client) {
				for (j = 0; j < MTLAN_VPNS_MAXINUM; j++) {
					if (pmtl[i].sdn_t.vpns_idx_rl[j]
						&& get_vpnx_by_vpns_idx(&vpnx, pmtl[i].sdn_t.vpns_idx_rl[j])
						&& vpnx.proto == VPN_PROTO_WG
						&& vpnx.unit == unit) {
						fp = fopen(fpath, "w");
						if (fp) {
							fprintf(fp, "#!/bin/sh\n\n");
							_wg_server_nf_bind_sdn(fp, wg_ifname, pmtl[i].nw_t.ifname);
							fclose(fp);
							chmod(fpath, S_IRUSR|S_IWUSR|S_IXUSR);
							eval(fpath);
						}
					}
				}
			}
		}

		// if no rule for specific SDN, add rule for all SDN.
		sdn_rule_exist = 0;
		for (i = 0; i < MTLAN_MAXINUM; i++) {
			snprintf(fpath, sizeof(fpath), "%s/fw_%s_sdn%d.sh", WG_DIR_CONF, wg_ifname, i);
			if (f_exists(fpath))
				sdn_rule_exist = 1;
		}
		snprintf(fpath, sizeof(fpath), "%s/fw_%s_none.sh", WG_DIR_CONF, wg_ifname);
		if (sdn_rule_exist) {
			if (f_exists(fpath)) {
				eval("sed", "-i", "s/-I/-D/", fpath);
				eval(fpath);
				unlink(fpath);
				if (!client)
					_wg_server_nf_bind_wan(wg_ifname, 1);
			}
			else if (restart_all_sdn && !client) {
				_wg_server_nf_bind_wan(wg_ifname, 1);
			}
		}
		else if (client) {
			if (!f_exists(fpath)) {
				fp = fopen(fpath, "w");
				if (fp) {
					fprintf(fp, "#!/bin/sh\n\n");
					_wg_client_nf_bind_sdn(fp, wg_ifname, NULL);
					fclose(fp);
					chmod(fpath, S_IRUSR|S_IWUSR|S_IXUSR);
					eval(fpath);
				}
			}
			else if (restart_all_sdn) {
				eval(fpath);
			}
		}
		else {
			if (!f_exists(fpath)) {
				fp = fopen(fpath, "w");
				if (fp) {
					fprintf(fp, "#!/bin/sh\n\n");
					_wg_server_nf_bind_sdn(fp, wg_ifname, NULL);
					fclose(fp);
					chmod(fpath, S_IRUSR|S_IWUSR|S_IXUSR);
					eval(fpath);
				}
			}
			else if (restart_all_sdn) {
				eval(fpath);
			}
		}
	}
}
void update_wgc_by_sdn(MTLAN_T *pmtl, size_t mtl_sz, int restart_all_sdn)
{
	_update_wg_by_sdn(pmtl, mtl_sz, restart_all_sdn, WG_TYPE_CLIENT);
}

void update_wgs_by_sdn(MTLAN_T *pmtl, size_t mtl_sz, int restart_all_sdn)
{
	_update_wg_by_sdn(pmtl, mtl_sz, restart_all_sdn, WG_TYPE_SERVER);
}
#endif

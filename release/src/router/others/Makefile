include ../common.mak

all:

install:
	install -d $(INSTALLDIR)/sbin
	install -d $(INSTALLDIR)/usr/sbin
	install -d $(INSTALLDIR)/lib

ifeq ($(TCONFIG_USB),y)
# Optware perl symlink
	install -d $(INSTALLDIR)/usr/bin
	ln -sf /opt/bin/perl $(INSTALLDIR)/usr/bin/perl
endif
ifeq ($(RTCONFIG_BCMARM),y)
ifneq ($(RTCONFIG_HND_ROUTER),y)
	install -m 0700 wlk $(INSTALLDIR)/usr/sbin
endif
endif
ifeq ($(RTCONFIG_ENTWARE),y)
	install -m 0755 entware.sh $(INSTALLDIR)/usr/sbin
endif
ifeq ($(K3),y)
	ln -sf /tmp/k3screenctrl $(INSTALLDIR)/lib/k3screenctrl
endif
# cron helper
	install -m 0700 cru $(INSTALLDIR)/usr/sbin

# playing with ipkg / from OpenWrt svn
#!!TB	install -m 0700 ipkg $(INSTALLDIR)/usr/sbin

# system info
	install -m 0700 sysinfo $(INSTALLDIR)/usr/sbin

# keep winscp happy
#	install -m 0700 groups $(INSTALLDIR)/usr/bin

# chpasswd script
	install -m 0700 chpasswd.sh $(INSTALLDIR)/usr/sbin

ifeq ($(RTN14U)$(RTAC52U)$(RTAC51U)$(RTN11P)$(RTN300)$(RTN54U)$(RTAC1200HP)$(RTN56UB1)$(RTN56UB2)$(RTAC54U)$(RTAC1200)$(RTAC1200V2)$(RTAC1200GA1)$(RTAC1200GU)$(RTN11PB1)$(RTAC85U)$(RTAC85P)$(RPAC87)$(RTN800HP)$(RTACRH26),y)
# for read/write Ralink PHY registers
	install -D mii_mgr $(INSTALLDIR)/bin/mii_mgr
endif

ifeq ($(RTCONFIG_RALINK),y)
# for adjust smp_affinity and rps_cpus
ifeq ($(RTCONFIG_RALINK_MT7621),y)
ifeq ($(RTAC1200GU),y)
	install smp_4300.sh $(INSTALLDIR)/sbin/smp.sh
else ifeq ($(RTAC85U)$(RTAC85P)$(RPAC87)$(RTN800HP)$(RTACRH26),y)
	install smp_5010.sh $(INSTALLDIR)/sbin/smp.sh
#else ifeq ($(RT4GAX56),y)
#	install smp_openwr_4200_MT7621.sh $(INSTALLDIR)/sbin/smp.sh
else ifeq ($(RTCONFIG_WLMODULE_MT7915D_AP),y)
	install smp_openwr_4210_MT7621.sh $(INSTALLDIR)/sbin/smp.sh
endif
endif
ifeq ($(RTCONFIG_RALINK_MT7629)$(RTCONFIG_RALINK_MT7622),y)
ifeq ($(RTACRH18),y)
	install smp_openwr_4200.sh $(INSTALLDIR)/sbin/smp.sh
endif
ifeq ($(RT4GAC86U),y)
	install smp_openwr_4200_mt7622.sh $(INSTALLDIR)/sbin/smp.sh
endif
endif
endif
clean:

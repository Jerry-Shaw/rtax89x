include $(SRCBASE)/router/.config

#! /usr/bin/make

#EXT_FUNC+=802_3 nat arp arpreply ip ip6 standard log redirect vlan mark_m mark \
#          pkttype stp among limit ulog nflog
EXT_FUNC+=ip standard redirect mark ip6 nat arp
ifeq ($(RTCONFIG_HND_ROUTER),y)
EXT_FUNC+=time ftos skiplog wmm_mark
else
ifeq ($(and $(RTCONFIG_BCMARM),$(RTCONFIG_AMAS)),y)
EXT_FUNC+=skipctf
endif
endif
EXT_TABLES+=filter nat broute
EXT_OBJS+=$(foreach T,$(EXT_FUNC), extensions/ebt_$(T).o)
EXT_OBJS+=$(foreach T,$(EXT_TABLES), extensions/ebtable_$(T).o)
EXT_LIBS+=$(foreach T,$(EXT_FUNC), extensions/libebt_$(T).so)
EXT_LIBS+=$(foreach T,$(EXT_TABLES), extensions/libebtable_$(T).so)
EXT_LIBSI+=$(foreach T,$(EXT_FUNC), -lebt_$(T))
EXT_LIBSI+=$(foreach T,$(EXT_TABLES), -lebtable_$(T))

extensions/ebt_%.so: extensions/ebt_%.o
	$(CC) $(LDFLAGS) -shared -o $@ $<

extensions/libebt_%.so: extensions/ebt_%.so
	mv $< $@

extensions/ebtable_%.so: extensions/ebtable_%.o
	$(CC) $(LDFLAGS) -shared -o $@ $<

extensions/libebtable_%.so: extensions/ebtable_%.so
	mv $< $@

extensions/ebt_%.o: extensions/ebt_%.c include/ebtables_u.h
	$(CC) $(CFLAGS) $(CFLAGS_SH_LIB) $(PROGSPECS) -c -o $@ $< -I$(KERNEL_INCLUDES)

extensions/ebtable_%.o: extensions/ebtable_%.c
	$(CC) $(CFLAGS) $(CFLAGS_SH_LIB) $(PROGSPECS) -c -o $@ $< -I$(KERNEL_INCLUDES)


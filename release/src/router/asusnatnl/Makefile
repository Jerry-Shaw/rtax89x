include ../common.mak
CFLAGS += $(if $(RTCONFIG_DEBUG),-g)

ifeq ($(or $(RTCONFIG_BCMARM),$(RTCONFIG_QCA_ARM),$(RTCONFIG_ALPINE),$(MUSL_LIBC)),y)
CONFIGURE=configure-router-arm
ARCH=ARM
else ifeq ($(RTCONFIG_RALINK_MT7622),y)
CONFIGURE=configure-router-arm64
ARCH=ARM64
else
CONFIGURE=configure-router-mips
ARCH=MIPS
endif

all: pjproject-1.12/stamp-h1-$(ARCH)
	$(MAKE) -C pjproject-1.12 UCLIBC_NG=$(UCLIBC_NG)
	$(MAKE) -C udt os=LINUX arch=$(ARCH)
	$(MAKE) -C natnl linux-lib router=yes RTCONFIG_SW_HW_AUTH=$(RTCONFIG_SW_HW_AUTH)
#	$(MAKE) -C natnl/msvc_prjs/asusnatnl_test

clean:
	-$(MAKE) -C pjproject-1.12 distclean
	-$(MAKE) -C udt clean
	-$(MAKE) -C natnl clean
	@rm -f pjproject-1.12/stamp-h1-*

pjproject-1.12/stamp-h1-%:
	cd pjproject-1.12 && AR="$(AR) -r -v" \
	./$(CONFIGURE)
	@touch $@

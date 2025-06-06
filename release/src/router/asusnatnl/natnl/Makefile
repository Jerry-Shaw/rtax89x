#Modify this to point to the PJSIP location.
include ../version.mak
#RTSRCBASE=../../..
PJBASE=../pjproject-1.12
RTSHAREDBASE=$(TOP)/shared
RTNVARMBASE=$(TOP_PLATFORM)/nvram$(BCMEX)$(EX7)
UMEMBASE=../umem-1.0.1
UDTBASE=../udt
LIB_UDT=udt
SCTPBASE=../pjproject-1.12/third_party/usrsctp/usrsctplib
NATBASE=.
#NVRAM_DIR=$(SRCBASE)/nvram$(BCMEX)
#SHARED_DIR=$(SRCBASE)/shared
include $(PJBASE)/build.mak

CC      = $(PJ_CC)
CXX		= $(PJ_CXX)
AR      = $(PJ_AR)
LD	    = $(PJ_LD)
#STRIP	= $(PJ_STRIP)   
LDFLAGS = $(PJ_LDFLAGS)
LDLIBS  = $(PJ_LDLIBS)
#LDLIBS += -L$(UDTBASE) -l$(LIB_UDT) -L$(NATBASE)
CFLAGS += $(PJ_CFLAGS)
CFLAGS += -g -I$(NATBASE) -I$(NATBASE)/msvc_prjs/asusnatnl 
CFLAGS += -DTUNNEL_V=\"$(VERSION)\"
CFLAGS += -I$(UDTBASE) -I$(SCTPBASE)
#CFLAGS += -Wall
CPPFLAGS += -fPIC $(CFLAGS) 

OS = $(shell uname -s)
LIB_NAME=libasusnatnl
LIBRARY = $(LIB_NAME).a
DYLIB = $(LIB_NAME).dylib
ifeq ($(OS),Darwin)
YYY = $(CFLAGS)
XXX = $(findstring armv7s, $(YYY))
ifeq ($(XXX),)
XXX = $(findstring armv7, $(YYY))
ifeq ($(XXX),)
XXX = $(findstring i386, $(YYY))
ifeq ($(XXX),)
XXX = $(findstring arm64, $(YYY))
ifeq ($(XXX),)
XXX = $(findstring x86_64, $(YYY))
endif
endif
endif
endif
LIBRARY = $(LIB_NAME)-$(XXX).a
LIB_UDT = udt-$(XXX)
DYLIB = $(LIB_NAME)-$(XXX).dylib
endif

LDLIBS += -L$(UDTBASE) -l$(LIB_UDT) -L$(NATBASE)

ifeq ($(AR),)
AR=ar
endif

ifeq ($(RTCONFIG_SW_HW_AUTH),y)
CFLAGS += -DSW_HW_AUTH
endif

ifeq ($(router), yes)
#CFLAGS += -DROUTER=1
#for wl_ioctl used.
CFLAGS += -fpermissive
CFLAGS += -DTYPEDEF_FLOAT_T
CFLAGS += -I$(RTSHAREDBASE)
CFLAGS += -I$(SRCBASE)/include

LDLIBS += -lshared
LDLIBS += -lnvram
LDFLAGS += -L$(RTSHAREDBASE)
LDFLAGS += -L$(RTNVARMBASE)
ifeq ($(RTCONFIG_BCM_502L07P2),y)
LDLIBS += -lethctl
LDFLAGS += -L$(TOP_PLATFORM)/ethctl_lib
endif
endif

#check if make goal is android-lib or linux-lib
ifeq ("$(MAKECMDGOALS)", $(filter "$(MAKECMDGOALS)", "android-lib" "linux-lib" "ios-dylib" "static" "macos-dylib"))
CFLAGS += -DNATNL_LIB=1
endif

ifeq ("$(MAKECMDGOALS)", $(filter "$(MAKECMDGOALS)", "android-lib"))
ifeq ($(findstring i686, $(PJ_CC)), i686)
ARCH=i686
STL_DIR=x86
LDFLAGS +=-static-libstdc++
LDLIBS +=/opt/android-toolchain/x86/sysroot/usr/lib/i686-linux-android/libc++_static.a
endif
ifeq ($(findstring aarch64, $(PJ_CC)), aarch64)
ARCH=aarch64
STL_DIR=arm64
LDFLAGS +=-static-libstdc++
LDLIBS +=/opt/android-toolchains/arm64/sysroot/usr/lib/aarch64-linux-android/libc++_static.a
endif
ifeq ($(findstring arm, $(PJ_CC)), arm)
ARCH=arm
EABI=eabi
STL_DIR=arm
LDFLAGS +=-static-libstdc++
LDLIBS +=/opt/android-toolchains/arm/sysroot/usr/lib/arm-linux-androideabi/libc++_static.a
endif
ANDROID_CROSS=$(ARCH)-linux-android$(EABI)
ANDROID_GCC=$(ANDROID_CROSS)-gcc
ANDROID_STRIP=$(ANDROID_CROSS)-strip
ANDROID_AR=$(ANDROID_CROSS)-ar
ANDROID_RANLIB=$(ANDROID_CROSS)-ranlib
JNI_OBJ=natjni.o
CPPFLAGS= ${CFLAGS} -frtti -fexceptions -DANDROID -DPJ_ANDROID=1 
endif


OBJS = main.o natnl.o \
		natnl_codec.o \
		socket.o message.o client.o list.o tunnel.o \
		im_handler.o \
		im_ipc.o \
		rtsp_handler.o \
		$(JNI_OBJ)
		#config.o \

TMP_OBJS=$(patsubst natnl.o,,$(OBJS)) 
APP_OBJS = $(TMP_OBJS)		
OBJ_LIBS = main.o $(OBJS)

# If your application is in a file named main.cpp or main.c
# this is the line you will need to build the binary.
#all: natnl
all: natnl

android-lib: main.cpp $(OBJS)
	$(CXX) -shared -o libnatnl.so $(CPPFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS)
#$(STRIP) libnatnl.so
#	NATLIB_NAME=libnatnl.so
ifeq ($(wildcard ./*.cpp),)
#For prebuild on router platform. Don't remove it.
linux-lib:
	-cp -f $(TOP)/asusnatnl/natnl/prebuild/libasusnatnl.so libasusnatnl.so
else
linux-lib: main.cpp $(OBJS)
	$(CXX) -shared -o libasusnatnl.so $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS)
	NATLIB_NAME=libasusnatnl.so
endif

ios-dylib: main.cpp $(OBJS)
	$(CXX)  -Wl $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -dynamiclib -o $(DYLIB)
	NATLIB_NAME=$(DYLIB)

macos-dylib: main.cpp $(OBJS)
	$(CXX)  -Wl $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -dynamiclib -o $(DYLIB)
	NATLIB_NAME=$(DYLIB)

$(LIBRARY): main.o $(OBJS)
	$(AR) crs $@ $?


static: $(LIBRARY) 
	echo '<<<<< YYY=$(YYY), XXX=$(XXX)>>>>>'

natnl: main.cpp $(APP_OBJS)
	$(CXX) -o $@ $< $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(APP_OBJS) $(LDLIBS)

%.o: %.c
	$(CXX) -c -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LDLIBS)  $<

clean:
	rm -f *.o natnl *.so *.dylib *.a

install:
	mkdir -p ./inst/$(TARGET_NAME)
	cp lib*.* ./inst/$(TARGET_NAME)


ARCH := $(shell uname -m)
DEST ?= build
TARGET_ARCH ?= $(ARCH) # Still useful for selecting toolchains/flags if needed, but not for filename suffix

# Source files for the C version
SOURCES := main.c block_device.c udpbd_server.c

# OBJECT is now always udpbd-server, with .exe for Windows
OBJECT := $(DEST)/udpbd-server

# Update EXISTING_OBJECTS to reflect the simplified naming
# It will primarily be $(DEST)/udpbd-server and $(DEST)/udpbd-server.exe
EXISTING_OBJECTS := $(wildcard $(sort 	$(DEST)/udpbd-server 	$(DEST)/udpbd-server.exe 	))

CFLAGS ?= -Os -s
CFLAGS += -Wall -Wextra

# Handle Windows target specifics for .exe suffix and libs
ifeq ($(TARGET_ARCH),win64)
    CFLAGS := $(CFLAGS) -mconsole -static -static-libgcc
    LDLIBS := $(LDLIBS) -lmingw32 -lws2_32 # Assuming these are still desired for Windows builds
    OBJECT := $(OBJECT).exe
else ifeq ($(TARGET_ARCH),win32)
    CFLAGS := $(CFLAGS) -mconsole -static -static-libgcc
    LDLIBS := $(LDLIBS) -lmingw32 -lws2_32
    OBJECT := $(OBJECT).exe
endif
# Removed the OPENWRT_BUILD conditional wrapper around this block.
# If OpenWrt is building for a Windows target (highly unlikely for this package),
# it would get the .exe. But OpenWrt usually defines TARGET_ARCH to non-windows values.

MKDIR_P ?= mkdir -p
CHMOD_X ?= chmod +x
# STRINGS variable removed as it's unused

.PHONY: all build dist clean distclean

all: build

build: $(OBJECT)

clean:
	$(RM) -f $(DEST)/* # Keeps the simplified clean rule

$(OBJECT): $(SOURCES) udpbd.h udpbd_c.h
	$(info Building $(OBJECT) for $(TARGET_ARCH)) # Simplified info message
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)
	$(CHMOD_X) $@

install: $(OBJECT)
	# Ensure PS2DEV and its bin subdirectory are defined/exist if this rule is used.
	# Example: make install PS2DEV=/usr/local/ps2dev
	# Defaulting PS2DEV if not set might be useful, e.g. PS2DEV ?= /usr/local/ps2dev
	[ -d "$(PS2DEV)/bin" ] || $(MKDIR_P) "$(PS2DEV)/bin"
	cp $(OBJECT) $(PS2DEV)/bin/$(notdir $(OBJECT))

# CROSS_ARCH_POSIX, CROSS_ARCH_WIN32, CROSS_ARCH_ALL removed as they are not used.

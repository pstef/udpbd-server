ARCH := $(shell uname -m)
DEST ?= build
TARGET_ARCH ?= $(ARCH)

SOURCES := main.c block_device.c udpbd_server.c
OBJECT := $(DEST)/udpbd-server
EXISTING_OBJECTS := $(wildcard $(sort 	$(DEST)/udpbd-server 	$(DEST)/udpbd-server.exe 	))

CFLAGS ?= -Os -s
CFLAGS += -Wall -Wextra

# Handle Windows target specifics for .exe suffix and libs
ifeq ($(TARGET_ARCH),win64)
    CFLAGS := $(CFLAGS) -mconsole -static -static-libgcc
    LDLIBS := $(LDLIBS) -lmingw32 -lws2_32
    OBJECT := $(OBJECT).exe
else ifeq ($(TARGET_ARCH),win32)
    CFLAGS := $(CFLAGS) -mconsole -static -static-libgcc
    LDLIBS := $(LDLIBS) -lmingw32 -lws2_32
    OBJECT := $(OBJECT).exe
endif

MKDIR_P ?= mkdir -p
CHMOD_X ?= chmod +x

.PHONY: all build dist clean distclean

all: build

build: $(OBJECT)

clean:
	$(RM) -f $(DEST)/*

$(OBJECT): $(SOURCES) udpbd.h udpbd_c.h
	$(info Building $(OBJECT) for $(TARGET_ARCH))
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)
	$(CHMOD_X) $@

install: $(OBJECT)
	[ -d "$(PS2DEV)/bin" ] || $(MKDIR_P) "$(PS2DEV)/bin"
	cp $(OBJECT) $(PS2DEV)/bin/$(notdir $(OBJECT))

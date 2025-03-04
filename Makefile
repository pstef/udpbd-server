
ARCH := $(shell uname -m)
DEST ?= build

TARGET_ARCH ?= $(ARCH)

CROSS_ARCH_POSIX ?= x86_64 i686 aarch64 armv7
CROSS_ARCH_WIN32 ?= win64 win32
CROSS_ARCH_ALL ?= $(CROSS_ARCH_POSIX) $(CROSS_ARCH_WIN32)

OBJECT := $(DEST)/udpbd-server.$(TARGET_ARCH)
EXISTING_OBJECTS := $(wildcard $(sort \
	$(OBJECT) \
	$(foreach x,$(CROSS_ARCH_POSIX),$(DEST)/udpbd-server.$(x)) \
	$(foreach x,$(CROSS_ARCH_WIN32),$(DEST)/udpbd-server.$(x).exe) \
	))

CXXFLAGS ?= -Os -s

ifeq ($(TARGET_ARCH),win64)
CXXFLAGS := $(CXXFLAGS) -mconsole -static -static-libgcc -static-libstdc++
LDLIBS := $(LDLIBS) -lmingw32 -lws2_32
OBJECT := $(OBJECT).exe

else ifeq ($(TARGET_ARCH),win32)
CXXFLAGS := $(CXXFLAGS) -mconsole -static -static-libgcc -static-libstdc++
LDLIBS := $(LDLIBS) -lmingw32 -lws2_32
OBJECT := $(OBJECT).exe

endif

MKDIR_P ?= mkdir -p
CHMOD_X ?= chmod +x
STRINGS ?= strings

.PHONY: all build dist clean distclean

all: build build.extras

build: $(OBJECT)

clean:
	$(RM) $(EXISTING_OBJECTS)

$(OBJECT): main.cpp
	$(info $(VARS))
	$(MKDIR_P) $(@D)
	$(CXX) $(CXXFLAGS) -v -o $@ $^ $(LDLIBS)
	$(CHMOD_X) $@

install: $(OBJECT)
	cp $(BIN) $(PS2DEV)/bin

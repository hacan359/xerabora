# ps2ra PC client.
#
#   make            native build (Linux, macOS, MSYS2): needs libcurl
#   make windows    cross build for Windows with MinGW-w64: no external
#                   libraries, HTTPS goes through WinHTTP
#
# rcheevos is compiled from the submodule, unmodified.
# Notification sounds: tools/make-sounds.py regenerates sounds/*.wav and
# src/sounds_data.c.

RC      := ../third_party/rcheevos
COMMON  := ../opl/modules/network/common
# Version: the v* tag of the checked-out commit, else the default in
# src/version.h. Override with VERSION=1.2.3.
VERSION ?= $(shell git describe --tags --exact-match --match 'v*' 2>/dev/null | sed 's/^v//')

CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -std=gnu99 \
           -I$(RC)/include -I$(RC)/src -I$(RC)/src/rcheevos -I$(RC)/src/rapi -I$(COMMON)
ifneq ($(VERSION),)
CFLAGS  += -DPS2RA_VERSION=\"$(VERSION)\"
endif

HDRS := $(wildcard src/*.h) $(wildcard $(COMMON)/*.h)

RC_SRC := \
	$(RC)/src/rcheevos/alloc.c \
	$(RC)/src/rcheevos/condition.c \
	$(RC)/src/rcheevos/condset.c \
	$(RC)/src/rcheevos/consoleinfo.c \
	$(RC)/src/rcheevos/format.c \
	$(RC)/src/rcheevos/lboard.c \
	$(RC)/src/rcheevos/memref.c \
	$(RC)/src/rcheevos/operand.c \
	$(RC)/src/rcheevos/richpresence.c \
	$(RC)/src/rcheevos/runtime.c \
	$(RC)/src/rcheevos/runtime_progress.c \
	$(RC)/src/rcheevos/trigger.c \
	$(RC)/src/rcheevos/value.c \
	$(RC)/src/rapi/rc_api_common.c \
	$(RC)/src/rapi/rc_api_editor.c \
	$(RC)/src/rapi/rc_api_info.c \
	$(RC)/src/rapi/rc_api_runtime.c \
	$(RC)/src/rapi/rc_api_user.c \
	$(RC)/src/rhash/md5.c \
	$(RC)/src/rc_client.c \
	$(RC)/src/rc_compat.c \
	$(RC)/src/rc_util.c \
	$(RC)/src/rc_version.c

SRC := src/main.c src/config.c src/console.c src/log.c src/platform.c \
       src/ra.c src/snapshot.c src/sound.c src/sounds_data.c src/watchlist.c

all: ps2ra

ps2ra: $(SRC) src/http_curl.c $(RC_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) -lcurl -lm

WINCC ?= x86_64-w64-mingw32-gcc

windows: ps2ra.exe

ps2ra.exe: $(SRC) src/http_winhttp.c $(RC_SRC) $(HDRS)
	$(WINCC) $(CFLAGS) -D_WIN32_WINNT=0x0601 -static -o $@ $(filter %.c,$^) -lws2_32 -lwinhttp -lwinmm -lm -lpthread

clean:
	rm -f ps2ra ps2ra.exe

.PHONY: all windows clean

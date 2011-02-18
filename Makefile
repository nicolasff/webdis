#!/usr/bin/make -f
OUT=webdis

FORMAT_OBJS=formats/json.o formats/raw.o formats/common.o formats/custom-type.o formats/bson.o
OBJS = webdis.o conf.o $(FORMAT_OBJS) cmd.o slog.o server.o libb64/cencode.o acl.o md5/md5.o

CFLAGS=-O3 -Wall -Wextra -I.
LDFLAGS=-levent

## Simple flags

# Wether in-source libraries should be used or system-wide installed (defaults
# to in-source).
USE_EMBEDDED_HIREDIS?=true
USE_EMBEDDED_JANSSON?=true

# Wether static or dynamic library should be used. Note that with dynamic
# linking its important to install libraries somewhere to LD_LIBRARY_PATH which
# is not done yet. So curently it's only usefull when using system-wide
# installed libraries. Defaults to static.
USE_DYNAMIC_HIREDIS?=false
USE_DYNAMIC_JANSSON?=false

ifeq ($(USE_EMBEDDED_HIREDIS), true)
	HIREDIS_INCLUDE_DIR = hiredis
	HIREDIS_STATIC_LIB = hiredis/libhiredis.a
	HIREDIS_DYNAMIC_LIB = hiredis/libhiredis.so
	HIREDIS_LINK_DIR = hiredis
else
	HIREDIS_INCLUDE_DIR ?= /usr/include/hiredis
	HIREDIS_STATIC_LIB ?= /usr/lib/libhiredis.a
	HIREDIS_LINK_DIR ?= /usr/lib
endif

ifeq ($(USE_DYNAMIC_HIREDIS), false)
	OBJS += $(HIREDIS_STATIC_LIB)
else
	LIBS += $(HIREDIS_DYNAMIC_LIB)
	LDFLAGS += -L$(HIREDIS_LINK_DIR) -lhiredis
endif


ifeq ($(USE_EMBEDDED_JANSSON), true)
	JANSSON_INCLUDE_DIR = jansson/src
	JANSSON_STATIC_LIB=jansson/src/.libs/libjansson.a
	JANSSON_DYNAMIC_LIB=jansson/src/.libs/libjansson.so
	JANSSON_LINK_DIR = jansson/src/.libs
	JANSSON_EMBEDDED_HEADERS = jansson/src/jansson.h
else
	JANSSON_INCLUDE_DIR ?= /usr/include/
	JANSSON_LINK_DIR ?= /usr/lib
	JANSSON_STATIC_LIB ?= /usr/lib/libjansson.a
endif

ifeq ($(USE_DYNAMIC_JANSSON), false)
	OBJS += $(JANSSON_STATIC_LIB)
else
	LIBS += $(JANSSON_DYNAMIC_LIB)
	LDFLAGS += -L$(JANSSON_LINK_DIR) -ljansson
endif

# Although HIREDIS_INCLUDE_DIR may not be specified in CFLAGS since first
# lookup will be system-wide, we'll set it here, just in case
CFLAGS += -I$(HIREDIS_INCLUDE_DIR) -I$(JANSSON_INCLUDE_DIR)

all: $(OUT) Makefile

$(HIREDIS_STATIC_LIB) $(HIREDIS_DYNAMIC_LIB): Makefile hiredis/Makefile
	cd hiredis && $(MAKE) libhiredis.a libhiredis.so

$(JANSSON_STATIC_LIB) $(JANSSON_DYNAMIC_LIB): Makefile jansson/Makefile
	cd jansson && $(MAKE)

jansson/Makefile jansson/src/jansson.h: jansson/Makefile.am jansson/jansson.pc.in jansson/configure.ac jansson/src/jansson.h.in
	cd jansson && autoreconf -i && ./configure

$(OUT): $(OBJS) $(LIBS) Makefile
	$(CC) $(LDFLAGS) -o $(OUT) $(OBJS)

%.o: %.c %.h $(JANSSON_EMBEDDED_HEADERS) Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f $(OBJS) $(OUT)
	cd hiredis && $(MAKE) $@
	cd jansson && $(MAKE) $@


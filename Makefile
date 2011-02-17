#!/usr/bin/make -f
OUT=webdis
HIREDIS_STATIC_LIB=hiredis/libhiredis.a
JANSSON_STATIC_LIB=jansson/src/.libs/libjansson.a
FORMAT_OBJS=formats/json.o formats/raw.o formats/common.o formats/custom-type.o formats/bson.o
OBJS=webdis.o conf.o $(FORMAT_OBJS) cmd.o slog.o server.o $(HIREDIS_STATIC_LIB) $(JANSSON_STATIC_LIB) libb64/cencode.o acl.o md5/md5.o

CFLAGS=-O3 -Wall -Wextra -I. -Ijansson/src
LDFLAGS=-levent

all: $(OUT) Makefile

$(HIREDIS_STATIC_LIB): Makefile hiredis/Makefile
	cd hiredis && $(MAKE) libhiredis.a

$(JANSSON_STATIC_LIB): Makefile jansson/Makefile
	cd jansson && $(MAKE)

jansson/Makefile: jansson/Makefile.am jansson/jansson.pc.in jansson/configure.ac
	cd jansson && autoreconf -i && ./configure

$(OUT): $(OBJS) Makefile
	$(CC) $(LDFLAGS) -o $(OUT) $(OBJS)

%.o: %.c %.h Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f $(OBJS) $(OUT)
	cd hiredis && $(MAKE) $@
	cd jansson && $(MAKE) $@


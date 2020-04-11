OUT=webdis
HIREDIS_OBJ?=src/hiredis/hiredis.o src/hiredis/sds.o src/hiredis/net.o src/hiredis/async.o src/hiredis/read.o src/hiredis/dict.o
JANSSON_OBJ?=src/jansson/src/dump.o src/jansson/src/error.o src/jansson/src/hashtable.o src/jansson/src/load.o src/jansson/src/strbuffer.o src/jansson/src/utf.o src/jansson/src/value.o src/jansson/src/variadic.o
B64_OBJS?=src/b64/cencode.o
FORMAT_OBJS?=src/formats/json.o src/formats/raw.o src/formats/common.o src/formats/custom-type.o
HTTP_PARSER_OBJS?=src/http-parser/http_parser.o

CFLAGS ?= -O3 -Wall -Wextra -Isrc -Isrc/jansson/src -Isrc/http-parser -MD
LDFLAGS ?= -levent -pthread

# Pass preprocessor macros to the compile invocation
CFLAGS += $(CPPFLAGS)

# check for MessagePack
MSGPACK_LIB=$(shell ls /usr/lib/libmsgpack.so 2>/dev/null)
ifneq ($(strip $(MSGPACK_LIB)),)
	FORMAT_OBJS += src/formats/msgpack.o
	CFLAGS += -DMSGPACK=1
	LDFLAGS += -lmsgpack
else
# check for MessagePackC
MSGPACKC_LIB=$(shell ls /usr/lib/libmsgpackc.so 2>/dev/null)
ifneq ($(strip $(MSGPACKC_LIB)),)
	FORMAT_OBJS += src/formats/msgpack.o
	CFLAGS += -DMSGPACK=1
	LDFLAGS += -lmsgpackc
endif
endif


OBJS_DEPS=$(wildcard *.d)
DEPS=$(FORMAT_OBJS) $(HIREDIS_OBJ) $(JANSSON_OBJ) $(HTTP_PARSER_OBJS) $(B64_OBJS)
OBJS=src/webdis.o src/cmd.o src/worker.o src/slog.o src/server.o src/acl.o src/md5/md5.o src/sha1/sha1.o src/http.o src/client.o src/websocket.o src/pool.o src/conf.o $(DEPS)



PREFIX ?= /usr/local
CONFDIR ?= $(DESTDIR)/etc

INSTALL_DIRS = $(DESTDIR)$(PREFIX) \
	       $(DESTDIR)$(PREFIX)/bin \
	       $(CONFDIR)

all: $(OUT) Makefile

$(OUT): $(OBJS) Makefile
	$(CC) -o $(OUT) $(OBJS) $(LDFLAGS)

%.o: %.c %.h Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

$(INSTALL_DIRS):
	mkdir -p $@

clean:
	rm -f $(OBJS) $(OUT) $(OBJS_DEPS)

install: $(OUT) $(INSTALL_DIRS)
	cp $(OUT) $(DESTDIR)$(PREFIX)/bin
	cp webdis.prod.json $(CONFDIR)


WEBDIS_PORT ?= 7379

test_all: test perftest

test:
	python3 tests/basic.py
	python3 tests/limits.py
	./tests/pubsub -p $(WEBDIS_PORT)

perftest:
	# This is a performance test that requires apache2-utils and curl
	./tests/bench.sh

-include $(OBJS:.o=.d)

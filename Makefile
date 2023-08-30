OUT=webdis
HIREDIS_OBJ?=src/hiredis/hiredis.o src/hiredis/sds.o src/hiredis/net.o src/hiredis/async.o src/hiredis/read.o src/hiredis/dict.o src/hiredis/alloc.o src/hiredis/sockcompat.o
JANSSON_OBJ?=src/jansson/src/dump.o src/jansson/src/error.o src/jansson/src/hashtable.o src/jansson/src/hashtable_seed.o src/jansson/src/load.o src/jansson/src/memory.o src/jansson/src/pack_unpack.o src/jansson/src/strbuffer.o src/jansson/src/strconv.o src/jansson/src/utf.o src/jansson/src/value.o
B64_OBJS?=src/b64/cencode.o
FORMAT_OBJS?=src/formats/json.o src/formats/raw.o src/formats/common.o src/formats/custom-type.o
HTTP_PARSER_OBJS?=src/http-parser/http_parser.o

CFLAGS ?= -std=c99 -Wall -Wextra -Isrc -Isrc/jansson/src -Isrc/http-parser -MD -D_POSIX_C_SOURCE=200809L -Wno-pragmas
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
else
# check for MessagePack on macOS
MSGPACK_OSX_LIB=$(shell ls /usr/local/lib/libmsgpackc.dylib 2>/dev/null)
ifneq ($(strip $(MSGPACK_OSX_LIB)),)
	FORMAT_OBJS += src/formats/msgpack.o
	CFLAGS += -DMSGPACK=1
	LDFLAGS += -lmsgpackc
else
# check for MessagePackC using ld (returns 1 in both cases on macOS)
MSGPACKC_LD=$(shell ld -lmsgpackc >/dev/null 2>/dev/null; echo $$?)
ifeq ($(strip $(MSGPACKC_LD)),0)
	FORMAT_OBJS += src/formats/msgpack.o
	CFLAGS += -DMSGPACK=1
	LDFLAGS += -lmsgpackc
else
# check for MessagePack-C (note the dash)
MSGPACK_C_LD=$(shell ld -lmsgpack-c >/dev/null 2>/dev/null; echo $$?)
ifeq ($(strip $(MSGPACK_C_LD)),0)
	FORMAT_OBJS += src/formats/msgpack.o
	CFLAGS += -DMSGPACK=1
	LDFLAGS += -lmsgpack-c
endif # MSGPACK_C_LD
endif # MSGPACKC_LD
endif # MSGPACK_OSX_LIB
endif # MSGPACKC_LIB
endif # MSGPACK_LIB

# if `make` is run with DEBUG=1, include debug symbols
DEBUG_FLAGS=
ifeq ($(DEBUG),1)
	DEBUG_FLAGS += -O0
	ifeq ($(shell cc -v 2>&1 | grep -cw 'gcc version'),1) # GCC used: add GDB debugging symbols
		DEBUG_FLAGS += -ggdb3
	else ifeq ($(shell gcc -v 2>&1 | grep -cw 'clang version'),1) # Clang used: add LLDB debugging symbols
		DEBUG_FLAGS += -g3 -glldb
	endif
else
	DEBUG_FLAGS += -O3
endif

CFLAGS += $(DEBUG_FLAGS)

# if `make` is run with SSL=1, include hiredis SSL support
ifeq ($(SSL),1)
	HIREDIS_OBJ += src/hiredis/ssl.o
	CFLAGS += -DHAVE_SSL=1
	LDFLAGS +=  -lssl -lcrypto
	ifneq (, $(shell which brew)) # Homebrew
		CFLAGS += -I$(shell brew --prefix)/opt/openssl/include
		LDFLAGS += -L$(shell brew --prefix)/opt/openssl/lib
	endif
	# On Ubuntu and Alpine, LDFLAGS are enough since the SSL headers are under /usr/include/openssl
endif

OBJS_DEPS=$(wildcard *.d)
DEPS=$(FORMAT_OBJS) $(HIREDIS_OBJ) $(JANSSON_OBJ) $(HTTP_PARSER_OBJS) $(B64_OBJS)
OBJS=src/webdis.o src/cmd.o src/worker.o src/slog.o src/server.o src/acl.o src/md5/md5.o src/sha1/sha1.o src/http.o src/client.o src/websocket.o src/pool.o src/conf.o $(DEPS)


PREFIX ?= /usr/local
CONFDIR ?= $(DESTDIR)/etc
SELF_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

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
	find "$(SELF_DIR)" -name '*.d' -delete

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

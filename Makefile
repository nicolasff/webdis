OUT=webdis
HIREDIS_OBJ?=hiredis/hiredis.o hiredis/sds.o hiredis/net.o hiredis/async.o hiredis/read.o hiredis/dict.o
JANSSON_OBJ?=jansson/src/dump.o jansson/src/error.o jansson/src/hashtable.o jansson/src/load.o jansson/src/strbuffer.o jansson/src/utf.o jansson/src/value.o jansson/src/variadic.o
B64_OBJS?=b64/cencode.o
FORMAT_OBJS?=formats/json.o formats/raw.o formats/common.o formats/custom-type.o
HTTP_PARSER_OBJS?=http-parser/http_parser.o

CFLAGS ?= -O0 -ggdb -Wall -Wextra -I. -Ijansson/src -Ihttp-parser
LDFLAGS ?= -levent -pthread

# check for MessagePack
ifneq ($(findstring yes,$(shell pkg-config --exists msgpack && echo yes)),)
	FORMAT_OBJS += formats/msgpack.o
	CFLAGS += -DMSGPACK=1 $(shell pkg-config --cflags msgpack)
	LDFLAGS += $(shell pkg-config --libs msgpack)
else
	MSGPACK_LIB=$(shell ls /usr/lib/libmsgpack.so 2>/dev/null)
	ifneq ($(strip $(MSGPACK_LIB)),)
		FORMAT_OBJS += formats/msgpack.o
		CFLAGS += -DMSGPACK=1
		LDFLAGS += -lmsgpack
	endif
endif


DEPS=$(FORMAT_OBJS) $(HIREDIS_OBJ) $(JANSSON_OBJ) $(HTTP_PARSER_OBJS) $(B64_OBJS)
OBJS=webdis.o cmd.o worker.o slog.o server.o acl.o md5/md5.o sha1/sha1.o http.o client.o websocket.o pool.o conf.o $(DEPS)



PREFIX ?= /usr/local
CONFDIR ?= $(DESTDIR)/etc

INSTALL_DIRS = $(DESTDIR) \
	       $(DESTDIR)/$(PREFIX) \
	       $(DESTDIR)/$(PREFIX)/bin \
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
	rm -f $(OBJS) $(OUT)

install: $(OUT) $(INSTALL_DIRS)
	cp $(OUT) $(DESTDIR)/$(PREFIX)/bin
	cp webdis.prod.json $(CONFDIR)


WEBDIS_PORT ?= 7379

test_all: test perftest

test:
	python tests/basic.py
	python tests/limits.py
	./tests/pubsub -p $(WEBDIS_PORT)

perftest:
	# This is a performance test that requires apache2-utils and curl
	./tests/bench.sh

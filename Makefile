OUT=webdis
HIREDIS_OBJ=hiredis/hiredis.o hiredis/sds.o hiredis/net.o hiredis/async.o hiredis/dict.o
JANSSON_OBJ=jansson/src/dump.o jansson/src/error.o jansson/src/hashtable.o jansson/src/load.o jansson/src/strbuffer.o jansson/src/utf.o jansson/src/value.o jansson/src/variadic.o 
FORMAT_OBJS=formats/json.o formats/raw.o formats/common.o formats/custom-type.o formats/bson.o
HTTP_PARSER_OBJS=http-parser/http_parser.o
DEPS=$(FORMAT_OBJS) $(HIREDIS_OBJ) $(JANSSON_OBJ) $(HTTP_PARSER_OBJS)
OBJS=webdis.o conf.o cmd.o slog.o server.o libb64/cencode.o acl.o md5/md5.o http.o client.o $(DEPS)

CFLAGS=-O3 -Wall -Wextra -I. -Ijansson/src -Ihttp-parser
LDFLAGS=-levent

all: $(OUT) Makefile

$(OUT): $(OBJS) Makefile
	$(CC) $(LDFLAGS) -o $(OUT) $(OBJS)

%.o: %.c %.h Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f $(OBJS) $(OUT)


OUT=websocket pubsub
CFLAGS=-O0 -g -Wall -Wextra -I../src/http-parser/
LDFLAGS=-g -levent -lpthread

all: $(OUT) Makefile

websocket: websocket.o ../src/http-parser/http_parser.o
	$(CC) -o $@ $^ $(LDFLAGS)

pubsub: pubsub.o
	$(CC) -o $@ $< $(LDFLAGS)

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f *.o $(OUT)


OUT=websocket pubsub
CFLAGS=-O3 -Wall -Wextra
LDFLAGS=-levent -lpthread -lrt

all: $(OUT) Makefile

websocket: websocket.o
	$(CC) -o $@ $< $(LDFLAGS)

pubsub: pubsub.o
	$(CC) -o $@ $< $(LDFLAGS)

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f *.o $(OUT)


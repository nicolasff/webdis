OUT=websocket pubsub
CFLAGS=-O3 -Wall -Wextra
LDFLAGS=-levent -lpthread -lrt

all: $(OUT) Makefile

%: %.o Makefile
	$(CC) $(LDFLAGS) -o $@ $<

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f *.o $(OUT)


OUT=websocket
CFLAGS=-O3 -Wall -Wextra
LDFLAGS=-levent -lpthread

all: $(OUT) Makefile

%: %.o Makefile
	$(CC) $(LDFLAGS) -o $@ $<

%.o: %.c Makefile
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f *.o $(OUT)


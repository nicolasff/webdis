OUT=test
HIREDIS_DIR=../hiredis
HIREDIS_OBJ=$(HIREDIS_DIR)/hiredis.o $(HIREDIS_DIR)/sds.o $(HIREDIS_DIR)/net.o $(HIREDIS_DIR)/async.o
OBJS=test.o $(HIREDIS_OBJ)
CFLAGS=-O0 -ggdb -Wall -Wextra -I. -I $(HIREDIS_DIR)/..
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


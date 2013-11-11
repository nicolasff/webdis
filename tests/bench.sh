#!/bin/bash
CLIENTS=100
REQUESTS=100000

HOST=$WEBDIS_HOST
PORT=$WEBDIS_PORT

[ -n $HOST ] && HOST=127.0.0.1
[ -n $PORT ] && PORT=7379

info() {
	echo "Testing on $HOST:$PORT with $CLIENTS clients in parallel, for a total of $REQUESTS requests per benchmark."
}

once() {
	curl -q http://$HOST:$PORT/$1 1> /dev/null 2> /dev/null
}

bench() {
	NUM=`ab -k -c $CLIENTS -n $REQUESTS http://$HOST:$PORT/$1 2>/dev/null | grep "#/sec" | sed -e "s/[^0-9.]//g"`
	echo -ne $NUM
}

test_ping() {
	echo -en "PING: "
	bench "PING"
	echo " requests/sec."
}

test_set() {
	echo -en "SET(hello,world): "
	bench "SET/hello/world"
	echo " requests/sec."
}

test_get() {
	echo -en "GET(hello): "
	bench "GET/hello"
	echo " requests/sec."
}

test_incr() {
	once "DEL/hello"

	echo -en "INCR(hello): "
	bench "INCR/hello"
	echo " requests/sec."
}

test_lpush() {
	once "DEL/hello"

	echo -en "LPUSH(hello,abc): "
	bench "LPUSH/hello/abc"
	echo " requests/sec."
}

test_lrange() {
	echo -en "LRANGE(hello,$1,$2): "
	bench "LRANGE/hello/$1/$2"
	echo " requests/sec."
}

info
test_ping
test_set
test_get
test_incr
test_lpush
test_lrange 0 10
test_lrange 0 100

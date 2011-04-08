#!/bin/bash
CLIENTS=100
REQUESTS=100000
HOST=127.0.0.1
PORT=7379

info() {
	echo "Testing on $HOST:$PORT with $CLIENTS clients in parallel, for a total of $REQUESTS requests per benchmark."
}

test_ping() {
	echo -en "PING: "
	NUM=`ab -k -c $CLIENTS -n $REQUESTS http://$HOST:$PORT/PING 2>/dev/null | grep "#/sec" | sed -e "s/[^0-9.]//g"`
	echo "$NUM requests/sec."
}

test_set() {
	echo -en "SET(hello,world): "
	NUM=`ab -k -c $CLIENTS -n $REQUESTS http://$HOST:$PORT/SET/hello/world 2>/dev/null | grep "#/sec" | sed -e "s/[^0-9.]//g"`
	echo "$NUM requests/sec."
}

test_get() {
	echo -en "GET(hello): "
	NUM=`ab -k -c $CLIENTS -n $REQUESTS http://$HOST:$PORT/GET/hello 2>/dev/null | grep "#/sec" | sed -e "s/[^0-9.]//g"`
	echo "$NUM requests/sec."
}

test_lpush() {
	echo -en "LPUSH(hello,abc): "
	curl -q http://$HOST:$PORT/DEL/hello 1> /dev/null 2> /dev/null
	NUM=`ab -k -c $CLIENTS -n $REQUESTS http://$HOST:$PORT/LPUSH/hello/abc 2>/dev/null | grep "#/sec" | sed -e "s/[^0-9.]//g"`
	echo "$NUM requests/sec."
}

test_lrange() {
	echo -en "LRANGE(hello,0,100): "
	NUM=`ab -k -c $CLIENTS -n $REQUESTS http://$HOST:$PORT/LRANGE/hello/0/100 2>/dev/null | grep "#/sec" | sed -e "s/[^0-9.]//g"`
	echo "$NUM requests/sec."
}

info
test_ping
test_set
test_get
test_lpush
test_lrange

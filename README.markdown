# About

A very simple prototype providing an HTTP interface to Redis. It uses [hiredis](https://github.com/antirez/hiredis) and [jansson](https://github.com/akheron/jansson).

<pre>
make clean all
./turnip &
curl http://127.0.0.1:7379/SET/hello/world
→ {"SET":[true,"OK"]}
curl http://127.0.0.1:7379/GET/hello
→ {"GET":"world"}

curl -d "GET/hello" http://127.0.0.1:7379/
→ {"GET":"world"}

</pre>

# Features
* GET and POST are supported.
* JSON output by default, optional JSONP parameter.
* Raw Redis 2.0 protocol output with `?format=raw`
* HTTP 1.1 pipelining (45 kqps on a desktop Linux machine.)
* Connects to Redis using a TCP or UNIX socket.

# Ideas, TODO...
* Add meta-data info per key (MIME type in a second key, for instance).
* Support PUT, DELETE, HEAD?
* Support pub/sub.
* Disable MULTI/EXEC/DISCARD/WATCH.
* Add logging.
* Enrich config file:
	* Provide timeout (this needs to be added to hiredis first.)
	* Restrict commands by IP range
* Send your ideas using the github tracker or on twitter [@yowgi](http://twitter.com/yowgi).

# HTTP error codes
* Unknown HTTP verb: 405 Method Not Allowed
* Redis is unreachable: 503 Service Unavailable
* Could also be used:
	* Timeout on the redis side: 503 Service Unavailable
	* Missing key: 404 Not Found

# Command format
The URI `/COMMAND/arg0/arg1/.../argN` executes the command on Redis and returns the response to the client. GET and POST are supported:

* `GET /COMMAND/arg0/.../argN`
* `POST /` with `COMMAND/arg0/.../argN` in the HTTP body.

# JSON output
JSON is the default output format. Each command returns a JSON object with the command as a key and the result as a value.

**Examples:**
<pre>
// string
$ curl http://127.0.0.1:7379/GET/y
{"GET":"41"}

// number
$ curl http://127.0.0.1:7379/INCR/y
{"INCR":42}

// list
$ curl http://127.0.0.1:7379/LRANGE/x/0/1
{"LRANGE":["abc","def"]}

// status
$ curl http://127.0.0.1:7379/TYPE/y
{"TYPE":[true,"string"]}

// error, which is basically a status
$ curl http://127.0.0.1:7379/MAKE-ME-COFFEE
{"MAKE-ME-COFFEE":[false,"ERR unknown command 'MAKE-ME-COFFEE'"]}

// JSONP callback:
$ curl  "http://127.0.0.1:7379/TYPE/y?jsonp=myCustomFunction"
myCustomFunction({"TYPE":[true,"string"]})

</pre>

# RAW output
This is the raw output of Redis; enable it with `?format=raw`.
<pre>

// string
$ curl http://127.0.0.1:7379/GET/z?format=raw
hello

// number
curl http://127.0.0.1:7379/INCR/a?format=raw
:2

// list
$ curl http://127.0.0.1:7379/LRANGE/x/0/-1?format=raw
*2
$3
abc
$3
def

// status
$ curl http://127.0.0.1:7379/TYPE/y?format=raw
+zset

// error, which is basically a status
$ curl http://127.0.0.1:7379/MAKE-ME-COFFEE?format=raw
-ERR unknown command 'ABC'

</pre>

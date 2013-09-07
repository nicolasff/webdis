# About

A very simple web server providing an HTTP interface to Redis. It uses [hiredis](https://github.com/antirez/hiredis), [jansson](https://github.com/akheron/jansson), [libevent](http://monkey.org/~provos/libevent/), and [http-parser](https://github.com/ry/http-parser/).

Webdis depends on libevent-dev. You can install it on Ubuntu by typing `sudo apt-get install libevent-dev` or on OS X by typing `brew install libevent`.
<pre>
make clean all

./webdis &

curl http://127.0.0.1:7379/SET/hello/world
→ {"SET":[true,"OK"]}

curl http://127.0.0.1:7379/GET/hello
→ {"GET":"world"}

curl -d "GET/hello" http://127.0.0.1:7379/
→ {"GET":"world"}

</pre>

# Features
* `GET` and `POST` are supported, as well as `PUT` for file uploads.
* JSON output by default, optional JSONP parameter (`?jsonp=myFunction` or `?callback=myFunction`).
* Raw Redis 2.0 protocol output with `.raw` suffix
* MessagePack output with `.msg` suffix
* HTTP 1.1 pipelining (70,000 http requests per second on a desktop Linux machine.)
* Multi-threaded server, configurable number of worker threads.
* WebSocket support (Currently using the “hixie-76” specification).
* Connects to Redis using a TCP or UNIX socket.
* Restricted commands by IP range (CIDR subnet + mask) or HTTP Basic Auth, returning 403 errors.
* Possible Redis authentication in the config file.
* Pub/Sub using `Transfer-Encoding: chunked`, works with JSONP as well. Webdis can be used as a Comet server.
* Drop privileges on startup.
* Custom Content-Type using a pre-defined file extension, or with `?type=some/thing`.
* URL-encoded parameters for binary data or slashes and question marks. For instance, `%2f` is decoded as `/` but not used as a command separator.
* Logs, with a configurable verbosity.
* Cross-origin requests, usable with XMLHttpRequest2 (Cross-Origin Resource Sharing - CORS).
* File upload with PUT.
* With the JSON output, the return value of INFO is parsed and transformed into an object.
* Optional daemonize: set `"daemonize": true` and `"pidfile": "/var/run/webdis.pid"` in webdis.json.
* Default root object: Add `"default_root": "/GET/index.html"` in webdis.json to substitute the request to `/` with a Redis request.
* HTTP request limit with `http_max_request_size` (in bytes, set to 128MB by default).
* Database selection in the URL, using e.g. `/7/GET/key` to run the command on DB 7.

# Ideas, TODO...
* Add better support for PUT, DELETE, HEAD, OPTIONS? How? For which commands?
	* This could be done using a “strict mode” with a table of commands and the verbs that can/must be used with each command. Strict mode would be optional, configurable. How would webdis know of new commands remains to be determined.
* MULTI/EXEC/DISCARD/WATCH are disabled at the moment; find a way to use them.
* Support POST of raw Redis protocol data, and execute the whole thing. This could be useful for MULTI/EXEC transactions.
* Enrich config file:
	* Provide timeout (maybe for some commands only?). What should the response be? 504 Gateway Timeout? 503 Service Unavailable?
* Multi-server support, using consistent hashing.
* SSL?
	* Not sure if this is such a good idea.
* SPDY?
	* SPDY is mostly useful for parallel fetches. Not sure if it would make sense for Webdis.
* Send your ideas using the github tracker, on twitter [@yowgi](http://twitter.com/yowgi) or by mail to n.favrefelix@gmail.com.

# HTTP error codes
* Unknown HTTP verb: 405 Method Not Allowed.
* Redis is unreachable: 503 Service Unavailable.
* Matching ETag sent using `If-None-Match`: 304 Not Modified.
* Could also be used:
	* Timeout on the redis side: 503 Service Unavailable.
	* Missing key: 404 Not Found.
	* Unauthorized command (disabled in config file): 403 Forbidden.

# Command format
The URI `/COMMAND/arg0/arg1/.../argN.ext` executes the command on Redis and returns the response to the client. GET, POST, and PUT are supported:

* `GET /COMMAND/arg0/.../argN.ext`
* `POST /` with `COMMAND/arg0/.../argN` in the HTTP body.
* `PUT /COMMAND/arg0.../argN-1` with `argN` in the HTTP body (see section on [file uploads](#file-upload).)

`.ext` is an optional extension; it is not read as part of the last argument but only represents the output format. Several formats are available (see below).

Special characters: `/` and `.` have special meanings, `/` separates arguments and `.` changes the Content-Type. They can be replaced by `%2f` and `%2e`, respectively.

# ACL
Access control is configured in `webdis.json`. Each configuration tries to match a client profile according to two criterias:

* [CIDR](http://en.wikipedia.org/wiki/CIDR) subnet + mask
* [HTTP Basic Auth](http://en.wikipedia.org/wiki/Basic_access_authentication) in the format of "user:password".

Each ACL contains two lists of commands, `enabled` and `disabled`. All commands being enabled by default, it is up to the administrator to disable or re-enable them on a per-profile basis.
Examples:
<pre>
{
	"disabled":	["DEBUG", "FLUSHDB", "FLUSHALL"],
},

{
	"http_basic_auth": "user:password",
	"disabled":	["DEBUG", "FLUSHDB", "FLUSHALL"],
	"enabled":	["SET"]
},

{
	"ip": 		"192.168.10.0/24",
	"enabled":	["SET"]
},

{
	"http_basic_auth": "user:password",
	"ip": 		"192.168.10.0/24",
	"enabled":	["SET", "DEL"]
}
</pre>
ACLs are interpreted in order, later authorizations superseding earlier ones if a client matches several. The special value "*" matches all commands.

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
This is the raw output of Redis; enable it with the `.raw` suffix.
<pre>

// string
$ curl http://127.0.0.1:7379/GET/z.raw
$5
hello

// number
curl http://127.0.0.1:7379/INCR/a.raw
:2

// list
$ curl http://127.0.0.1:7379/LRANGE/x/0/-1.raw
*2
$3
abc
$3
def

// status
$ curl http://127.0.0.1:7379/TYPE/y.raw
+zset

// error, which is basically a status
$ curl http://127.0.0.1:7379/MAKE-ME-COFFEE.raw
-ERR unknown command 'MAKE-ME-COFFEE'
</pre>

# Custom content-type
Several content-types are available:

* `.json` for `application/json` (this is the default Content-Type).
* `.msg` for `application/x-msgpack`. See [http://msgpack.org/](http://msgpack.org/) for the specs.
* `.txt` for `text/plain`
* `.html` for `text/html`
* `xhtml` for `application/xhtml+xml`
* `xml` for `text/xml`
* `.png` for `image/png`
* `jpg` or `jpeg` for `image/jpeg`
* Any other with the `?type=anything/youwant` query string.
* Add a custom separator for list responses with `?sep=,` query string.

<pre>
curl -v "http://127.0.0.1:7379/GET/hello.html"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: text/html
&lt; Date: Mon, 03 Jan 2011 20:43:36 GMT
&lt; Content-Length: 137
&lt;
&lt;!DOCTYPE html&gt;
&lt;html&gt;
[...]
&lt;/html&gt;

curl -v "http://127.0.0.1:7379/GET/hello.txt"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: text/plain
&lt; Date: Mon, 03 Jan 2011 20:43:36 GMT
&lt; Content-Length: 137
[...]

curl -v "http://127.0.0.1:7379/GET/big-file?type=application/pdf"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: application/pdf
&lt; Date: Mon, 03 Jan 2011 20:45:12 GMT
[...]
</pre>

# File upload
Webdis supports file upload using HTTP PUT. The command URI is slightly different, as the last argument is taken from the HTTP body.
For example: instead of `/SET/key/value`, the URI becomes `/SET/key` and the value is the entirety of the body. This works for other commands such as LPUSH, etc.

**Uploading a binary file to webdis**:
<pre>
$ file redis-logo.png
redis-logo.png: PNG image, 513 x 197, 8-bit/color RGBA, non-interlaced

$ wc -c redis-logo.png
16744 redis-logo.png

$ curl -v --upload-file redis-logo.png http://127.0.0.1:7379/SET/logo
[...]
&gt; PUT /SET/logo HTTP/1.1
&gt; User-Agent: curl/7.19.7 (x86_64-pc-linux-gnu) libcurl/7.19.7 OpenSSL/0.9.8k zlib/1.2.3.3 libidn/1.15
&gt; Host: 127.0.0.1:7379
&gt; Accept: */*
&gt; Content-Length: 16744
&gt; Expect: 100-continue
&gt;
&lt; HTTP/1.1 100 Continue
&lt; HTTP/1.1 200 OK
&lt; Content-Type: application/json
&lt; ETag: "0db1124cf79ffeb80aff6d199d5822f8"
&lt; Date: Sun, 09 Jan 2011 16:48:19 GMT
&lt; Content-Length: 19
&lt;
{"SET":[true,"OK"]}

$ curl -vs http://127.0.0.1:7379/GET/logo.png -o out.png
&gt; GET /GET/logo.png HTTP/1.1
&gt; User-Agent: curl/7.19.7 (x86_64-pc-linux-gnu) libcurl/7.19.7 OpenSSL/0.9.8k zlib/1.2.3.3 libidn/1.15
&gt; Host: 127.0.0.1:7379
&gt; Accept: */*
&gt;
&lt; HTTP/1.1 200 OK
&lt; Content-Type: image/png
&lt; ETag: "1991df597267d70bf9066a7d11969da0"
&lt; Date: Sun, 09 Jan 2011 16:50:51 GMT
&lt; Content-Length: 16744

$ md5sum redis-logo.png out.png
1991df597267d70bf9066a7d11969da0  redis-logo.png
1991df597267d70bf9066a7d11969da0  out.png
</pre>

The file was uploaded and re-downloaded properly: it has the same hash and the content-type was set properly thanks to the `.png` extension.

# WebSockets
Webdis supports WebSocket clients implementing [dixie-76](http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-76).  
Web Sockets are supported with the following formats, selected by the connection URL:

* JSON (on `/` or `/.json`)
* Raw Redis wire protocol (on `/.raw`)

**Example**:
<pre>
function testJSON() {
	var jsonSocket = new WebSocket("ws://127.0.0.1:7379/.json");
	jsonSocket.onopen = function() {

		console.log("JSON socket connected!");
		jsonSocket.send(JSON.stringify(["SET", "hello", "world"]));
		jsonSocket.send(JSON.stringify(["GET", "hello"]));
	};
	jsonSocket.onmessage = function(messageEvent) {
		console.log("JSON received:", messageEvent.data);
	};
}
testJSON();
</pre>

This produces the following output:
<pre>
JSON socket connected!
JSON received: {"SET":[true,"OK"]}
JSON received: {"GET":"world"}
</pre>

# Pub/Sub with chunked transfer encoding
Webdis exposes Redis PUB/SUB channels to HTTP clients, forwarding messages in the channel as they are published by Redis. This is done using chunked transfer encoding.

**Example using XMLHttpRequest**:
<pre>
var previous_response_length = 0
xhr = new XMLHttpRequest()
xhr.open("GET", "http://127.0.0.1:7379/SUBSCRIBE/hello", true);
xhr.onreadystatechange = checkData;
xhr.send(null);

function checkData() {
	if(xhr.readyState == 3)  {
    	response = xhr.responseText;
    	chunk = response.slice(previous_response_length);
    	previous_response_length = response.length;
    	console.log(chunk);
    }
};
</pre>

Publish messages to redis to see output similar to the following:
<pre>
{"SUBSCRIBE":["subscribe","hello",1]}
{"SUBSCRIBE":["message","hello","some message"]}
{"SUBSCRIBE":["message","hello","some other message"]} 
</pre>
